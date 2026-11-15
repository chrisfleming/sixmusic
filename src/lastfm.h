#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QNetworkAccessManager>
#include <QObject>
#include <QTimer>
#include <functional>

// Scrobbling, now-playing updates, and loves for Last.fm. Each user brings
// their own API key and shared secret (last.fm/api/account/create) and
// approves access in the browser, so no Last.fm password is ever typed here.
class Lastfm : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool configured READ configured NOTIFY changed)
    Q_PROPERTY(bool connected READ connected NOTIFY changed)
    Q_PROPERTY(bool waiting READ waiting NOTIFY changed)
    Q_PROPERTY(QString user READ user NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(int pending READ pending NOTIFY changed)

public:
    explicit Lastfm(const QString &configFile = {}, const QString &queueFile = {}, const QString &apiUrl = {},
                    QObject *parent = nullptr);

    bool configured() const { return !m_apiKey.isEmpty() && !m_secret.isEmpty(); }
    bool connected() const { return configured() && !m_session.isEmpty(); }
    bool waiting() const { return m_waiting; }
    QString user() const { return m_user; }
    QString error() const { return m_error; }
    int pending() const { return m_queue.size(); }

    Q_INVOKABLE void saveCredentials(const QString &apiKey, const QString &secret);
    Q_INVOKABLE void connectAccount();
    Q_INVOKABLE void cancelConnect();
    Q_INVOKABLE void disconnectAccount();

    // All three do nothing unless connected.
    void nowPlaying(const QString &artist, const QString &track, int durationSeconds);
    void scrobble(const QString &artist, const QString &track, const QDateTime &started);
    void love(const QString &artist, const QString &track, bool loved);

    // Tests shorten the wait between checks for browser approval.
    void setPollInterval(int milliseconds) { m_poll.setInterval(milliseconds); }

    // md5 over the sorted name+value pairs plus the secret, without format and callback.
    static QString sign(const QMap<QString, QString> &params, const QString &secret);

signals:
    void changed();

private:
    struct Scrobble { QString artist, track; qint64 timestamp; };
    // done gets the parsed reply, or an empty object when the network failed.
    using Done = std::function<void(const QJsonObject &reply, const QString &networkError)>;

    void call(const QString &method, QMap<QString, QString> params, bool post, Done done);
    void flush();
    void pollSession();
    void stopWaiting();
    void setError(const QString &message);
    void loadConfig();
    bool saveConfig();
    void loadQueue();
    void saveQueue();
    void sessionRejected();

    QNetworkAccessManager m_network;
    QString m_configFile, m_queueFile, m_apiUrl;
    QString m_apiKey, m_secret, m_session, m_user, m_error, m_token;
    bool m_waiting = false, m_flushing = false;
    QList<Scrobble> m_queue;
    QTimer m_retry, m_poll, m_deadline;
};
