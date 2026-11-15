#pragma once
#include <QWebSocketServer>
#include <QHash>
#include <QPointer>
#include <QWebSocket>

// Reference relay for local tests or a VPS behind an HTTPS reverse proxy.
class Relay : public QObject {
    Q_OBJECT
public:
    explicit Relay(QObject *parent=nullptr);
    ~Relay() override;
    bool listen(const QHostAddress &address=QHostAddress::LocalHost,quint16 port=0);
    quint16 port() const{return server.serverPort();}
    void disconnectPeers();
private:
    struct Room {QPointer<QWebSocket> sender,receiver;};
    QWebSocketServer server;
    QHash<QString,Room> rooms;
    void accept();
};
