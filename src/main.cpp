#include "window.hpp"
#include "relay.hpp"
#include <QApplication>
#include <QCommandLineParser>
#include <QFile>
#include <QTimer>
#include <QTextStream>
#include <memory>

int main(int argc,char **argv) {
    bool preview=argc==3&&QString::fromLocal8Bit(argv[1])=="--screenshot";
    bool gui=argc==1||preview;
    std::unique_ptr<QCoreApplication> app(gui?static_cast<QCoreApplication *>(new QApplication(argc,argv)):new QCoreApplication(argc,argv));
    QCoreApplication::setOrganizationName("mnOS");QCoreApplication::setApplicationName("mnTransfer");QCoreApplication::setApplicationVersion("0.1.0");
    if(gui){Window window;window.show();if(preview)QTimer::singleShot(500,app.get(),[&]{app->exit(window.grab().save(QString::fromLocal8Bit(argv[2]))?0:1);});return app->exec();}
    QCommandLineParser p;p.setApplicationDescription("C++ encrypted file transfer. Public peers require a WSS relay.");p.addHelpOption();p.addVersionOption();
    p.addOption({"send","File to send","file"});p.addOption({"relay","Relay WSS URL (or --serve bind address)","url"});
    p.addOption({"receive","Pairing code. Prefer --code-file to keep it out of process arguments.","code"});
    p.addOption({"code-file","Write sender code or read receiver code from this file","path"});
    p.addOption({"out","Receiver destination directory","directory"});p.addOption({"serve","Run reference relay on loopback"});p.addOption({"port","Reference relay port","port","8787"});
    p.addOption({"bind","Reference relay bind address (TLS reverse proxy required for public use)","address","127.0.0.1"});
    p.addOption({"timeout","Exit unsuccessfully after this many seconds (0 disables)","seconds","0"});p.process(*app);
    if(p.isSet("serve")){Relay relay;bool ok=false;auto port=p.value("port").toUShort(&ok);QHostAddress bind(p.value("bind"));if(!ok||bind.isNull()||!relay.listen(bind,port))return 1;
        QTextStream(stdout)<<"Relay listening on "<<bind.toString()<<":"<<relay.port()<<Qt::endl;return app->exec();}
    Transfer transfer;
    QObject::connect(&transfer,&Transfer::status,[](const QString &s){QTextStream(stderr)<<s<<Qt::endl;});
    QObject::connect(&transfer,&Transfer::codeReady,[&](const QString &s){
        if(p.isSet("code-file")){QFile f(p.value("code-file"));if(!f.open(QIODevice::WriteOnly|QIODevice::NewOnly)){QTextStream(stderr)<<"Cannot create new code file"<<Qt::endl;app->exit(1);return;}
            f.setPermissions(QFileDevice::ReadOwner|QFileDevice::WriteOwner);f.write(s.toUtf8());f.write("\n");f.close();}
        else QTextStream(stdout)<<s<<Qt::endl;
    });
    QObject::connect(&transfer,&Transfer::completed,[&](const QString &s){QTextStream(stdout)<<"VERIFIED "<<s<<Qt::endl;app->exit(0);});
    QObject::connect(&transfer,&Transfer::failed,[&](const QString &s){QTextStream(stderr)<<s<<Qt::endl;app->exit(1);});
    bool ok=false;auto timeout=p.value("timeout").toInt(&ok);if(!ok||timeout<0||timeout>604800)return 2;
    if(timeout)QTimer::singleShot(timeout*1000,app.get(),[&]{QTextStream(stderr)<<"Transfer timed out"<<Qt::endl;app->exit(1);});
    QTimer::singleShot(0,app.get(),[&]{
        if(p.isSet("send")){transfer.sendFile(p.value("send"),QUrl(p.value("relay")));return;}
        QString code=p.value("receive");if(code.isEmpty()&&p.isSet("code-file")){QFile f(p.value("code-file"));if(f.open(QIODevice::ReadOnly)&&f.size()<=2048)code=QString::fromUtf8(f.readAll()).trimmed();}
        if(code.isEmpty()||!p.isSet("out")){QTextStream(stderr)<<"Use --send FILE --relay WSS or --code-file CODE --out DIR"<<Qt::endl;app->exit(2);return;}
        transfer.receiveFile(code,p.value("out"),true);
    });return app->exec();
}
