#include "backend.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSettings>
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

Backend::Backend(const QString &segmentsUrl, const QString &broadcastUrl, QObject *parent)
    : QObject(parent),
      m_segmentsUrl(segmentsUrl.isEmpty() ? kSegments : segmentsUrl),
      m_broadcastUrl(broadcastUrl.isEmpty() ? kBroadcast : broadcastUrl) {
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
