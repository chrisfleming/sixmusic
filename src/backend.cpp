#include "backend.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QUrl>
#include <QtMath>

namespace {
// The 320 kbit/s HLS feed the BBC publishes for UK listeners.
const char *kStream = "https://as-hls-ww-live.akamaized.net/pool_81827798/live/ww/bbc_6music/"
                      "bbc_6music.isml/bbc_6music-audio%3d320000.norewind.m3u8";
const char *kSegments = "https://rms.api.bbc.co.uk/v2/services/bbc_6music/segments/latest"
                        "?experience=domestic&offset=0&limit=8";
const char *kBroadcast = "https://rms.api.bbc.co.uk/v2/broadcasts/latest?service=bbc_6music&on_air=now";
}

Backend::Backend(const QString &segmentsUrl, const QString &broadcastUrl, const QString &lovedFile, QObject *parent)
    : QObject(parent),
      m_segmentsUrl(segmentsUrl.isEmpty() ? kSegments : segmentsUrl),
      m_broadcastUrl(broadcastUrl.isEmpty() ? kBroadcast : broadcastUrl),
      m_lovedFile(lovedFile.isEmpty()
                      ? QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/sixmusic/loved.json"
                      : lovedFile) {
    loadLoved();
    m_player.setAudioOutput(&m_output);

    QSettings settings;
    m_output.setVolume(settings.value("volume", 0.8).toDouble());

    connect(&m_player, &QMediaPlayer::playbackStateChanged, this, [this] {
        if (playing())
            m_streamError.clear();
        emit playingChanged();
    });
    connect(&m_player, &QMediaPlayer::mediaStatusChanged, this, [this] { emit playingChanged(); });
    connect(&m_player, &QMediaPlayer::errorOccurred, this, [this](QMediaPlayer::Error, const QString &message) {
        m_streamError = message.isEmpty() ? tr("Unknown error") : message;
        emit playingChanged();
    });
    // Outputs come and go as speakers are plugged in; re-resolve the saved
    // choice each time so a returning device is picked up again.
    m_outputId = settings.value("output").toString();
    connect(&m_devices, &QMediaDevices::audioOutputsChanged, this, &Backend::applyOutput);
    applyOutput();

    connect(&m_output, &QAudioOutput::volumeChanged, this, &Backend::volumeChanged);
    connect(&m_output, &QAudioOutput::mutedChanged, this, &Backend::volumeChanged);

    // The BBC updates the track a few seconds after it starts, so a short
    // poll keeps the display close without hammering the service.
    m_poll.setInterval(12000);
    connect(&m_poll, &QTimer::timeout, this, &Backend::refresh);
    m_poll.start();
    refresh();
}

bool Backend::buffering() const {
    if (!playing())
        return false;
    const auto status = m_player.mediaStatus();
    return status == QMediaPlayer::LoadingMedia || status == QMediaPlayer::StalledMedia
        || status == QMediaPlayer::BufferingMedia;
}

void Backend::setVolume(double volume) {
    m_output.setVolume(qBound(0.0, volume, 1.0));
    QSettings().setValue("volume", m_output.volume());
}

QVariantList Backend::outputs() const {
    QVariantList list{QVariantMap{{"id", QString()}, {"name", QString()}}};
    for (const auto &device : QMediaDevices::audioOutputs())
        list.append(QVariantMap{{"id", QString::fromUtf8(device.id())}, {"name", device.description()}});
    return list;
}

int Backend::outputIndex() const {
    const auto list = outputs();
    for (int i = 1; i < list.size(); ++i)
        if (list.at(i).toMap().value("id").toString() == m_outputId)
            return i;
    return 0;
}

void Backend::chooseOutput(int index) {
    const auto list = outputs();
    if (index >= 0 && index < list.size())
        setOutput(list.at(index).toMap().value("id").toString());
}

void Backend::setOutput(const QString &id) {
    m_outputId = id;
    QSettings().setValue("output", id);
    applyOutput();
}

