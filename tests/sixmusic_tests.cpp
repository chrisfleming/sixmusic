#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QCryptographicHash>
#include <QDesktopServices>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrl>
#include <QUrlQuery>
#include <QtTest>

#include "backend.h"
#include "lastfm.h"
#include "mpris.h"
#include "theme.h"
#include "translator.h"

// Shaped like the replies from rms.api.bbc.co.uk, with made-up content.
static const char *kSegments = R"json({"data":[
  {"segment_type":"music","titles":{"primary":"Some Band","secondary":"First Song"},
   "image_url":"img.test/{recipe}/a.jpg","offset":{"label":"Now Playing","now_playing":true}},
  {"segment_type":"music","titles":{"primary":"Other Act","secondary":"Earlier Song"},
   "image_url":"img.test/{recipe}/b.jpg","offset":{"label":"3 Minutes Ago","now_playing":false}},
  {"segment_type":"speech","titles":{"primary":"Chat","secondary":null},
   "offset":{"label":"5 Minutes Ago","now_playing":false}}
]})json";

static const char *kSpeech = R"json({"data":[
  {"segment_type":"music","titles":{"primary":"Other Act","secondary":"Earlier Song"},
   "offset":{"label":"1 Minute Ago","now_playing":false}}
]})json";

static const char *kBroadcast = R"json({"data":[{"start":"2026-11-15T13:00:00Z","end":"2026-11-15T16:00:00Z",
  "programme":{"titles":{"primary":"The Show","secondary":"With a Guest"},
  "images":[{"url":"img.test/{recipe}/show.jpg"}]}}]})json";

// A stand-in for ws.audioscrobbler.com: records each request and answers with
// the next canned reply (the last one repeats).
class FakeLastfm : public QObject {
    Q_OBJECT
public:
    FakeLastfm() {
        QVERIFY2(m_server.listen(QHostAddress::LocalHost), "fake server");
        connect(&m_server, &QTcpServer::newConnection, this, [this] {
            auto *socket = m_server.nextPendingConnection();
            connect(socket, &QTcpSocket::readyRead, this, [this, socket] { handle(socket); });
        });
    }
    QString url() const { return QString("http://127.0.0.1:%1/").arg(m_server.serverPort()); }
    QList<QByteArray> requests;       // request line for GET, body for POST
    QList<QByteArray> replies;

private:
    void handle(QTcpSocket *socket) {
        m_buffer[socket] += socket->readAll();
        const QByteArray &data = m_buffer[socket];
        const int end = data.indexOf("\r\n\r\n");
        if (end < 0)
            return;
        int length = 0;
        for (const auto &line : data.left(end).split('\n'))
            if (line.toLower().startsWith("content-length:"))
                length = line.mid(15).trimmed().toInt();
        if (data.size() < end + 4 + length)
            return;
        const bool post = data.startsWith("POST");
        requests.append(post ? data.mid(end + 4, length) : data.left(data.indexOf("\r\n")));
        m_buffer.remove(socket);
        const QByteArray body = replies.isEmpty() ? QByteArray("{}") : replies.size() > 1 ? replies.takeFirst() : replies.first();
        socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: " + QByteArray::number(body.size())
                      + "\r\nConnection: close\r\n\r\n" + body);
        socket->disconnectFromHost();
    }
    QTcpServer m_server;
    QHash<QTcpSocket *, QByteArray> m_buffer;
};

// Catches the browser URL the connect step opens.
class UrlCatcher : public QObject {
    Q_OBJECT
public slots:
    void open(const QUrl &url) { opened.append(url); }
public:
    QList<QUrl> opened;
};

static QMap<QString, QString> decodeForm(const QByteArray &body) {
    QMap<QString, QString> map;
    for (const auto &pair : body.split('&')) {
        const auto parts = pair.split('=');
        map[QUrl::fromPercentEncoding(parts.value(0))] = QUrl::fromPercentEncoding(parts.value(1));
    }
    return map;
}

static void writeFile(const QString &path, const QByteArray &contents) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(contents);
}

