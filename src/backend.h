#pragma once

#include <QAudioDevice>
#include <QAudioOutput>
#include <QDateTime>
#include <QMediaDevices>
#include <QMediaPlayer>
#include <QNetworkAccessManager>
#include <QObject>
#include <QTimer>
#include <QVariantList>

// Plays BBC Radio 6 Music and follows what is on air: the current track, the
// recent ones, and the show. Both come from the BBC's public RMS feed.
class Backend : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool playing READ playing NOTIFY playingChanged)
    Q_PROPERTY(bool buffering READ buffering NOTIFY playingChanged)
    Q_PROPERTY(QString streamError READ streamError NOTIFY playingChanged)
    Q_PROPERTY(QString infoError READ infoError NOTIFY infoErrorChanged)
    Q_PROPERTY(double volume READ volume WRITE setVolume NOTIFY volumeChanged)
    Q_PROPERTY(bool muted READ muted WRITE setMuted NOTIFY volumeChanged)
    Q_PROPERTY(QVariantList outputs READ outputs NOTIFY outputsChanged)
    Q_PROPERTY(int outputIndex READ outputIndex NOTIFY outputsChanged)
    Q_PROPERTY(bool onMusic READ onMusic NOTIFY nowChanged)
    Q_PROPERTY(QString artist READ artist NOTIFY nowChanged)
    Q_PROPERTY(QString track READ track NOTIFY nowChanged)
    Q_PROPERTY(QString artwork READ artwork NOTIFY nowChanged)
    Q_PROPERTY(QVariantList recent READ recent NOTIFY nowChanged)
    Q_PROPERTY(QString showTitle READ showTitle NOTIFY nowChanged)
    Q_PROPERTY(QString showSubtitle READ showSubtitle NOTIFY nowChanged)
    Q_PROPERTY(QString showImage READ showImage NOTIFY nowChanged)
    Q_PROPERTY(QDateTime showStart READ showStart NOTIFY nowChanged)
    Q_PROPERTY(QDateTime showEnd READ showEnd NOTIFY nowChanged)

public:
    explicit Backend(const QString &segmentsUrl = {}, const QString &broadcastUrl = {}, QObject *parent = nullptr);

    bool playing() const { return m_player.playbackState() == QMediaPlayer::PlayingState; }
    bool buffering() const;
    QString streamError() const { return m_streamError; }
    QString infoError() const { return m_infoError; }
    double volume() const { return m_output.volume(); }
    bool muted() const { return m_output.isMuted(); }
    // The first entry is always the system default, with an empty id.
    QVariantList outputs() const;
    int outputIndex() const;
    bool onMusic() const { return m_onMusic; }
    QString artist() const { return m_artist; }
    QString track() const { return m_track; }
    QString artwork() const { return m_artwork; }
    QVariantList recent() const { return m_recent; }
    QString showTitle() const { return m_showTitle; }
    QString showSubtitle() const { return m_showSubtitle; }
    QString showImage() const { return m_showImage; }
    QDateTime showStart() const { return m_showStart; }
    QDateTime showEnd() const { return m_showEnd; }

    void setVolume(double volume);
    void setMuted(bool muted);

    Q_INVOKABLE void toggle();
    Q_INVOKABLE void play();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void nudgeVolume(double delta);
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void chooseOutput(int index);
    void setOutput(const QString &id);

    // Parsing is separate from the network so tests can feed it saved replies.
    // Both return false when the reply is not what the BBC documents.
    bool applySegments(const QByteArray &json);
    bool applyBroadcast(const QByteArray &json);
    static QString artworkUrl(QString url, int size);

signals:
    void playingChanged();
    void infoErrorChanged();
    void volumeChanged();
    void nowChanged();
    void outputsChanged();

private:
    void applyOutput();

    void fetch(const QString &url, bool (Backend::*apply)(const QByteArray &));
    void setInfoError(const QString &message);

    QMediaPlayer m_player;
    QAudioOutput m_output;
    QMediaDevices m_devices;
    QString m_outputId;
    QNetworkAccessManager m_network;
    QTimer m_poll;
    QString m_segmentsUrl, m_broadcastUrl;
    QString m_streamError, m_infoError;
    bool m_onMusic = false;
    QString m_artist, m_track, m_artwork;
    QVariantList m_recent;
    QString m_showTitle, m_showSubtitle, m_showImage;
    QDateTime m_showStart, m_showEnd;
};