// A device that has gone away falls back to the system default, but the
// choice is kept so it returns when the device does.
void Backend::applyOutput() {
    QAudioDevice chosen = QMediaDevices::defaultAudioOutput();
    if (!m_outputId.isEmpty())
        for (const auto &device : QMediaDevices::audioOutputs())
            if (QString::fromUtf8(device.id()) == m_outputId)
                chosen = device;
    if (m_output.device() != chosen)
        m_output.setDevice(chosen);
    emit outputsChanged();
}

QString Backend::lovedKey(const QString &artist, const QString &track) {
    return artist.trimmed().toLower() + '\t' + track.trimmed().toLower();
}

bool Backend::isLoved(const QString &artist, const QString &track) const {
    const QString key = lovedKey(artist, track);
    for (const auto &item : m_loved)
        if (lovedKey(item.artist, item.track) == key)
            return true;
    return false;
}

// Newest first, which is how people look for what they just loved.
QVariantList Backend::lovedTracks() const {
    QVariantList list;
    for (auto it = m_loved.crbegin(); it != m_loved.crend(); ++it)
        list.append(QVariantMap{{"artist", it->artist}, {"track", it->track}, {"show", it->show}, {"when", it->when}});
    return list;
}

bool Backend::toggleCurrentLoved() {
    if (!m_onMusic)
        return false;
    toggleLoved(m_artist, m_track);
    return true;
}

void Backend::toggleLoved(const QString &artist, const QString &track) {
    if (!m_lovedWritable || artist.isEmpty() || track.isEmpty())
        return;
    const QString key = lovedKey(artist, track);
    const auto before = m_loved;
    bool removed = false;
    for (int i = m_loved.size() - 1; i >= 0; --i)
        if (lovedKey(m_loved.at(i).artist, m_loved.at(i).track) == key) {
            m_loved.removeAt(i);
            removed = true;
        }
    if (!removed)
        m_loved.append({artist, track, m_showTitle, QDateTime::currentDateTime()});
    // Keep the list on screen in step with the file: undo if it cannot be saved.
    if (!saveLoved())
        m_loved = before;
    emit lovedChanged();
}

// The log is a plain JSON array so it can be read, grepped, or exported.
// A file that cannot be parsed is left alone, never overwritten.
void Backend::loadLoved() {
    QFile file(m_lovedFile);
    if (!file.exists())
        return;
    if (!file.open(QIODevice::ReadOnly)) {
        m_lovedWritable = false;
        m_lovedError = tr("Can't read %1: %2").arg(m_lovedFile, file.errorString());
        return;
    }
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isArray()) {
        m_lovedWritable = false;
        m_lovedError = tr("Can't read %1: not a list of tracks").arg(m_lovedFile);
        return;
    }
    for (const auto &value : document.array()) {
        const auto item = value.toObject();
        m_loved.append({item.value("artist").toString(), item.value("track").toString(),
                        item.value("show").toString(),
                        QDateTime::fromString(item.value("loved_at").toString(), Qt::ISODate).toLocalTime()});
    }
}

bool Backend::saveLoved() {
    QJsonArray array;
    for (const auto &item : m_loved)
        array.append(QJsonObject{{"artist", item.artist}, {"track", item.track}, {"show", item.show},
                                 {"loved_at", item.when.toUTC().toString(Qt::ISODate)}});
    QDir().mkpath(QFileInfo(m_lovedFile).absolutePath());
    // QSaveFile writes beside the target and renames, so a crash cannot
    // leave a half-written log.
    QSaveFile file(m_lovedFile);
    if (!file.open(QIODevice::WriteOnly) || file.write(QJsonDocument(array).toJson()) < 0 || !file.commit()) {
        m_lovedError = tr("Can't save %1: %2").arg(m_lovedFile, file.errorString());
        return false;
    }
    m_lovedError.clear();
    return true;
}

void Backend::setMuted(bool muted) { m_output.setMuted(muted); }

void Backend::nudgeVolume(double delta) {
    if (muted())
        setMuted(false);
    setVolume(qRound((volume() + delta) * 100) / 100.0);
}

void Backend::toggle() {
    if (playing())
        stop();
    else
        play();
}