class SixMusicTests : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        // Volume is remembered with QSettings; keep tests out of the real one.
        QVERIFY(m_config.isValid());
        qputenv("XDG_CONFIG_HOME", m_config.path().toUtf8());
        qputenv("XDG_DATA_HOME", m_config.path().toUtf8());
    }

    void readsNowPlayingAndRecent()
    {
        Backend backend("http://127.0.0.1:1/s", "http://127.0.0.1:1/b");
        QVERIFY(backend.applySegments(kSegments));
        QVERIFY(backend.onMusic());
        QCOMPARE(backend.artist(), QString("Some Band"));
        QCOMPARE(backend.track(), QString("First Song"));
        QCOMPARE(backend.artwork(), QString("img.test/320x320/a.jpg"));
        // Speech segments are not tracks, so only the earlier song is listed.
        QCOMPARE(backend.recent().size(), 1);
        QCOMPARE(backend.recent().first().toMap().value("track").toString(), QString("Earlier Song"));
    }

    void noTrackWhileSpeechIsOn()
    {
        Backend backend("http://127.0.0.1:1/s", "http://127.0.0.1:1/b");
        QVERIFY(backend.applySegments(kSegments));
        QVERIFY(backend.applySegments(kSpeech));
        QVERIFY(!backend.onMusic());
        QVERIFY(backend.track().isEmpty());
    }

    void readsTheShow()
    {
        Backend backend("http://127.0.0.1:1/s", "http://127.0.0.1:1/b");
        QSignalSpy spy(&backend, &Backend::nowChanged);
        QVERIFY(backend.applyBroadcast(kBroadcast));
        QCOMPARE(backend.showTitle(), QString("The Show"));
        QCOMPARE(backend.showSubtitle(), QString("With a Guest"));
        QCOMPARE(backend.showImage(), QString("img.test/320x320/show.jpg"));
        QCOMPARE(backend.showStart().toUTC(), QDateTime(QDate(2026, 11, 15), QTime(13, 0), QTimeZone::UTC));
        QCOMPARE(spy.count(), 1);
        // The same reply again changes nothing, so QML does not re-render.
        QVERIFY(backend.applyBroadcast(kBroadcast));
        QCOMPARE(spy.count(), 1);
    }

    void rejectsUnexpectedReplies()
    {
        Backend backend("http://127.0.0.1:1/s", "http://127.0.0.1:1/b");
        QVERIFY(!backend.applySegments("<html>nope</html>"));
        QVERIFY(!backend.applyBroadcast("{}"));
    }

    void volumeIsClamped()
    {
        Backend backend("http://127.0.0.1:1/s", "http://127.0.0.1:1/b");
        backend.setVolume(2);
        QCOMPARE(backend.volume(), 1.0);
        backend.setVolume(-1);
        QCOMPARE(backend.volume(), 0.0);
        backend.nudgeVolume(0.05);
        QVERIFY(qAbs(backend.volume() - 0.05) < 1e-6);
    }

    void mprisDescribesTheTrackAndTheShow()
    {
        Backend backend("http://127.0.0.1:1/s", "http://127.0.0.1:1/b");
        QVERIFY(backend.applyBroadcast(kBroadcast));
        QVERIFY(backend.applySegments(kSegments));
        auto meta = Mpris::metadata(backend);
        QCOMPARE(meta.value("xesam:title").toString(), QString("First Song"));
        QCOMPARE(meta.value("xesam:artist").toStringList(), QStringList{"Some Band"});
        QCOMPARE(meta.value("xesam:album").toString(), QString("The Show"));
        QCOMPARE(meta.value("mpris:artUrl").toString(), QString("img.test/320x320/a.jpg"));

        QVERIFY(backend.applySegments(kSpeech));
        meta = Mpris::metadata(backend);
        QCOMPARE(meta.value("xesam:title").toString(), QString("The Show"));
        QCOMPARE(meta.value("mpris:artUrl").toString(), QString("img.test/320x320/show.jpg"));
        QCOMPARE(Mpris::playbackStatus(false), QString("Paused"));
        QCOMPARE(Mpris::playbackStatus(true), QString("Playing"));
    }

    void lovingATrackSavesAndTogglesOff()
    {
        QTemporaryDir dir;
        const QString file = dir.filePath("nested/loved.json");
        {
            Backend backend("http://127.0.0.1:1/s", "http://127.0.0.1:1/b", file);
            QVERIFY(!backend.toggleCurrentLoved()); // nothing on air yet
            QVERIFY(backend.applyBroadcast(kBroadcast));
            QVERIFY(backend.applySegments(kSegments));
            QVERIFY(!backend.currentLoved());
            QVERIFY(backend.toggleCurrentLoved());
            QVERIFY(backend.currentLoved());
            QCOMPARE(backend.lovedTracks().size(), 1);
            QCOMPARE(backend.lovedTracks().first().toMap().value("show").toString(), QString("The Show"));
            // Case and spacing do not make a second entry.
            backend.toggleLoved(" some band", "FIRST SONG ");
            QVERIFY(!backend.currentLoved());
            backend.toggleLoved("Some Band", "First Song");
            backend.toggleLoved("Other Act", "Earlier Song");
        }
        // A new run reads the log back, newest first.
        Backend again("http://127.0.0.1:1/s", "http://127.0.0.1:1/b", file);
        const auto loved = again.lovedTracks();
        QCOMPARE(loved.size(), 2);
        QCOMPARE(loved.first().toMap().value("track").toString(), QString("Earlier Song"));
        QVERIFY(again.isLoved("some band", "first song"));
        QVERIFY(again.lovedError().isEmpty());
    }

    void loveStateFollowsTheTrackOnAir()
    {
        QTemporaryDir dir;
        Backend backend("http://127.0.0.1:1/s", "http://127.0.0.1:1/b", dir.filePath("loved.json"));
        backend.toggleLoved("Some Band", "First Song");
        QSignalSpy spy(&backend, &Backend::lovedChanged);
        // The heart binds to currentLoved, so a new track must announce it.
        QVERIFY(backend.applySegments(kSegments));
        QVERIFY(backend.currentLoved());
        QVERIFY(spy.count() >= 1);
        QVERIFY(backend.applySegments(kSpeech));
        QVERIFY(!backend.currentLoved());
    }

    void unreadableLogIsNeverOverwritten()
    {
        QTemporaryDir dir;
        const QString file = dir.filePath("loved.json");
        write(file, "this is not json");
        Backend backend("http://127.0.0.1:1/s", "http://127.0.0.1:1/b", file);
        QVERIFY(!backend.lovedError().isEmpty());
        backend.toggleLoved("A", "B");
        QVERIFY(backend.lovedTracks().isEmpty());
        QFile log(file);
        QVERIFY(log.open(QIODevice::ReadOnly));
        QCOMPARE(log.readAll(), QByteArray("this is not json"));
    }

    void scrobbleThresholdFollowsLastfmRules()
    {
        QCOMPARE(Backend::scrobbleThreshold(0), 240);    // length unknown
        QCOMPARE(Backend::scrobbleThreshold(30), -1);    // too short to count
        QCOMPARE(Backend::scrobbleThreshold(31), 15);
        QCOMPARE(Backend::scrobbleThreshold(200), 100);  // half the track
        QCOMPARE(Backend::scrobbleThreshold(600), 240);  // capped at four minutes
    }

    void lastfmSignsSortedParamsWithoutFormat()
    {
        const QMap<QString, QString> params{{"track", "T"}, {"artist", "A"}, {"format", "json"}, {"callback", "x"}};
        const QString expected = QString::fromLatin1(
            QCryptographicHash::hash("artistAtrackTsecret", QCryptographicHash::Md5).toHex());
        QCOMPARE(Lastfm::sign(params, "secret"), expected);
    }

    void lastfmKeepsCredentialsPrivate()
    {
        QTemporaryDir dir;
        Lastfm lastfm(dir.filePath("c/lastfm.json"), dir.filePath("q.json"), "http://127.0.0.1:1/");
        QVERIFY(!lastfm.configured());
        lastfm.saveCredentials(" key ", " secret ");
        QVERIFY(lastfm.configured());
        QVERIFY(!lastfm.connected());
        const auto permissions = QFile::permissions(dir.filePath("c/lastfm.json"));
        QVERIFY(permissions & QFile::ReadOwner);
        QVERIFY(!(permissions & (QFile::ReadGroup | QFile::ReadOther | QFile::WriteGroup | QFile::WriteOther)));
        Lastfm reloaded(dir.filePath("c/lastfm.json"), dir.filePath("q.json"), "http://127.0.0.1:1/");
        QVERIFY(reloaded.configured());
    }

    void lastfmIgnoresScrobblesWhenNotConnected()
    {
        QTemporaryDir dir;
        Lastfm lastfm(dir.filePath("c.json"), dir.filePath("q.json"), "http://127.0.0.1:1/");
        lastfm.scrobble("A", "B", QDateTime::currentDateTime());
        QCOMPARE(lastfm.pending(), 0);
    }

    void lastfmSendsASignedScrobble()
    {
        QTemporaryDir dir;
        FakeLastfm server;
        server.replies = {R"json({"scrobbles":{"@attr":{"accepted":1,"ignored":0}}})json"};
        writeFile(dir.filePath("c.json"), R"json({"api_key":"k","api_secret":"s","session_key":"sk","user":"me"})json");
        Lastfm lastfm(dir.filePath("c.json"), dir.filePath("q.json"), server.url());
        QVERIFY(lastfm.connected());

        // A "+" and "&" in a title must arrive intact, not as a space or a split.
        lastfm.scrobble("Sly & Robbie", "A+B", QDateTime::fromSecsSinceEpoch(1700000000));
        QTRY_COMPARE(server.requests.size(), 1);
        auto form = decodeForm(server.requests.first());
        QCOMPARE(form.value("method"), QString("track.scrobble"));
        QCOMPARE(form.value("artist[0]"), QString("Sly & Robbie"));
        QCOMPARE(form.value("track[0]"), QString("A+B"));
        QCOMPARE(form.value("timestamp[0]"), QString("1700000000"));
        QCOMPARE(form.value("sk"), QString("sk"));
        const QString sent = form.take("api_sig");
        QCOMPARE(sent, Lastfm::sign(form, "s"));
        QTRY_COMPARE(lastfm.pending(), 0);
    }

    void lastfmKeepsScrobblesUntilItCanSendThem()
    {
        QTemporaryDir dir;
        FakeLastfm server;
        server.replies = {R"json({"error":11,"message":"Service offline"})json"};
        writeFile(dir.filePath("c.json"), R"json({"api_key":"k","api_secret":"s","session_key":"sk","user":"me"})json");
        {
            Lastfm lastfm(dir.filePath("c.json"), dir.filePath("q.json"), server.url());
            lastfm.scrobble("A", "B", QDateTime::currentDateTime());
            QTRY_VERIFY(!lastfm.error().isEmpty());
            QCOMPARE(lastfm.pending(), 1);
        }
        // A new run finds the queue on disk and sends it when Last.fm is back.
        server.replies = {R"json({"scrobbles":{}})json"};
        Lastfm again(dir.filePath("c.json"), dir.filePath("q.json"), server.url());
        QCOMPARE(again.pending(), 1);
        QTRY_COMPARE_WITH_TIMEOUT(again.pending(), 0, 8000);
    }

    void lastfmForgetsALoginItNoLongerTrusts()
    {
        QTemporaryDir dir;
        FakeLastfm server;
        server.replies = {R"json({"error":9,"message":"Invalid session key"})json"};
        writeFile(dir.filePath("c.json"), R"json({"api_key":"k","api_secret":"s","session_key":"old","user":"me"})json");
        Lastfm lastfm(dir.filePath("c.json"), dir.filePath("q.json"), server.url());
        lastfm.love("A", "B", true);
        QTRY_VERIFY(!lastfm.connected());
        QVERIFY(lastfm.configured());
        QVERIFY(!lastfm.error().isEmpty());
    }

    void lastfmConnectsAfterBrowserApproval()
    {
        QTemporaryDir dir;
        FakeLastfm server;
        // Token, then "not approved yet", then the session.
        server.replies = {R"json({"token":"tok"})json", R"json({"error":14,"message":"not authorised"})json",
                          R"json({"session":{"name":"chris","key":"newkey"}})json"};
        writeFile(dir.filePath("c.json"), R"json({"api_key":"k","api_secret":"s"})json");
        UrlCatcher catcher;
        QDesktopServices::setUrlHandler("https", &catcher, "open");

        Lastfm lastfm(dir.filePath("c.json"), dir.filePath("q.json"), server.url());
        lastfm.setPollInterval(100);
        lastfm.connectAccount();
        QTRY_VERIFY(lastfm.waiting());
        QCOMPARE(catcher.opened.size(), 1);
        QVERIFY(catcher.opened.first().toString().contains("api_key=k"));
        QVERIFY(catcher.opened.first().toString().contains("token=tok"));
        QTRY_VERIFY(lastfm.connected());
        QCOMPARE(lastfm.user(), QString("chris"));
        QVERIFY(!lastfm.waiting());
        QDesktopServices::unsetUrlHandler("https");

        Lastfm reloaded(dir.filePath("c.json"), dir.filePath("q.json"), server.url());
        QVERIFY(reloaded.connected());
    }

    void lovingATrackAlsoLovesItOnLastfm()
    {
        QTemporaryDir dir;
        FakeLastfm server;
        server.replies = {"{}"};
        writeFile(dir.filePath("c.json"), R"json({"api_key":"k","api_secret":"s","session_key":"sk","user":"me"})json");
        Lastfm lastfm(dir.filePath("c.json"), dir.filePath("q.json"), server.url());
        lastfm.love("A", "B", true);
        lastfm.love("A", "B", false);
        QTRY_COMPARE(server.requests.size(), 2);
        QCOMPARE(decodeForm(server.requests.at(0)).value("method"), QString("track.love"));
        QCOMPARE(decodeForm(server.requests.at(1)).value("method"), QString("track.unlove"));
    }

    void outputListStartsWithSystemDefault()
    {
        Backend backend("http://127.0.0.1:1/s", "http://127.0.0.1:1/b");
        const auto list = backend.outputs();
        QVERIFY(!list.isEmpty());
        QVERIFY(list.first().toMap().value("id").toString().isEmpty());
        QCOMPARE(backend.outputIndex(), 0);
    }

    void missingOutputFallsBackToDefault()
    {
        Backend backend("http://127.0.0.1:1/s", "http://127.0.0.1:1/b");
        backend.setOutput("no-such-speakers");
        QCOMPARE(backend.outputIndex(), 0);
        backend.chooseOutput(99); // out of range is ignored
        backend.chooseOutput(0);
        QCOMPARE(backend.outputIndex(), 0);
    }

    void reportsUnreachableInfoService()
    {
        Backend backend("http://127.0.0.1:1/s", "http://127.0.0.1:1/b");
        QTRY_VERIFY_WITH_TIMEOUT(!backend.infoError().isEmpty(), 5000);
    }

    void themeFallsBack()
    {
        QTemporaryDir dir;
        Theme theme(dir.path());
        QCOMPARE(theme.accent(), QString("#FFD60A"));
        QCOMPARE(theme.accentForeground(), QString("black"));
    }

    void themeReadsAccent()
    {
        QTemporaryDir dir;
        QDir(dir.path()).mkpath("theme");
        QFile colors(dir.path() + "/theme/colors.toml");
        QVERIFY(colors.open(QIODevice::WriteOnly));
        colors.write("accent = \"#112233\"\n");
        colors.close();

        Theme theme(dir.path());
        QCOMPARE(theme.accent(), QString("#112233"));
        QCOMPARE(theme.accentForeground(), QString("white"));
    }

    void themeFollowsHyprlandRounding()
    {
        QTemporaryDir dir;
        const QString response = dir.filePath("response.json"), command = dir.filePath("hyprctl");
        auto write = [](const QString &path, const QByteArray &data) {
            QFile file(path);
            return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
        };
        QVERIFY(write(command, "#!/bin/sh\ncat '" + response.toUtf8() + "'\n"));
        QVERIFY(QFile::setPermissions(command, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        QVERIFY(write(response, "{\"int\":0}"));

        Theme theme(dir.path(), nullptr, command);
        QCOMPARE(theme.radius(), 0);
        QVERIFY(write(response, "{\"int\":6}"));
        QTRY_COMPARE(theme.radius(), 6);
    }

    void translatorFallsBackToEnglish()
    {
        QTemporaryDir dir;
        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.language(), QString("en"));
        QVERIFY(translator.translate("Main", "Play").isNull());
    }

    void translatorFollowsTheLanguageFile()
    {
        QTemporaryDir dir;
        write(dir.path() + "/language", "pt-BR\n");
        write(dir.path() + "/pt.json", R"({"Play": "Reproduzir", "Stop": "Parar"})");
        write(dir.path() + "/pt-BR.json", R"({"Play": "Tocar"})");

        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.language(), QString("pt-BR"));
        QCOMPARE(translator.translate("Main", "Play"), QString("Tocar"));
        QCOMPARE(translator.translate("Main", "Stop"), QString("Parar"));
        QVERIFY(translator.translate("Main", "Quit").isNull());
    }

    void translatorPicksPluralForms()
    {
        QTemporaryDir dir;
        write(dir.path() + "/language", "ru\n");
        write(dir.path() + "/en.json", R"({"%n tracks": {"one": "%n track", "other": "%n tracks"}})");
        write(dir.path() + "/ru.json", R"({"%n tracks": {"one": "%n трек", "few": "%n трека", "many": "%n треков", "other": "%n трека"}})");

        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.translate("Main", "%n tracks", nullptr, 21), QString("%n трек"));
        QCOMPARE(translator.translate("Main", "%n tracks", nullptr, 25), QString("%n треков"));
    }

private:
    QTemporaryDir m_config;

    static void write(const QString &path, const QByteArray &contents)
    {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(contents);
    }
};

QTEST_MAIN(SixMusicTests)
#include "sixmusic_tests.moc"
