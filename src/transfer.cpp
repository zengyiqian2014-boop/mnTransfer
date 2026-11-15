#include "transfer.hpp"
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QtEndian>
#include <memory>
#include <stdexcept>

namespace {
QByteArray json(const QJsonObject &o){return QJsonDocument(o).toJson(QJsonDocument::Compact);}
QJsonObject object(const QByteArray &b){QJsonParseError e;auto d=QJsonDocument::fromJson(b,&e);if(e.error||!d.isObject())throw std::runtime_error("Invalid protocol message");return d.object();}
qint64 integer(const QJsonObject &o,const char *key){bool ok=false;auto v=o[key].toString().toLongLong(&ok);if(!ok||v<0)throw std::runtime_error("Invalid byte count");return v;}
void ensure(bool ok,const char *text){if(!ok)throw std::runtime_error(text);}
bool safeName(const QString &s) {
    static QRegularExpression reserved("^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\\.|$)",QRegularExpression::CaseInsensitiveOption);
    if(s.isEmpty()||s=="."||s==".."||s.toUtf8().size()>240||s.endsWith('.')||s.endsWith(' ')||reserved.match(s).hasMatch())return false;
    for(auto c:s)if(c.unicode()<32||QString("/\\:*?\"<>|").contains(c))return false;
    return true;
}
}
Transfer::Transfer(QObject *p):QObject(p) {
    ensure(sodium_init()>=0,"Secure random initialization failed");
    reconnect.setSingleShot(true);reconnect.setInterval(2000);
    connect(&reconnect,&QTimer::timeout,this,&Transfer::connectRelay);
    socket.setMaxAllowedIncomingMessageSize(131072);socket.setMaxAllowedIncomingFrameSize(131072);
    connect(&socket,&QWebSocket::textMessageReceived,this,[this](const QString &s){try{text(s);}catch(const std::exception &e){fail(QString::fromUtf8(e.what()));}});
    connect(&socket,&QWebSocket::binaryMessageReceived,this,[this](const QByteArray &s){try{binary(s);}catch(const std::exception &e){fail(QString::fromUtf8(e.what()));}});
    connect(&socket,&QWebSocket::bytesWritten,this,[this](qint64){if(sending&&phase==Phase::Data)pump();});
    connect(&socket,&QWebSocket::disconnected,this,[this]{
        if(phase==Phase::Done&&!sending){emit completed(target);return;}
        if(stopping||phase==Phase::Done||phase==Phase::Error||phase==Phase::Idle)return;
        ++epoch;file.flush();file.close();phase=Phase::Pairing;emit status("连接中断，保留断点并重连 / Reconnecting");reconnect.start();
    });
#if QT_VERSION >= QT_VERSION_CHECK(6,5,0)
    connect(&socket,&QWebSocket::errorOccurred,this,[this](QAbstractSocket::SocketError){
#else
    connect(&socket,QOverload<QAbstractSocket::SocketError>::of(&QWebSocket::error),this,[this](QAbstractSocket::SocketError){
#endif
        if(stopping||phase==Phase::Done||phase==Phase::Error)return;
        emit status("连接错误 / "+socket.errorString());if(socket.state()==QAbstractSocket::UnconnectedState)reconnect.start();
    });
}
Transfer::~Transfer(){cancel();sodium_memzero(stream.k,sizeof stream.k);if(!pair.secret.isEmpty())sodium_memzero(pair.secret.data(),size_t(pair.secret.size()));}
void Transfer::cancel(){stopping=true;++epoch;reconnect.stop();socket.abort();file.close();phase=Phase::Idle;}
void Transfer::fail(const QString &message){if(phase==Phase::Error)return;phase=Phase::Error;stopping=true;++epoch;reconnect.stop();socket.abort();file.close();emit failed(message);}
void Transfer::hashFile(const QString &path,qint64 limit,std::function<void(QByteArray)> done,bool keepDigest) {
    struct Job {QFile input;QCryptographicHash hash{QCryptographicHash::Sha256};qint64 left;Job(const QString &p,qint64 l):input(p),left(l){}};
    auto job=std::make_shared<Job>(path,limit);if(!job->input.open(QIODevice::ReadOnly)){fail("无法读取文件 / "+job->input.errorString());return;}
    if(keepDigest)digest.reset();
    auto *timer=new QTimer(this);const auto generation=epoch;
    connect(timer,&QTimer::timeout,this,[this,timer,job,done=std::move(done),generation,keepDigest]{
        if(generation!=epoch){timer->stop();timer->deleteLater();return;}
        if(job->left) {
            auto chunk=job->input.read(qMin<qint64>(4*1024*1024,job->left));
            if(chunk.isEmpty()){timer->stop();timer->deleteLater();fail("文件读取失败或大小改变 / File changed while hashing");return;}
            job->hash.addData(chunk);if(keepDigest)digest.addData(chunk);job->left-=chunk.size();
        }
        if(!job->left){timer->stop();timer->deleteLater();done(job->hash.result());}
    });timer->start(0);
}
void Transfer::sendFile(const QString &path,const QUrl &relay) {
    cancel();stopping=false;sending=true;source=QFileInfo(path).absoluteFilePath();total=QFileInfo(source).size();name=QFileInfo(source).fileName();
    if(!Crypto::relayAllowed(relay)||!QFileInfo(source).isFile()||!safeName(name)){fail("请选择有效文件和 wss:// 中继地址 / Invalid file or relay");return;}
    pair={relay,Crypto::random(32)};phase=Phase::Pairing;emit status("正在计算 SHA-256 / Hashing");
    hashFile(source,total,[this](QByteArray result){
        if(QFileInfo(source).size()!=total){fail("源文件在校验时改变 / Source file changed");return;}
        sha=result;emit codeReady(pair.code());connectRelay();
    });
}
void Transfer::receiveFile(const QString &code,const QString &directory,bool autoAccept) {
    cancel();stopping=false;sending=false;automatic=autoAccept;folder=QFileInfo(directory).absoluteFilePath();
    try {pair=Crypto::Pair::parse(code);ensure(QFileInfo(folder).isDir(),"Choose an existing destination directory");phase=Phase::Pairing;connectRelay();}
    catch(const std::exception &e){fail(QString::fromUtf8(e.what()));}
}
void Transfer::connectRelay() {
    if(stopping)return;
    phase=Phase::Pairing;QUrl u=pair.relay;u.setPath("/v1/"+pair.room()+(sending?"/send":"/receive"));
    emit status("等待另一端 / Waiting for peer");socket.open(u);
}
void Transfer::text(const QString &s) {
    ensure(s.size()<=4096,"Oversized protocol message");auto o=object(s.toUtf8());
    if(o["event"]=="waiting")return;
    if(o["event"]=="paired") {
        ensure(phase==Phase::Pairing,"Unexpected pair event");reset=false;finalSent=false;
        if(!sending){challenge=Crypto::random(32);replyKey=Crypto::derive(pair.secret,"mnTransfer/v1/reply"+challenge);phase=Phase::Header;
            socket.sendTextMessage(QString::fromUtf8(json({{"challenge",Crypto::encode(challenge)}})));}
        return;
    }
    if(sending&&o.contains("challenge")&&!o.contains("header")) {
        ensure(phase==Phase::Pairing,"Unexpected receiver challenge");challenge=Crypto::decode(o["challenge"].toString());ensure(challenge.size()==32,"Invalid receiver challenge");
        auto key=Crypto::derive(pair.secret,"mnTransfer/v1/send"+challenge);replyKey=Crypto::derive(pair.secret,"mnTransfer/v1/reply"+challenge);
        QByteArray header(crypto_secretstream_xchacha20poly1305_HEADERBYTES,Qt::Uninitialized);
        crypto_secretstream_xchacha20poly1305_init_push(&stream,reinterpret_cast<unsigned char *>(header.data()),reinterpret_cast<const unsigned char *>(key.data()));
        socket.sendTextMessage(QString::fromUtf8(json({{"header",Crypto::encode(header)},{"challenge",Crypto::encode(challenge)}})));
        phase=Phase::Ready;push('M'+json({{"name",name},{"size",QString::number(total)},{"sha256",QString::fromLatin1(sha.toHex())},{"challenge",Crypto::encode(challenge)}}));return;
    }
    if(!sending&&o.contains("header")) {
        ensure(phase==Phase::Header,"Unexpected stream header");auto h=Crypto::decode(o["header"].toString());
        ensure(Crypto::decode(o["challenge"].toString())==challenge&&h.size()==crypto_secretstream_xchacha20poly1305_HEADERBYTES,"Invalid session header");
        auto key=Crypto::derive(pair.secret,"mnTransfer/v1/send"+challenge);
        ensure(crypto_secretstream_xchacha20poly1305_init_pull(&stream,reinterpret_cast<const unsigned char *>(h.data()),reinterpret_cast<const unsigned char *>(key.data()))==0,"Invalid encrypted stream");
        phase=Phase::Offer;return;
    }
    throw std::runtime_error("Unexpected relay message");
}
void Transfer::push(const QByteArray &plain,bool last) {
    QByteArray b(plain.size()+crypto_secretstream_xchacha20poly1305_ABYTES,Qt::Uninitialized);unsigned long long n=0;
    crypto_secretstream_xchacha20poly1305_push(&stream,reinterpret_cast<unsigned char *>(b.data()),&n,
        reinterpret_cast<const unsigned char *>(plain.data()),size_t(plain.size()),nullptr,0,
        last?crypto_secretstream_xchacha20poly1305_TAG_FINAL:crypto_secretstream_xchacha20poly1305_TAG_MESSAGE);
    socket.sendBinaryMessage(b);
}
void Transfer::reply(const QJsonObject &o){socket.sendBinaryMessage(Crypto::seal(replyKey,json(o)));}
void Transfer::binary(const QByteArray &b) {
    ensure(b.size()<=131072,"Oversized encrypted frame");
    if(sending){ensure(phase==Phase::Ready||phase==Phase::Data||phase==Phase::Finish,"Unexpected response");handleReply(object(Crypto::open(replyKey,b)));return;}
    ensure(phase==Phase::Offer||phase==Phase::Data,"Unexpected stream data");
    ensure(b.size()>=crypto_secretstream_xchacha20poly1305_ABYTES,"Truncated encrypted frame");
    QByteArray p(b.size()-crypto_secretstream_xchacha20poly1305_ABYTES,Qt::Uninitialized);unsigned long long length=0;unsigned char tag=0;
    ensure(crypto_secretstream_xchacha20poly1305_pull(&stream,reinterpret_cast<unsigned char *>(p.data()),&length,&tag,
        reinterpret_cast<const unsigned char *>(b.data()),size_t(b.size()),nullptr,0)==0,"Wrong pairing key or altered file stream");
    p.resize(qsizetype(length));ensure(!p.isEmpty(),"Empty stream record");
    if(p[0]=='M') {
        ensure(phase==Phase::Offer&&tag==0,"Unexpected file offer");auto o=object(p.mid(1));name=o["name"].toString();total=integer(o,"size");
        auto hash=o["sha256"].toString();ensure(QRegularExpression("^[0-9a-f]{64}$").match(hash).hasMatch(),"Invalid SHA-256");sha=QByteArray::fromHex(hash.toLatin1());
        ensure(safeName(name)&&Crypto::decode(o["challenge"].toString())==challenge,"Invalid filename or replayed offer");
        phase=Phase::Ready;emit offered(name,total);if(automatic)acceptOffer(true);return;
    }
    ensure(phase==Phase::Data,"File was not accepted");
    if(p[0]=='R') {
        ensure(!reset&&received==lastAck&&tag==0,"Invalid resume reset");reset=true;ensure(file.resize(0)&&file.seek(0),"Cannot reset partial file");
        received=lastAck=0;digest.reset();reply({{"kind","ready"},{"bytes","0"},{"sha256",QString::fromLatin1(digest.result().toHex())}});return;
    }
    if(p[0]=='D') {
        ensure(p.size()>9&&p.size()<=65545&&tag==0,"Invalid data record");auto offset=qFromBigEndian<quint64>(reinterpret_cast<const uchar *>(p.constData()+1));
        ensure(offset==quint64(received)&&p.size()-9<=total-received,"Out-of-order file bytes");auto data=p.mid(9);
        ensure(file.write(data)==data.size(),"Destination write failed (check free disk space)");digest.addData(data);received+=data.size();
        if(received-lastAck>=512*1024||received==total){ensure(file.flush(),"Destination flush failed");lastAck=received;reply({{"kind","ack"},{"bytes",QString::number(received)}});emit progress(received,total);}return;
    }
    if(p[0]=='F') {
        ensure(p.size()==1&&tag==crypto_secretstream_xchacha20poly1305_TAG_FINAL&&received==total,"Premature transfer completion");
        ensure(digest.result()==sha,"Final SHA-256 mismatch; partial file retained");ensure(file.flush(),"Destination flush failed");file.close();
        target=QDir(folder).filePath(name);
        for(unsigned i=1;QFileInfo::exists(target)&&i<10000;i++)target=QDir(folder).filePath(name+" ("+QString::number(i)+")");
        ensure(QFile::rename(partPath,target),"Cannot finalize download; partial file retained");
        phase=Phase::Done;reply({{"kind","done"},{"bytes",QString::number(total)}});emit progress(total,total);
        // Finish the WebSocket close handshake before telling a CLI receiver
        // to exit. Otherwise its queued authenticated receipt can be dropped.
        socket.close();return;
    }
    throw std::runtime_error("Unknown file record");
}
void Transfer::acceptOffer(bool accepted) {
    if(sending||phase!=Phase::Ready)return;
    if(!accepted){fail("接收已取消 / Receive cancelled");return;}
    try {beginReceive();}catch(const std::exception &e){fail(QString::fromUtf8(e.what()));}
}
void Transfer::beginReceive() {
    partPath=QDir(folder).filePath(".mntransfer-"+QString::fromLatin1(sha.toHex())+".part");
    ensure(!QFileInfo(partPath).isSymLink(),"Refusing a symbolic-link partial file");
    file.setFileName(partPath);ensure(file.open(QIODevice::ReadWrite),"Cannot open partial file");
    file.setPermissions(QFileDevice::ReadOwner|QFileDevice::WriteOwner);
    received=file.size();ensure(received<=total,"Partial file is larger than the offered file");lastAck=received;
    emit status("检查已有断点 / Verifying partial file");
    hashFile(partPath,received,[this](QByteArray result){
        if(!file.seek(received)){fail("Cannot seek partial file");return;}
        phase=Phase::Data;reply({{"kind","ready"},{"bytes",QString::number(received)},{"sha256",QString::fromLatin1(result.toHex())}});
        emit status("接收中 / Receiving");emit progress(received,total);
    },true);
}
void Transfer::handleReply(const QJsonObject &o) {
    const auto kind=o["kind"].toString();auto count=integer(o,"bytes");
    if(kind=="ready") {
        ensure(phase==Phase::Ready&&count<=total,"Invalid resume offset");auto expected=o["sha256"].toString();
        phase=Phase::Header;emit status("核对断点 / Checking resume offset");
        hashFile(source,count,[this,count,expected](QByteArray result){
            if(QString::fromLatin1(result.toHex())!=expected){
                if(reset){fail("Resume verification failed");return;}reset=true;phase=Phase::Ready;push("R");return;
            }
            file.close();file.setFileName(source);
            if(!file.open(QIODevice::ReadOnly)||file.size()!=total||!file.seek(count)){fail("源文件发生变化或无法打开 / Source changed");return;}
            sent=confirmed=count;phase=Phase::Data;emit status("发送中 / Sending");emit progress(confirmed,total);pump();
        });return;
    }
    if(kind=="ack") {
        ensure(phase==Phase::Data&&count<=sent,"Invalid file acknowledgement");
        if(count>confirmed){confirmed=count;emit progress(confirmed,total);}pump();return;
    }
    if(kind=="done") {
        ensure(phase==Phase::Finish&&count==total,"Invalid completion acknowledgement");
        phase=Phase::Done;file.close();emit progress(total,total);emit completed(source);socket.close();return;
    }
    throw std::runtime_error("Unexpected acknowledgement");
}
void Transfer::pump() {
    if(!sending||phase!=Phase::Data||finalSent)return;
    while(sent<total&&sent-confirmed<1024*1024&&socket.bytesToWrite()<2*1024*1024) {
        auto b=file.read(qMin<qint64>(65536,total-sent));if(b.isEmpty()){fail("Source read failed");return;}
        QByteArray record(9,Qt::Uninitialized);record[0]='D';qToBigEndian<quint64>(quint64(sent),reinterpret_cast<uchar *>(record.data()+1));record+=b;
        sent+=b.size();push(record);
    }
    if(sent==total&&confirmed==total){finalSent=true;phase=Phase::Finish;push("F",true);}
}
