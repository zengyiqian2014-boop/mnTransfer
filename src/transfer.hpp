#pragma once
#include "crypto.hpp"
#include <QWebSocket>
#include <QFile>
#include <QCryptographicHash>
#include <QJsonObject>
#include <QTimer>
#include <functional>

class Transfer : public QObject {
    Q_OBJECT
public:
    explicit Transfer(QObject *parent=nullptr);
    ~Transfer() override;
    void sendFile(const QString &path,const QUrl &relay);
    void receiveFile(const QString &code,const QString &folder,bool autoAccept=false);
    void acceptOffer(bool accept);
    void cancel();
    QString pairingCode() const{return pair.code();}
signals:
    void status(const QString &message);
    void codeReady(const QString &code);
    void offered(const QString &name,qint64 bytes);
    void progress(qint64 bytes,qint64 total);
    void completed(const QString &path);
    void failed(const QString &message);
private:
    enum class Phase {Idle,Pairing,Header,Offer,Ready,Data,Finish,Done,Error};
    Phase phase=Phase::Idle;
    QWebSocket socket;
    QTimer reconnect;
    Crypto::Pair pair;
    QByteArray challenge,replyKey,sha;
    crypto_secretstream_xchacha20poly1305_state stream{};
    QFile file;
    QCryptographicHash digest{QCryptographicHash::Sha256};
    QString source,folder,name,partPath,target;
    qint64 total=0,sent=0,confirmed=0,received=0,lastAck=0;
    bool sending=false,automatic=false,reset=false,stopping=false,finalSent=false;
    unsigned epoch=0;
    void connectRelay();
    void text(const QString &message);
    void binary(const QByteArray &message);
    void fail(const QString &message);
    void push(const QByteArray &plain,bool last=false);
    void reply(const QJsonObject &message);
    void pump();
    void beginReceive();
    void handleReply(const QJsonObject &message);
    void hashFile(const QString &path,qint64 limit,std::function<void(QByteArray)> done,bool keepDigest=false);
};
