#pragma once

#include <QDBusAbstractAdaptor>
#include <QDBusObjectPath>
#include <QObject>
#include <QStringList>
#include <QVariantMap>

class Backend;

// Publishes the player on the session bus as MPRIS (org.mpris.MediaPlayer2),
// which is what media keys, playerctl, and desktop now-playing widgets talk to.
class Mpris : public QObject {
    Q_OBJECT

public:
    explicit Mpris(Backend *backend, QObject *parent = nullptr);

    // Claims org.mpris.MediaPlayer2.sixmusic. False when there is no session bus.
    bool registerOnBus();

    static QString playbackStatus(bool playing);
    static QVariantMap metadata(const Backend &backend);

    Backend *backend() const { return m_backend; }
    void changed(const QString &interface, const QVariantMap &properties);

private:
    Backend *m_backend;
};

class MprisRoot : public QDBusAbstractAdaptor {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2")
    Q_PROPERTY(bool CanQuit READ canQuit)
    Q_PROPERTY(bool CanRaise READ canRaise)
    Q_PROPERTY(bool HasTrackList READ hasTrackList)
    Q_PROPERTY(QString Identity READ identity)
    Q_PROPERTY(QString DesktopEntry READ desktopEntry)
    Q_PROPERTY(QStringList SupportedUriSchemes READ none)
    Q_PROPERTY(QStringList SupportedMimeTypes READ none)

public:
    explicit MprisRoot(Mpris *parent) : QDBusAbstractAdaptor(parent) {}
    bool canQuit() const { return true; }
    bool canRaise() const { return false; }
    bool hasTrackList() const { return false; }
    QString identity() const { return "6 Music"; }
    QString desktopEntry() const { return "sixmusic"; }
    QStringList none() const { return {}; }

public slots:
    void Quit();
    void Raise() {}
};

class MprisPlayer : public QDBusAbstractAdaptor {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2.Player")
    Q_PROPERTY(QString PlaybackStatus READ playbackStatus)
    Q_PROPERTY(double Rate READ one WRITE ignore)
    Q_PROPERTY(double MinimumRate READ one)
    Q_PROPERTY(double MaximumRate READ one)
    Q_PROPERTY(double Volume READ volume WRITE setVolume)
    Q_PROPERTY(qlonglong Position READ position)
    Q_PROPERTY(QVariantMap Metadata READ metadata)
    Q_PROPERTY(bool CanGoNext READ no)
    Q_PROPERTY(bool CanGoPrevious READ no)
    Q_PROPERTY(bool CanPlay READ yes)
    Q_PROPERTY(bool CanPause READ yes)
    Q_PROPERTY(bool CanSeek READ no)
    Q_PROPERTY(bool CanControl READ yes)

public:
    explicit MprisPlayer(Mpris *parent);
    QString playbackStatus() const;
    double one() const { return 1.0; }
    void ignore(double) {}
    double volume() const;
    void setVolume(double volume);
    qlonglong position() const { return 0; }
    QVariantMap metadata() const;
    bool yes() const { return true; }
    bool no() const { return false; }

public slots:
    void Play();
    void Pause();
    void PlayPause();
    void Stop();
    void Next() {}
    void Previous() {}
    void Seek(qlonglong) {}
    void SetPosition(const QDBusObjectPath &, qlonglong) {}
    void OpenUri(const QString &) {}

private:
    Mpris *m_mpris;
};
