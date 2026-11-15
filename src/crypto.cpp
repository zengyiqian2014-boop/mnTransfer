#include "crypto.hpp"
#include <stdexcept>

namespace Crypto {
QByteArray random(int count) {QByteArray b(count,Qt::Uninitialized);randombytes_buf(b.data(),size_t(count));return b;}
QByteArray derive(const QByteArray &secret,const QByteArray &label) {
    if(secret.size()!=32)throw std::runtime_error("Invalid pairing key");
    QByteArray b(32,Qt::Uninitialized);
    crypto_generichash(reinterpret_cast<unsigned char *>(b.data()),32,
        reinterpret_cast<const unsigned char *>(label.data()),size_t(label.size()),
        reinterpret_cast<const unsigned char *>(secret.data()),32);return b;
}
QString encode(const QByteArray &b){return QString::fromLatin1(b.toBase64(QByteArray::Base64UrlEncoding|QByteArray::OmitTrailingEquals));}
QByteArray decode(const QString &s) {
    auto r=QByteArray::fromBase64Encoding(s.toLatin1(),QByteArray::Base64UrlEncoding|QByteArray::AbortOnBase64DecodingErrors);
    if(!r)throw std::runtime_error("Invalid pairing code encoding");
    return r.decoded;
}
bool relayAllowed(const QUrl &u) {
    return u.isValid()&&u.userInfo().isEmpty()&&!u.hasQuery()&&!u.hasFragment()&&
        (u.path().isEmpty()||u.path()=="/")&&!u.host().isEmpty()&&
        (u.scheme()=="wss"||(u.scheme()=="ws"&&(u.host()=="127.0.0.1"||u.host()=="::1"||u.host()=="localhost")));
}
QString Pair::room() const {return QString::fromLatin1(derive(secret,"mnTransfer/v1/room").toHex());}
QString Pair::code() const {return "mn1."+encode(relay.toString(QUrl::FullyEncoded).toUtf8())+"."+encode(secret);}
Pair Pair::parse(const QString &code) {
    auto p=code.trimmed().split('.');if(p.size()!=3||p[0]!="mn1"||code.size()>2048)throw std::runtime_error("Invalid pairing code");
    Pair r{QUrl(QString::fromUtf8(decode(p[1]))),decode(p[2])};
    if(!relayAllowed(r.relay)||r.secret.size()!=32)throw std::runtime_error("Pairing code requires a valid WSS relay and 256-bit key");
    return r;
}
QByteArray seal(const QByteArray &key,const QByteArray &plain) {
    QByteArray out=random(24);out.resize(24+plain.size()+16);unsigned long long size=0;
    crypto_aead_xchacha20poly1305_ietf_encrypt(reinterpret_cast<unsigned char *>(out.data()+24),&size,
        reinterpret_cast<const unsigned char *>(plain.data()),size_t(plain.size()),nullptr,0,nullptr,
        reinterpret_cast<const unsigned char *>(out.data()),reinterpret_cast<const unsigned char *>(key.data()));
    return out;
}
QByteArray open(const QByteArray &key,const QByteArray &cipher) {
    if(key.size()!=32||cipher.size()<40)throw std::runtime_error("Invalid authenticated response");
    QByteArray out(cipher.size()-40,Qt::Uninitialized);unsigned long long size=0;
    if(crypto_aead_xchacha20poly1305_ietf_decrypt(reinterpret_cast<unsigned char *>(out.data()),&size,nullptr,
        reinterpret_cast<const unsigned char *>(cipher.data()+24),size_t(cipher.size()-24),nullptr,0,
        reinterpret_cast<const unsigned char *>(cipher.data()),reinterpret_cast<const unsigned char *>(key.data())))
        throw std::runtime_error("Pairing key mismatch or altered response");
    out.resize(qsizetype(size));return out;
}
}
