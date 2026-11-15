#include "relay.hpp"
#include <QRegularExpression>
#include <QTimer>

Relay::Relay(QObject *p):QObject(p),server("mnTransfer",QWebSocketServer::NonSecureMode,this) {
    connect(&server,&QWebSocketServer::newConnection,this,&Relay::accept);
}
bool Relay::listen(const QHostAddress &a,quint16 p){return server.listen(a,p);}
Relay::~Relay() {
    // Closing sockets emits disconnected synchronously. Remove callbacks before
    // member destruction; they must never access a destroyed or invalidated map.
    auto active=rooms;rooms.clear();
    for(const auto &r:active)for(auto s:{r.sender,r.receiver})if(s){s->disconnect(this);s->abort();s->deleteLater();}
    server.close();
}
void Relay::disconnectPeers(){auto active=rooms;for(const auto &r:active){if(r.sender)r.sender->close();if(r.receiver)r.receiver->close();}}
void Relay::accept() {
    auto *s=server.nextPendingConnection();
    static QRegularExpression pattern("^/v1/([0-9a-f]{64})/(send|receive)$");
    auto m=pattern.match(s->requestUrl().path());
    if(!m.hasMatch()||rooms.size()>1024){s->close(QWebSocketProtocol::CloseCodePolicyViolated,"Invalid room");s->deleteLater();return;}
    const auto id=m.captured(1);const bool sender=m.captured(2)=="send";
    auto &room=rooms[id];auto &slot=sender?room.sender:room.receiver;
    if(slot){s->close(QWebSocketProtocol::CloseCodePolicyViolated,"Role already connected");s->deleteLater();return;}
    slot=s;s->setMaxAllowedIncomingMessageSize(131072);s->setMaxAllowedIncomingFrameSize(131072);
    connect(s,&QWebSocket::binaryMessageReceived,this,[this,id,s,sender](const QByteArray &b){
        auto r=rooms.value(id);auto peer=sender?r.receiver:r.sender;
        if(peer&&peer->bytesToWrite()<2*1024*1024)peer->sendBinaryMessage(b);
        else if(peer)s->close(QWebSocketProtocol::CloseCodePolicyViolated,"Receiver backpressure");
    });
    connect(s,&QWebSocket::textMessageReceived,this,[this,id,sender](const QString &b){
        auto r=rooms.value(id);auto peer=sender?r.receiver:r.sender;
        if(peer&&b.size()<=4096)peer->sendTextMessage(b);
    });
    connect(s,&QWebSocket::disconnected,this,[this,id,s,sender]{
        auto it=rooms.find(id);if(it!=rooms.end()){
            auto &slot=sender?it->sender:it->receiver;if(slot==s)slot=nullptr;
            auto peer=sender?it->receiver:it->sender;
            // Erasing or closing a peer can recursively emit disconnected.
            // Do not retain an iterator across that callback.
            if(!it->sender&&!it->receiver)rooms.erase(it);
            if(peer)peer->close(QWebSocketProtocol::CloseCodeGoingAway,"Peer disconnected");
        }s->deleteLater();
    });
    QTimer::singleShot(24*60*60*1000,s,[s]{s->close(QWebSocketProtocol::CloseCodeGoingAway,"Room expired");});
    s->sendTextMessage("{\"event\":\"waiting\"}");
    if(room.sender&&room.receiver){room.sender->sendTextMessage("{\"event\":\"paired\"}");room.receiver->sendTextMessage("{\"event\":\"paired\"}");}
}