void Backend::play() {
    m_streamError.clear();
    // A live stream has no useful pause: starting again must rejoin the
    // present, so reload the source instead of resuming a stale buffer.
    // Qt ignores setSource() with the URL it already has, and after stop() the
    // old pipeline is gone, so clear it first to force a fresh connection.
    m_player.setSource(QUrl());
    m_player.setSource(QUrl(kStream));
    m_player.play();
    emit playingChanged();
}

void Backend::stop() {
    m_player.stop();
    emit playingChanged();
}

void Backend::refresh() {
    fetch(m_segmentsUrl, &Backend::applySegments);
    fetch(m_broadcastUrl, &Backend::applyBroadcast);
}

void Backend::fetch(const QString &url, bool (Backend::*apply)(const QByteArray &)) {
    QNetworkRequest request{QUrl(url)};
    request.setHeader(QNetworkRequest::UserAgentHeader, "sixmusic");
    request.setTransferTimeout(10000);
    auto *reply = m_network.get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, apply] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            setInfoError(reply->errorString());
            return;
        }
        if (!(this->*apply)(reply->readAll()))
            setInfoError(tr("Unexpected reply from the BBC"));
        else
            setInfoError({});
    });
}

void Backend::setInfoError(const QString &message) {
    if (message == m_infoError)
        return;
    m_infoError = message;
    emit infoErrorChanged();
}

// BBC image URLs carry a {recipe} placeholder for the size, e.g. 320x320.
QString Backend::artworkUrl(QString url, int size) {
    return url.replace("{recipe}", QString("%1x%1").arg(size));
}

bool Backend::applySegments(const QByteArray &json) {
    const auto data = QJsonDocument::fromJson(json).object().value("data");
    if (!data.isArray())
        return false;

    bool onMusic = false;
    QString artist, track, artwork;
    QVariantList recent;
    for (const auto &value : data.toArray()) {
        const auto item = value.toObject();
        if (item.value("segment_type").toString() != "music")
            continue;
        const auto titles = item.value("titles").toObject();
        const QString itemArtist = titles.value("primary").toString();
        const QString itemTrack = titles.value("secondary").toString();
        const QString image = artworkUrl(item.value("image_url").toString(), 320);
        if (item.value("offset").toObject().value("now_playing").toBool()) {
            onMusic = true;
            artist = itemArtist;
            track = itemTrack;
            artwork = image;
        } else {
            recent.append(QVariantMap{{"artist", itemArtist}, {"track", itemTrack},
                                      {"when", item.value("offset").toObject().value("label").toString()}});
        }
    }

    if (onMusic == m_onMusic && artist == m_artist && track == m_track && artwork == m_artwork
        && recent == m_recent)
        return true;
    m_onMusic = onMusic;
    m_artist = artist;
    m_track = track;
    m_artwork = artwork;
    m_recent = recent;
    emit nowChanged();
    // Whether the track on air is loved depends on which track that is.
    emit lovedChanged();
    return true;
}

bool Backend::applyBroadcast(const QByteArray &json) {
    const auto data = QJsonDocument::fromJson(json).object().value("data");
    if (!data.isArray())
        return false;
    const auto items = data.toArray();
    if (items.isEmpty())
        return true;

    const auto broadcast = items.first().toObject();
    const auto programme = broadcast.value("programme").toObject();
    const auto titles = programme.value("titles").toObject();
    const QString title = titles.value("primary").toString();
    const QString subtitle = titles.value("secondary").toString();
    // Programmes list their pictures under "images", unlike segments.
    const auto images = programme.value("images").toArray();
    const QString image = images.isEmpty() ? QString()
        : artworkUrl(images.first().toObject().value("url").toString(), 320);
    const auto start = QDateTime::fromString(broadcast.value("start").toString(), Qt::ISODate).toLocalTime();
    const auto end = QDateTime::fromString(broadcast.value("end").toString(), Qt::ISODate).toLocalTime();

    if (title == m_showTitle && subtitle == m_showSubtitle && image == m_showImage && start == m_showStart
        && end == m_showEnd)
        return true;
    m_showTitle = title;
    m_showSubtitle = subtitle;
    m_showImage = image;
    m_showStart = start;
    m_showEnd = end;
    emit nowChanged();
    return true;
}
