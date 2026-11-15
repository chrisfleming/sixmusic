#include "lastfm.h"

#include <QCryptographicHash>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUrlQuery>

Lastfm::Lastfm(const QString &configFile, const QString &queueFile, const QString &apiUrl, QObject *parent)
    : QObject(parent),
      m_configFile(configFile.isEmpty()
                       ? QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + "/sixmusic/lastfm.json"
                       : configFile),
      m_queueFile(queueFile.isEmpty()
                      ? QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/sixmusic/scrobble-queue.json"
                      : queueFile),
      m_apiUrl(apiUrl.isEmpty() ? "https://ws.audioscrobbler.com/2.0/" : apiUrl) {
    m_retry.setSingleShot(true);
    m_retry.setInterval(60000);
    connect(&m_retry, &QTimer::timeout, this, &Lastfm::flush);
    m_poll.setInterval(4000);
    connect(&m_poll, &QTimer::timeout, this, &Lastfm::pollSession);
    // A token lives an hour, but nobody should be left waiting that long.
    m_deadline.setSingleShot(true);
    m_deadline.setInterval(5 * 60 * 1000);
    connect(&m_deadline, &QTimer::timeout, this, [this] {
        stopWaiting();
        setError(tr("Last.fm access was not approved in time. Try again."));
    });
    loadConfig();
    loadQueue();
    if (!m_queue.isEmpty())
        m_retry.start(5000);
}

QString Lastfm::sign(const QMap<QString, QString> &params, const QString &secret) {
    QByteArray text;
    // QMap iterates in key order, which is the order Last.fm signs in.
    for (auto it = params.cbegin(); it != params.cend(); ++it)
        if (it.key() != "format" && it.key() != "callback")
            text += it.key().toUtf8() + it.value().toUtf8();
    text += secret.toUtf8();
    return QString::fromLatin1(QCryptographicHash::hash(text, QCryptographicHash::Md5).toHex());
}

void Lastfm::call(const QString &method, QMap<QString, QString> params, bool post, Done done) {
    params["method"] = method;
    params["api_key"] = m_apiKey;
    if (!m_session.isEmpty() && method != "auth.getToken" && method != "auth.getSession")
        params["sk"] = m_session;
    params["api_sig"] = sign(params, m_secret);
    params["format"] = "json";

    // Encoded by hand: a "+" or "&" in a title must survive, and form
    // encoding would read a bare "+" as a space.
    QByteArray body;
    for (auto it = params.cbegin(); it != params.cend(); ++it) {
        if (!body.isEmpty())
            body += '&';
        body += QUrl::toPercentEncoding(it.key()) + '=' + QUrl::toPercentEncoding(it.value());
    }

    QNetworkRequest request;
    request.setHeader(QNetworkRequest::UserAgentHeader, "sixmusic");
    request.setTransferTimeout(15000);
    QNetworkReply *reply;
    if (post) {
        request.setUrl(QUrl(m_apiUrl));
        request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
        reply = m_network.post(request, body);
    } else {
        request.setUrl(QUrl(m_apiUrl + "?" + QString::fromLatin1(body)));
        reply = m_network.get(request);
    }
    connect(reply, &QNetworkReply::finished, this, [reply, done] {
        reply->deleteLater();
        // Last.fm reports its own errors as JSON, often with an HTTP error
        // status, so a readable JSON body means the service answered.
        const auto reading = QJsonDocument::fromJson(reply->readAll()).object();
        if (!reading.isEmpty())
            done(reading, {});
        else
            done({}, reply->errorString());
    });
}

void Lastfm::setError(const QString &message) {
    if (message == m_error)
        return;
    m_error = message;
    emit changed();
}

// Reply codes from the Last.fm API docs: 9 means the session is no longer
// valid, 11 and 16 are temporary service trouble.
void Lastfm::sessionRejected() {
    m_session.clear();
    m_user.clear();
    saveConfig();
    setError(tr("Last.fm no longer accepts this login. Connect again."));
}

void Lastfm::saveCredentials(const QString &apiKey, const QString &secret) {
    m_apiKey = apiKey.trimmed();
    m_secret = secret.trimmed();
    m_session.clear();
    m_user.clear();
    m_error.clear();
    saveConfig();
    emit changed();
}

void Lastfm::connectAccount() {
    if (!configured() || m_waiting)
        return;
    m_error.clear();
    call("auth.getToken", {}, false, [this](const QJsonObject &reply, const QString &networkError) {
        const QString token = reply.value("token").toString();
        if (token.isEmpty()) {
            setError(!networkError.isEmpty() ? tr("Can't reach Last.fm: %1").arg(networkError)
                                             : tr("Last.fm refused the API key: %1").arg(reply.value("message").toString()));
            return;
        }
        m_token = token;
        m_waiting = true;
        QUrl url("https://www.last.fm/api/auth/");
        QUrlQuery query;
        query.addQueryItem("api_key", m_apiKey);
        query.addQueryItem("token", token);
        url.setQuery(query);
        QDesktopServices::openUrl(url);
        m_poll.start();
        m_deadline.start();
        emit changed();
    });
}

void Lastfm::pollSession() {
    call("auth.getSession", {{"token", m_token}}, false, [this](const QJsonObject &reply, const QString &) {
        if (!m_waiting)
            return;
        const auto session = reply.value("session").toObject();
        if (!session.isEmpty()) {
            m_session = session.value("key").toString();
            m_user = session.value("name").toString();
            stopWaiting();
            m_error.clear();
            saveConfig();
            emit changed();
            return;
        }
        const int code = reply.value("error").toInt();
        // 14 only means the browser step is not finished yet; a dropped
        // connection is just worth another try on the next tick.
        if (code != 0 && code != 14 && code != 11 && code != 16) {
            stopWaiting();
            setError(tr("Last.fm refused the login: %1").arg(reply.value("message").toString()));
        }
    });
}

