#include "ndmessagebox.h"

#include <QApplication>
#include <QEvent>
#include <QEventLoop>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QScreen>
#include <QVBoxLayout>

NdMessageBox::NdMessageBox(QWidget *parent, Kind kind, const QString &title,
                           const QString &text, bool blueOk, int width)
    : OverlayMask(parent ? parent->window() : nullptr)
{
    setObjectName(QStringLiteral("ndMsgMask"));
    setFocusPolicy(Qt::StrongFocus);
    enableOverlay(); // 全屏半透明黑遮罩由 OverlayMask 统一提供
    setStyleSheet(QStringLiteral(
        "QWidget#ndMsgCard{background:#22293C;border:1px solid rgba(255,255,255,0.12);"
        "border-radius:12px;}"
        "QWidget#ndMsgCard QLabel{background:transparent;}"
        "QPushButton#ndMsgOk{background:#43CF7C;color:#0B1F14;border:none;border-radius:6px;"
        "padding:6px 26px;font-weight:bold;}"
        "QPushButton#ndMsgOk:hover{background:#5CE0A0;}"
        "QPushButton#ndMsgOkBlue{background:#1673db;color:#ffffff;border:none;"
        "border-radius:6px;padding:6px 26px;font-weight:bold;}"
        "QPushButton#ndMsgOkBlue:hover{background:#2D86E8;}"
        "QPushButton#ndMsgOkBlue:pressed{background:#125FB5;}"
        "QPushButton#ndMsgCancel{background:rgba(224,108,117,0.85);color:#fff;border:none;"
        "border-radius:6px;padding:6px 22px;}"
        "QPushButton#ndMsgCancel:hover{background:rgba(232,130,138,0.95);}"));

    QGridLayout *outer = new QGridLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);

    QWidget *card = new QWidget(this);
    card->setObjectName(QStringLiteral("ndMsgCard"));
    card->setFixedWidth(width);
    QVBoxLayout *cl = new QVBoxLayout(card);
    cl->setContentsMargins(26, 20, 26, 18);
    cl->setSpacing(12);

    // 标题颜色按提示级别区分：错误偏红、警告偏黄、普通白色
    const char *titleColor = "#FFFFFF";
    if (kind == Error)
        titleColor = "#E06C75";
    else if (kind == Warning)
        titleColor = "#E5C07B";

    QLabel *titleLb = new QLabel(title, card);
    titleLb->setStyleSheet(QStringLiteral("font-size:17px;font-weight:bold;color:%1;")
                               .arg(QLatin1String(titleColor)));
    QLabel *textLb = new QLabel(text, card);
    textLb->setWordWrap(true);
    textLb->setTextInteractionFlags(Qt::TextSelectableByMouse);
    textLb->setStyleSheet(QStringLiteral("color:rgba(255,255,255,0.85);font-size:13px;"));

    QHBoxLayout *btns = new QHBoxLayout();
    btns->addStretch();
    if (kind == Question) {
        QPushButton *cancel = new QPushButton(QStringLiteral("取 消"), card);
        cancel->setObjectName(QStringLiteral("ndMsgCancel"));
        cancel->setCursor(Qt::PointingHandCursor);
        connect(cancel, &QPushButton::clicked, this, [this] { finish(false); });
        btns->addWidget(cancel);
    }
    QPushButton *ok = new QPushButton(QStringLiteral("确 定"), card);
    ok->setObjectName(blueOk ? QStringLiteral("ndMsgOkBlue") : QStringLiteral("ndMsgOk"));
    ok->setCursor(Qt::PointingHandCursor);
    ok->setDefault(true);
    connect(ok, &QPushButton::clicked, this, [this] { finish(true); });
    btns->addWidget(ok);

    cl->addWidget(titleLb);
    cl->addWidget(textLb, 1);
    cl->addSpacing(4);
    cl->addLayout(btns);
    outer->addWidget(card, 0, 0, Qt::AlignCenter);

    installEventFilter(this);
    ok->setFocus();
}

bool NdMessageBox::run(QWidget *parent, Kind kind, const QString &title, const QString &text,
                       bool blueOk, int width)
{
    NdMessageBox box(parent, kind, title, text, blueOk, width);
    box.showOverlay(); // 统一全屏遮罩，不再按父窗口尺寸裁剪
    box.setFocus();

    QEventLoop loop;
    box.m_loop = &loop;
    loop.exec();
    box.m_loop = nullptr;
    return box.m_ok;
}

void NdMessageBox::information(QWidget *parent, const QString &title, const QString &text)
{
    run(parent, Info, title, text);
}

void NdMessageBox::warning(QWidget *parent, const QString &title, const QString &text)
{
    run(parent, Warning, title, text);
}

void NdMessageBox::critical(QWidget *parent, const QString &title, const QString &text)
{
    run(parent, Error, title, text);
}

void NdMessageBox::details(QWidget *parent, const QString &title, const QString &html)
{
    run(parent, Info, title, html, false, 720);
}

bool NdMessageBox::question(QWidget *parent, const QString &title, const QString &text,
                            bool blueOk)
{
    return run(parent, Question, title, text, blueOk);
}

void NdMessageBox::finish(bool ok)
{
    m_ok = ok;
    hide();
    if (m_loop)
        m_loop->quit();
}

// 点卡片以外（遮罩）视为取消
bool NdMessageBox::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == this && event->type() == QEvent::MouseButtonRelease) {
        auto *me = static_cast<QMouseEvent *>(event);
        QWidget *card = findChild<QWidget *>(QStringLiteral("ndMsgCard"));
        if (!card || !card->geometry().contains(me->pos())) {
            finish(false);
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void NdMessageBox::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape)
        finish(false);
    else if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)
        finish(true);
    else
        QWidget::keyPressEvent(event);
}
