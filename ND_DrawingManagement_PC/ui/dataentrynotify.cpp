#include "dataentrynotify.h"

#include <QApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QList>
#include <QPointer>
#include <QPushButton>
#include <QScreen>
#include <QTimer>
#include <QVBoxLayout>

namespace {
// 多个通知同时在场时自下而上堆叠，避免后来的把前一个盖住
QList<QPointer<DataEntryNotify>> &visibleNotifies()
{
    static QList<QPointer<DataEntryNotify>> list;
    return list;
}

void forgetNotify(DataEntryNotify *w)
{
    QList<QPointer<DataEntryNotify>> &list = visibleNotifies();
    for (int i = list.size() - 1; i >= 0; --i) {
        if (!list.at(i) || list.at(i).data() == w)
            list.removeAt(i);
    }
}
} // namespace

DataEntryNotify::DataEntryNotify(const Options &opt, QWidget *parent) : QWidget(parent)
{
    // Tool + 置顶但不铺满屏幕：只在右下角占一小块，不遮挡其它程序
    setWindowFlags(Qt::FramelessWindowHint | Qt::Tool | Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setObjectName(QStringLiteral("dailyQtyNotify"));
    setFixedWidth(360);

    // 全局 QSS 里 QWidget 有深色背景，这里逐项覆盖，保证卡片、标签、按键配色一致
    setStyleSheet(QStringLiteral(
        "QWidget#dailyQtyNotify{background:transparent;}"
        "QWidget#dailyQtyCard{background:#22293C;border:1px solid rgba(255,255,255,0.14);"
        "border-radius:12px;}"
        "QWidget#dailyQtyCard QLabel{background:transparent;}"
        "QPushButton#ndMsgOk{background:#43CF7C;color:#0B1F14;border:none;border-radius:6px;"
        "padding:6px 26px;font-weight:bold;}"
        "QPushButton#ndMsgOk:hover{background:#5CE0A0;}"
        "QPushButton#ndMsgCancel{background:rgba(224,108,117,0.85);color:#fff;border:none;"
        "border-radius:6px;padding:6px 22px;}"
        "QPushButton#ndMsgCancel:hover{background:rgba(232,130,138,0.95);}"));

    QVBoxLayout *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);

    QWidget *card = new QWidget(this);
    card->setObjectName(QStringLiteral("dailyQtyCard"));
    outer->addWidget(card);

    QVBoxLayout *cl = new QVBoxLayout(card);
    cl->setContentsMargins(22, 18, 22, 16);
    cl->setSpacing(10);

    QLabel *title = new QLabel(opt.title, card);
    title->setStyleSheet(QStringLiteral(
        "font-size:17px;font-weight:bold;color:#FFFFFF;"));
    QLabel *text = new QLabel(opt.text, card);
    text->setWordWrap(true);
    text->setStyleSheet(QStringLiteral(
        "color:rgba(255,255,255,0.85);font-size:13px;"));
    cl->addWidget(title);
    cl->addWidget(text);

    if (!opt.tip.isEmpty()) {
        QLabel *tip = new QLabel(opt.tip, card);
        tip->setWordWrap(true);
        tip->setStyleSheet(QStringLiteral(
            "color:rgba(255,255,255,0.55);font-size:12px;"));
        cl->addWidget(tip);
    }

    QHBoxLayout *btns = new QHBoxLayout();
    btns->addStretch();
    if (opt.showCancel) {
        QPushButton *cancel = new QPushButton(QStringLiteral("取 消"), card);
        cancel->setObjectName(QStringLiteral("ndMsgCancel"));
        cancel->setCursor(Qt::PointingHandCursor);
        connect(cancel, &QPushButton::clicked, this, [this] { finish(false); });
        btns->addWidget(cancel);
    }

    m_ok = new QPushButton(QStringLiteral("确 定"), card);
    m_ok->setObjectName(QStringLiteral("ndMsgOk"));
    m_ok->setCursor(Qt::PointingHandCursor);
    m_ok->setDefault(true);
    connect(m_ok, &QPushButton::clicked, this, [this] { finish(true); });
    btns->addWidget(m_ok);

    cl->addSpacing(4);
    cl->addLayout(btns);

    setFocusPolicy(Qt::StrongFocus);

    // 无人处理时自动收起（例如自动备份提示 10 分钟）
    if (opt.autoCloseMs > 0)
        QTimer::singleShot(opt.autoCloseMs, this, [this] { finish(false); });
}

DataEntryNotify::~DataEntryNotify()
{
    forgetNotify(this);
}

void DataEntryNotify::showMessage(const Options &opt)
{
    auto *w = new DataEntryNotify(opt, nullptr);
    w->showAtBottomRight();
}

void DataEntryNotify::showAtBottomRight()
{
    adjustSize();
    forgetNotify(this);
    QList<QPointer<DataEntryNotify>> &list = visibleNotifies();
    int offset = 0;
    for (int i = list.size() - 1; i >= 0; --i) {
        DataEntryNotify *n = list.at(i).data();
        if (n && n->isVisible())
            offset += n->height() + 12;
    }
    list.append(QPointer<DataEntryNotify>(this));

    QScreen *screen = QApplication::primaryScreen();
    const QRect g = screen ? screen->availableGeometry() : QRect(0, 0, 1280, 720);
    move(g.right() - width() - 18, g.bottom() - height() - 18 - offset);
    show();
    raise();
}

void DataEntryNotify::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape)
        finish(false);
    else if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)
        finish(true);
    else
        QWidget::keyPressEvent(event);
}

void DataEntryNotify::finish(bool ok)
{
    if (m_finished)
        return;
    m_finished = true;
    hide();
    if (ok)
        emit accepted();
    else
        emit rejected();
    deleteLater();
}
