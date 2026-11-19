#include "ndinputdialog.h"

#include <QEvent>
#include <QEventLoop>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

NdInputDialog::NdInputDialog(QWidget *parent, const QString &title, const QString &label,
                             const QVariant &defaultValue, bool isInt, int min, int max, int width)
    : OverlayMask(parent ? parent->window() : nullptr), m_isInt(isInt)
{
    setObjectName(QStringLiteral("ndInputMask"));
    setFocusPolicy(Qt::StrongFocus);
    enableOverlay();
    setStyleSheet(QStringLiteral(
        "QWidget#ndInputCard{background:#22293C;border:1px solid rgba(255,255,255,0.12);"
        "border-radius:12px;}"
        "QWidget#ndInputCard QLabel{background:transparent;}"
        "QLineEdit, QSpinBox{background:#1B2133;border:1px solid rgba(255,255,255,0.18);"
        "border-radius:6px;color:#ffffff;padding:7px 10px;font-size:14px;}"
        "QLineEdit:focus, QSpinBox:focus{border:1px solid #43CF7C;}"
        "QPushButton#ndInputOk{background:#43CF7C;color:#0B1F14;border:none;border-radius:6px;"
        "padding:7px 26px;font-weight:bold;}"
        "QPushButton#ndInputOk:hover{background:#5CE0A0;}"
        "QPushButton#ndInputCancel{background:rgba(224,108,117,0.85);color:#fff;border:none;"
        "border-radius:6px;padding:7px 22px;}"
        "QPushButton#ndInputCancel:hover{background:rgba(232,130,138,0.95);}"));

    QVBoxLayout *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);

    QWidget *card = new QWidget(this);
    card->setObjectName(QStringLiteral("ndInputCard"));
    card->setFixedWidth(width);
    QVBoxLayout *cl = new QVBoxLayout(card);
    cl->setContentsMargins(26, 22, 26, 20);
    cl->setSpacing(14);

    QLabel *titleLb = new QLabel(title, card);
    titleLb->setStyleSheet(QStringLiteral("font-size:17px;font-weight:bold;color:#FFFFFF;"));
    QLabel *labelLb = new QLabel(label, card);
    labelLb->setStyleSheet(QStringLiteral("color:rgba(255,255,255,0.85);font-size:13px;"));

    if (m_isInt) {
        m_spin = new QSpinBox(card);
        m_spin->setRange(min, max);
        m_spin->setValue(defaultValue.toInt());
        m_spin->setFocus();
        cl->addWidget(titleLb);
        cl->addWidget(labelLb);
        cl->addWidget(m_spin);
    } else {
        m_edit = new QLineEdit(defaultValue.toString(), card);
        m_edit->setFocus();
        m_edit->selectAll();
        connect(m_edit, &QLineEdit::returnPressed, this, [this] { finish(true); });
        cl->addWidget(titleLb);
        cl->addWidget(labelLb);
        cl->addWidget(m_edit);
    }

    QHBoxLayout *btns = new QHBoxLayout();
    btns->addStretch();
    QPushButton *cancel = new QPushButton(QStringLiteral("取 消"), card);
    cancel->setObjectName(QStringLiteral("ndInputCancel"));
    cancel->setCursor(Qt::PointingHandCursor);
    connect(cancel, &QPushButton::clicked, this, [this] { finish(false); });
    btns->addWidget(cancel);
    QPushButton *ok = new QPushButton(QStringLiteral("确 定"), card);
    ok->setObjectName(QStringLiteral("ndInputOk"));
    ok->setCursor(Qt::PointingHandCursor);
    ok->setDefault(true);
    connect(ok, &QPushButton::clicked, this, [this] { finish(true); });
    btns->addWidget(ok);

    cl->addSpacing(4);
    cl->addLayout(btns);
    outer->addWidget(card, 0, Qt::AlignCenter);

    installEventFilter(this);
}

bool NdInputDialog::getText(QWidget *parent, const QString &title, const QString &label,
                            const QString &defaultText, QString *out, int width)
{
    NdInputDialog dlg(parent, title, label, defaultText, false, 0, 0, width);
    dlg.showOverlay();
    dlg.setFocus();
    QEventLoop loop;
    dlg.m_loop = &loop;
    loop.exec();
    dlg.m_loop = nullptr;
    if (dlg.m_ok && out)
        *out = dlg.m_edit ? dlg.m_edit->text().trimmed() : QString();
    return dlg.m_ok;
}

bool NdInputDialog::getInt(QWidget *parent, const QString &title, const QString &label,
                           int defaultValue, int min, int max, int *out, int width)
{
    NdInputDialog dlg(parent, title, label, defaultValue, true, min, max, width);
    dlg.showOverlay();
    dlg.setFocus();
    QEventLoop loop;
    dlg.m_loop = &loop;
    loop.exec();
    dlg.m_loop = nullptr;
    if (dlg.m_ok && out)
        *out = dlg.m_spin ? dlg.m_spin->value() : defaultValue;
    return dlg.m_ok;
}

void NdInputDialog::finish(bool ok)
{
    m_ok = ok;
    hide();
    if (m_loop)
        m_loop->quit();
}

bool NdInputDialog::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == this && event->type() == QEvent::MouseButtonRelease) {
        auto *me = static_cast<QMouseEvent *>(event);
        QWidget *card = findChild<QWidget *>(QStringLiteral("ndInputCard"));
        if (!card || !card->geometry().contains(me->pos())) {
            finish(false);
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void NdInputDialog::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape)
        finish(false);
    else
        QWidget::keyPressEvent(event);
}
