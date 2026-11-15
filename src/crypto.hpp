#pragma once
#include <QByteArray>
#include <QString>
#include <QUrl>
#include <sodium.h>

namespace Crypto {
QByteArray random(int count);
QByteArray derive(const QByteArray &secret,const QByteArray &label);
QString encode(const QByteArray &bytes);
QByteArray decode(const QString &text);
bool relayAllowed(const QUrl &url);
struct Pair {
    QUrl relay;
    QByteArray secret;
    QString room() const;
    QString code() const;
    static Pair parse(const QString &code);
};
QByteArray seal(const QByteArray &key,const QByteArray &plain);
QByteArray open(const QByteArray &key,const QByteArray &cipher);
}
