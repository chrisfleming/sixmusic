#include "mpris.h"

#include "backend.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusMessage>

Mpris::Mpris(Backend *backend, QObject *parent) : QObject(parent), m_backend(backend) {
    new MprisRoot(this);
    auto *player = new MprisPlayer(this);
    Q_UNUSED(player)
}

bool Mpris::registerOnBus() {
    auto bus = QDBusConnection::sessionBus();
    return bus.isConnected() && bus.registerService("org.mpris.MediaPlayer2.sixmusic")
        && bus.registerObject("/org/mpris/MediaPlayer2", this, QDBusConnection::ExportAdaptors);
}

// A live stream has nothing to pause, but "Paused" keeps the widget on screen
// with a working play button, where "Stopped" makes many clients drop it.
QString Mpris::playbackStatus(bool playing) { return playing ? "Playing" : "Paused"; }

QVariantMap Mpris::metadata(const Backend &backend) {
    QVariantMap map{{"mpris:trackid", QVariant::fromValue(QDBusObjectPath("/org/mpris/MediaPlayer2/sixmusic/live"))}};
    if (backend.onMusic()) {
        map["xesam:title"] = backend.track();
        map["xesam:artist"] = QStringList{backend.artist()};
        map["xesam:album"] = backend.showTitle();
        if (!backend.artwork().isEmpty())
            map["mpris:artUrl"] = backend.artwork();
    } else {
        map["xesam:title"] = backend.showTitle().isEmpty() ? QString("BBC Radio 6 Music") : backend.showTitle();
        map["xesam:artist"] = QStringList{"BBC Radio 6 Music"};
        if (!backend.showImage().isEmpty())
            map["mpris:artUrl"] = backend.showImage();
    }
    return map;
}

void Mpris::changed(const QString &interface, const QVariantMap &properties) {
    auto message = QDBusMessage::createSignal("/org/mpris/MediaPlayer2", "org.freedesktop.DBus.Properties",
                                              "PropertiesChanged");
    message << interface << properties << QStringList();
    QDBusConnection::sessionBus().send(message);
}

void MprisRoot::Quit() { QCoreApplication::quit(); }

MprisPlayer::MprisPlayer(Mpris *parent) : QDBusAbstractAdaptor(parent), m_mpris(parent) {
    const QString interface = "org.mpris.MediaPlayer2.Player";
    auto *backend = parent->backend();
    connect(backend, &Backend::playingChanged, this, [this, interface] {
        m_mpris->changed(interface, {{"PlaybackStatus", playbackStatus()}});
    });
    connect(backend, &Backend::volumeChanged, this, [this, interface] {
        m_mpris->changed(interface, {{"Volume", volume()}});
    });
    connect(backend, &Backend::nowChanged, this, [this, interface] {
        m_mpris->changed(interface, {{"Metadata", metadata()}});
    });
}

QString MprisPlayer::playbackStatus() const { return Mpris::playbackStatus(m_mpris->backend()->playing()); }
double MprisPlayer::volume() const { return m_mpris->backend()->muted() ? 0.0 : m_mpris->backend()->volume(); }
void MprisPlayer::setVolume(double volume) { m_mpris->backend()->setVolume(volume); }
QVariantMap MprisPlayer::metadata() const { return Mpris::metadata(*m_mpris->backend()); }

void MprisPlayer::Play() { m_mpris->backend()->play(); }
void MprisPlayer::Pause() { m_mpris->backend()->stop(); }
void MprisPlayer::PlayPause() { m_mpris->backend()->toggle(); }
void MprisPlayer::Stop() { m_mpris->backend()->stop(); }
