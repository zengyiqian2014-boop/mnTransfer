#include "transfer.hpp"
#include "relay.hpp"
#include <QtTest>
#include <QTemporaryDir>
#include <QDir>
#include <QJsonDocument>
#include <QSignalSpy>

class TransferTests : public QObject {
    Q_OBJECT
private slots:
    void crypto() {
        QVERIFY(sodium_init()>=0);auto key=Crypto::random(32);auto secret=QByteArray("private\0file",12);
        auto cipher=Crypto::seal(key,secret);QCOMPARE(Crypto::open(key,cipher),secret);QVERIFY(!cipher.contains(secret));
        cipher[30]^=1;QVERIFY_EXCEPTION_THROWN(Crypto::open(key,cipher),std::runtime_error);
        QVERIFY_EXCEPTION_THROWN(Crypto::open(Crypto::random(32),Crypto::seal(key,secret)),std::runtime_error);
        Crypto::Pair p{QUrl("wss://relay.example.com"),key};QCOMPARE(Crypto::Pair::parse(p.code()).secret,key);
        QVERIFY_EXCEPTION_THROWN(Crypto::Pair::parse("mn1.bad.bad"),std::runtime_error);
        QVERIFY(!Crypto::relayAllowed(QUrl("ws://public.example.com")));
        QVERIFY(!Crypto::relayAllowed(QUrl("wss://user:password@relay.example.com")));
        QVERIFY(!Crypto::relayAllowed(QUrl("wss://relay.example.com/path")));
    }
    void files_data() {
        QTest::addColumn<int>("size");QTest::addColumn<int>("partial");QTest::addColumn<bool>("corrupt");
        QTest::newRow("empty")<<0<<0<<false;
        QTest::newRow("binary")<<4099<<0<<false;
        QTest::newRow("window-and-unaligned-tail")<<(5*1024*1024+53)<<0<<false;
        QTest::newRow("resume")<<(3*1024*1024+77)<<1048593<<false;
        QTest::newRow("corrupt-prefix-restarts")<<(3*1024*1024+77)<<1048593<<true;
    }
    void files() {
        QFETCH(int,size);QFETCH(int,partial);QFETCH(bool,corrupt);
        QTemporaryDir work;QVERIFY(work.isValid());QDir(work.path()).mkdir("out");const auto out=work.path()+"/out";
        auto bytes=Crypto::random(size);const auto source=work.path()+"/镜像 with space.iso";
        QFile f(source);QVERIFY(f.open(QIODevice::WriteOnly));QCOMPARE(f.write(bytes),qint64(size));f.close();
        auto sha=QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex();
        if(partial){QFile part(out+"/.mntransfer-"+sha+".part");QVERIFY(part.open(QIODevice::WriteOnly));auto data=bytes.first(partial);if(corrupt)data[0]^=1;QCOMPARE(part.write(data),qint64(partial));}
        // A receiver must never overwrite a preexisting unrelated file.
        QFile old(out+"/镜像 with space.iso");QVERIFY(old.open(QIODevice::WriteOnly));old.write("keep me");old.close();
        Relay relay;QVERIFY(relay.listen());Transfer sender,receiver;
        QSignalSpy sd(&sender,&Transfer::completed),rd(&receiver,&Transfer::completed),se(&sender,&Transfer::failed),re(&receiver,&Transfer::failed);
        connect(&sender,&Transfer::codeReady,&receiver,[&](const QString &c){receiver.receiveFile(c,out,true);});
        sender.sendFile(source,QUrl("ws://127.0.0.1:"+QString::number(relay.port())));
        QTRY_VERIFY_WITH_TIMEOUT(!rd.isEmpty()||!se.isEmpty()||!re.isEmpty(),20000);
        QVERIFY2(se.isEmpty(),se.isEmpty()?"":qPrintable(se.first().first().toString()));
        QVERIFY2(re.isEmpty(),re.isEmpty()?"":qPrintable(re.first().first().toString()));
        QTRY_COMPARE_WITH_TIMEOUT(sd.size(),1,5000);QCOMPARE(rd.size(),1);
        QFile received(rd.first().first().toString());QVERIFY(received.open(QIODevice::ReadOnly));QCOMPARE(received.readAll(),bytes);
        QVERIFY(old.open(QIODevice::ReadOnly));QCOMPARE(old.readAll(),QByteArray("keep me"));
    }
    void reconnect() {
        QTemporaryDir work;QVERIFY(work.isValid());QDir(work.path()).mkdir("out");
        auto bytes=Crypto::random(8*1024*1024+21);QString path=work.path()+"/image.iso",out=work.path()+"/out";
        QFile file(path);QVERIFY(file.open(QIODevice::WriteOnly));file.write(bytes);file.close();
        Relay relay;QVERIFY(relay.listen());Transfer sender,receiver;QString code;bool interrupted=false;
        QSignalSpy done(&receiver,&Transfer::completed),error(&receiver,&Transfer::failed);
        connect(&sender,&Transfer::codeReady,&receiver,[&](const QString &s){code=s;receiver.receiveFile(s,out,true);});
        connect(&receiver,&Transfer::progress,&receiver,[&](qint64 count,qint64){if(count>=1024*1024&&!interrupted){interrupted=true;relay.disconnectPeers();}});
        sender.sendFile(path,QUrl("ws://127.0.0.1:"+QString::number(relay.port())));
        QTRY_VERIFY_WITH_TIMEOUT(!done.isEmpty()||!error.isEmpty(),30000);
        QVERIFY(interrupted);QVERIFY(error.isEmpty());QCOMPARE(done.size(),1);
        QFile result(done.first().first().toString());QVERIFY(result.open(QIODevice::ReadOnly));QCOMPARE(result.readAll(),bytes);
    }
    void maliciousFilename() {
        QTemporaryDir directory;Relay relay;QVERIFY(relay.listen());
        Crypto::Pair pair{QUrl("ws://127.0.0.1:"+QString::number(relay.port())),Crypto::random(32)};
        QWebSocket attacker;Transfer receiver;QSignalSpy errors(&receiver,&Transfer::failed);QSignalSpy offers(&receiver,&Transfer::offered);
        connect(&attacker,&QWebSocket::textMessageReceived,&attacker,[&](const QString &s){
            auto o=QJsonDocument::fromJson(s.toUtf8()).object();if(!o.contains("challenge"))return;
            auto challenge=Crypto::decode(o["challenge"].toString());auto key=Crypto::derive(pair.secret,"mnTransfer/v1/send"+challenge);
            crypto_secretstream_xchacha20poly1305_state stream{};QByteArray header(24,Qt::Uninitialized);
            crypto_secretstream_xchacha20poly1305_init_push(&stream,reinterpret_cast<unsigned char *>(header.data()),reinterpret_cast<const unsigned char *>(key.data()));
            attacker.sendTextMessage(QString::fromUtf8(QJsonDocument(QJsonObject{{"header",Crypto::encode(header)},{"challenge",Crypto::encode(challenge)}}).toJson(QJsonDocument::Compact)));
            QByteArray metadata='M'+QJsonDocument(QJsonObject{{"name","../escape.iso"},{"size","0"},{"sha256",QString(64,'0')},{"challenge",Crypto::encode(challenge)}}).toJson(QJsonDocument::Compact);
            QByteArray cipher(metadata.size()+17,Qt::Uninitialized);unsigned long long size=0;
            crypto_secretstream_xchacha20poly1305_push(&stream,reinterpret_cast<unsigned char *>(cipher.data()),&size,reinterpret_cast<const unsigned char *>(metadata.data()),size_t(metadata.size()),nullptr,0,0);
            attacker.sendBinaryMessage(cipher);
        });
        QUrl endpoint=pair.relay;endpoint.setPath("/v1/"+pair.room()+"/send");attacker.open(endpoint);
        receiver.receiveFile(pair.code(),directory.path(),true);QTRY_COMPARE_WITH_TIMEOUT(errors.size(),1,5000);QCOMPARE(offers.size(),0);
        QCOMPARE(QDir(directory.path()).entryList(QDir::Files|QDir::Hidden).size(),0);
    }
};
QTEST_GUILESS_MAIN(TransferTests)
#include "transfer_tests.moc"
