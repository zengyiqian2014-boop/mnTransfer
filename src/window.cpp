#include "window.hpp"
#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QDir>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTabBar>
#include <QVBoxLayout>

Window::Window() {
    setWindowTitle("mnTransfer");resize(760,650);setMinimumSize(620,560);
    auto *root=new QWidget;setCentralWidget(root);auto *layout=new QVBoxLayout(root);layout->setContentsMargins(32,26,32,26);layout->setSpacing(14);
    auto *title=new QLabel("mnTransfer");title->setStyleSheet("font-size:28px;font-weight:600;color:#eef4f6");layout->addWidget(title);
    auto *subtitle=new QLabel("公网配对 · 断点续传 · SHA-256 校验");subtitle->setStyleSheet("color:#9daebc");layout->addWidget(subtitle);
    auto *tabs=new QTabWidget;layout->addWidget(tabs);
    auto *send=new QWidget;auto *sl=new QVBoxLayout(send);sl->setSpacing(12);sl->setContentsMargins(18,18,18,18);
    auto *relay=new QLineEdit(QSettings().value("relay").toString());relay->setPlaceholderText("wss://你的中继域名（首次设置）");
    sl->addWidget(new QLabel("中继地址 / Relay"));sl->addWidget(relay);
    auto *choose=new QPushButton("选择文件并生成配对码 / Send a file");sl->addWidget(choose);
    auto *code=new QPlainTextEdit;code->setReadOnly(true);code->setPlaceholderText("配对码包含接收密钥，只分享给接收者。");code->setMaximumHeight(108);sl->addWidget(code);
    auto *copy=new QPushButton("复制配对码 / Copy code");sl->addWidget(copy);sl->addStretch();tabs->addTab(send,"发送 / Send");
    auto *receive=new QWidget;auto *rl=new QVBoxLayout(receive);rl->setSpacing(12);rl->setContentsMargins(18,18,18,18);
    auto *input=new QPlainTextEdit;input->setPlaceholderText("粘贴发送方给你的完整配对码");input->setMaximumHeight(108);rl->addWidget(input);
    auto *folder=new QLineEdit(QStandardPaths::writableLocation(QStandardPaths::DownloadLocation));rl->addWidget(new QLabel("保存位置 / Save to"));rl->addWidget(folder);
    auto *browse=new QPushButton("选择保存文件夹");rl->addWidget(browse);
    auto *begin=new QPushButton("连接并接收 / Receive");rl->addWidget(begin);rl->addStretch();tabs->addTab(receive,"接收 / Receive");
    auto *state=new QLabel("选择发送文件，或粘贴配对码开始接收。");state->setWordWrap(true);layout->addWidget(state);
    auto *bar=new QProgressBar;bar->setRange(0,1000);bar->setValue(0);bar->setTextVisible(false);layout->addWidget(bar);
    auto *amount=new QLabel("0 B");amount->setStyleSheet("color:#9daebc");layout->addWidget(amount);
    auto *cancel=new QPushButton("停止 / Stop");layout->addWidget(cancel);
    auto *note=new QLabel("文件通过中继传输，内容在客户端加密。双方需要保持在线。\n公网模式需要可用中继，速度取决于两端网络和中继带宽。");note->setWordWrap(true);note->setStyleSheet("font-size:12px;color:#8a9cab");layout->addWidget(note);
    setStyleSheet("QMainWindow,QWidget{background:#131c24;color:#dce5ed;font-size:14px;} QLineEdit,QPlainTextEdit{background:#0b131b;border:1px solid #344555;border-radius:6px;padding:9px;selection-background-color:#336f72;} QPushButton{background:#254d53;border:1px solid #3d6870;border-radius:6px;padding:10px;} QPushButton:hover{background:#32646b;} QPushButton:disabled{color:#697984;background:#1c2c34;} QTabWidget::pane{border:1px solid #344555;border-radius:6px;} QTabBar::tab{padding:10px 25px;background:#1b2c39;} QTabBar::tab:selected{background:#2a4655;} QProgressBar{border:0;background:#243441;height:8px;border-radius:4px;} QProgressBar::chunk{background:#70c8b6;border-radius:4px;}");
    auto busy=[choose,begin,relay,tabs](bool on){choose->setEnabled(!on);begin->setEnabled(!on);relay->setEnabled(!on);tabs->tabBar()->setEnabled(!on);};
    connect(choose,&QPushButton::clicked,this,[this,relay,code,bar,busy]{
        auto path=QFileDialog::getOpenFileName(this,"选择发送文件");if(path.isEmpty())return;
        auto address=QUrl(relay->text().trimmed());if(!Crypto::relayAllowed(address)){QMessageBox::warning(this,"中继未设置","请填写已部署中继的 wss:// 地址。HTTPS 证书必须有效。");return;}
        QSettings().setValue("relay",address.toString());code->clear();bar->setValue(0);busy(true);transfer.sendFile(path,address);
    });
    connect(copy,&QPushButton::clicked,this,[code]{if(!code->toPlainText().isEmpty())QApplication::clipboard()->setText(code->toPlainText());});
    connect(browse,&QPushButton::clicked,this,[this,folder]{auto p=QFileDialog::getExistingDirectory(this,"保存位置",folder->text());if(!p.isEmpty())folder->setText(p);});
    connect(begin,&QPushButton::clicked,this,[this,input,folder,bar,busy]{bar->setValue(0);busy(true);transfer.receiveFile(input->toPlainText(),folder->text());});
    connect(cancel,&QPushButton::clicked,this,[this,state,busy]{transfer.cancel();busy(false);state->setText("已停止，已有断点保留。/ Stopped; partial file retained.");});
    connect(&transfer,&Transfer::codeReady,this,[code](const QString &s){code->setPlainText(s);});
    connect(&transfer,&Transfer::status,state,&QLabel::setText);
    connect(&transfer,&Transfer::offered,this,[this](const QString &name,qint64 n){
        transfer.acceptOffer(QMessageBox::question(this,"接收文件 / Receive file",name+"\n"+QLocale().formattedDataSize(n)+"\n\n确认保存到所选文件夹？") == QMessageBox::Yes);
    });
    connect(&transfer,&Transfer::progress,this,[bar,amount](qint64 n,qint64 total){bar->setValue(total?int((long double)n*1000/total):1000);amount->setText(QLocale().formattedDataSize(n)+" / "+QLocale().formattedDataSize(total));});
    connect(&transfer,&Transfer::completed,this,[state,busy](const QString &p){busy(false);state->setText("传输完成，SHA-256 一致 / Verified\n"+p);});
    connect(&transfer,&Transfer::failed,this,[state,busy](const QString &s){busy(false);state->setText("传输未完成 / "+s);});
}