void Lastfm::stopWaiting() {
    m_poll.stop();
    m_deadline.stop();
    m_token.clear();
    if (m_waiting) {
        m_waiting = false;
        emit changed();
    }
}

void Lastfm::cancelConnect() { stopWaiting(); }

void Lastfm::disconnectAccount() {
    stopWaiting();
    m_session.clear();
    m_user.clear();
    m_queue.clear();
    saveQueue();
    saveConfig();
    m_error.clear();
    emit changed();
}

void Lastfm::nowPlaying(const QString &artist, const QString &track, int durationSeconds) {
    if (!connected())
        return;
    QMap<QString, QString> params{{"artist", artist}, {"track", track}};
    if (durationSeconds > 0)
        params["duration"] = QString::number(durationSeconds);
    // Only a rejected session matters here; a missed update is harmless.
    call("track.updateNowPlaying", params, true, [this](const QJsonObject &reply, const QString &) {
        if (reply.value("error").toInt() == 9)
            sessionRejected();
    });
}

void Lastfm::scrobble(const QString &artist, const QString &track, const QDateTime &started) {
    if (!connected())
        return;
    m_queue.append({artist, track, started.toSecsSinceEpoch()});
    saveQueue();
    emit changed();
    flush();
}

// Scrobbles wait in a file until Last.fm accepts them, so a dropped
// connection loses nothing.
void Lastfm::flush() {
    if (m_flushing || m_queue.isEmpty() || !connected())
        return;
    const int count = qMin(50, int(m_queue.size()));
    QMap<QString, QString> params;
    for (int i = 0; i < count; ++i) {
        const QString n = QString::number(i);
        params["artist[" + n + "]"] = m_queue.at(i).artist;
        params["track[" + n + "]"] = m_queue.at(i).track;
        params["timestamp[" + n + "]"] = QString::number(m_queue.at(i).timestamp);
    }
    m_flushing = true;
    call("track.scrobble", params, true, [this, count](const QJsonObject &reply, const QString &networkError) {
        m_flushing = false;
        const int code = reply.value("error").toInt();
        const bool temporary = !networkError.isEmpty() || code == 11 || code == 16;
        if (code == 9) {
            sessionRejected();
            return;
        }
        if (temporary) {
            setError(tr("Can't reach Last.fm, will retry: %1").arg(networkError.isEmpty() ? reply.value("message").toString() : networkError));
            m_retry.start();
            return;
        }
        // Accepted, or refused for good (bad data would fail forever), so drop the batch.
        m_queue.erase(m_queue.begin(), m_queue.begin() + count);
        saveQueue();
        m_error = code != 0 ? tr("Last.fm refused a scrobble: %1").arg(reply.value("message").toString()) : QString();
        emit changed();
        if (!m_queue.isEmpty())
            flush();
    });
}

void Lastfm::love(const QString &artist, const QString &track, bool loved) {
    if (!connected())
        return;
    call(loved ? "track.love" : "track.unlove", {{"artist", artist}, {"track", track}}, true,
         [this, loved](const QJsonObject &reply, const QString &networkError) {
             const int code = reply.value("error").toInt();
             if (code == 9) {
                 sessionRejected();
             } else if (!networkError.isEmpty() || code != 0) {
                 setError(loved ? tr("Loved here, but not on Last.fm: %1").arg(networkError.isEmpty() ? reply.value("message").toString() : networkError)
                                : tr("Unloved here, but not on Last.fm: %1").arg(networkError.isEmpty() ? reply.value("message").toString() : networkError));
             } else {
                 setError({});
             }
         });
}

void Lastfm::loadConfig() {
    QFile file(m_configFile);
    if (!file.open(QIODevice::ReadOnly))
        return;
    const auto object = QJsonDocument::fromJson(file.readAll()).object();
    m_apiKey = object.value("api_key").toString();
    m_secret = object.value("api_secret").toString();
    m_session = object.value("session_key").toString();
    m_user = object.value("user").toString();
}

// The file holds the API secret and session key, so only its owner may read it.
bool Lastfm::saveConfig() {
    QDir().mkpath(QFileInfo(m_configFile).absolutePath());
    QSaveFile file(m_configFile);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    const QJsonObject object{{"api_key", m_apiKey}, {"api_secret", m_secret}, {"session_key", m_session}, {"user", m_user}};
    file.write(QJsonDocument(object).toJson());
    if (!file.commit()) {
        m_error = tr("Can't save %1: %2").arg(m_configFile, file.errorString());
        return false;
    }
    return true;
}

void Lastfm::loadQueue() {
    QFile file(m_queueFile);
    if (!file.open(QIODevice::ReadOnly))
        return;
    for (const auto &value : QJsonDocument::fromJson(file.readAll()).array()) {
        const auto item = value.toObject();
        m_queue.append({item.value("artist").toString(), item.value("track").toString(),
                        item.value("timestamp").toVariant().toLongLong()});
    }
}

void Lastfm::saveQueue() {
    QJsonArray array;
    for (const auto &item : m_queue)
        array.append(QJsonObject{{"artist", item.artist}, {"track", item.track}, {"timestamp", item.timestamp}});
    QDir().mkpath(QFileInfo(m_queueFile).absolutePath());
    QSaveFile file(m_queueFile);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(array).toJson());
        file.commit();
    }
}
