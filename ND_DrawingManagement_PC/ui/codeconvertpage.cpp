#include "codeconvertpage.h"
#include "util/modelcodec.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QHeaderView>
#include <QAbstractItemView>
#include <QScrollArea>
#include <QFrame>
#include <QGuiApplication>
#include <QClipboard>
#include <QToolTip>
#include <QCursor>

namespace {
const QString kExample = QStringLiteral("MF1004-8C-48-0530A");

// 按键沿用「芯片查询」页风格：绿色实心主按钮 + 圆角描边次按钮。
// 样式直接设在按钮自身，且带 #codecPage 前缀，避免页面级
// "#codecPage QWidget{background:transparent}" 把按钮压成看不清的文字。
const char *kGoButtonQss =
    "#codecPage QPushButton#chipGreen{background:#43CF7C;color:#0B1F14;"
    "border:1px solid #43CF7C;border-radius:8px;font-size:12px;font-weight:bold;"
    "padding:0 14px;min-height:32px;}"
    "#codecPage QPushButton#chipGreen:hover{background:#5BE090;border-color:#5BE090;}"
    "#codecPage QPushButton#chipGreen:disabled{background:transparent;color:#6D7890;"
    "border-color:#3A465F;font-weight:normal;}";
const char *kMinorButtonQss =
    "#codecPage QPushButton#codecMinor{background:#2E3750;color:#F2F5FC;"
    "border:1px solid #5A6680;border-radius:8px;font-size:12px;"
    "padding:0 14px;min-height:32px;}"
    "#codecPage QPushButton#codecMinor:hover{background:#35426A;"
    "border-color:#43CF7C;color:#fff;}"
    "#codecPage QPushButton#codecMinor:disabled{background:transparent;color:#6D7890;"
    "border-color:#3A465F;}";

void setRuleCell(QTableWidget *t, int row, int col, const QString &text)
{
    auto *it = new QTableWidgetItem(text);
    it->setTextAlignment(Qt::AlignCenter);
    t->setItem(row, col, it);
}

void setupRuleTable(QTableWidget *t, int rows)
{
    t->verticalHeader()->setVisible(false);
    t->verticalHeader()->setDefaultSectionSize(26);
    t->setEditTriggers(QAbstractItemView::NoEditTriggers);
    t->setSelectionMode(QAbstractItemView::NoSelection);
    t->setFocusPolicy(Qt::NoFocus);
    t->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    t->setFixedHeight(rows * 26 + 30);
    QHeaderView *hh = t->horizontalHeader();
    for (int c = 0; c < t->columnCount(); ++c)
        hh->setSectionResizeMode(c, QHeaderView::Stretch);
}
} // namespace

CodeConvertPage::CodeConvertPage(QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("codecPage"));
    buildUi();
}

void CodeConvertPage::buildUi()
{
    setStyleSheet(QStringLiteral(
        "#codecPage{background:#2B3046;color:#fff;}"
        "#codecPage QWidget{background:transparent;}"
        "#codecPage QLabel{color:rgba(255,255,255,0.72);font-size:12px;}"
        "#codecPage QLineEdit{background:#22293C;border:1px solid #4A5468;"
        "    border-radius:8px;padding:6px 10px;color:#fff;font-size:13px;min-height:24px;}"
        "#codecPage QLineEdit:focus{border:1px solid #43CF7C;}"
        "#codecPage QLineEdit#codecOutput{color:#8CE0B7;font-weight:bold;}"
        "#codecPage #codecCard{background:rgba(0,0,0,0.16);"
        "    border:1px solid #4A5468;border-radius:10px;}"
        "#codecPage #codecTitle{color:#fff;font-size:18px;font-weight:bold;}"
        "#codecPage #codecSection{color:#8CE0B7;font-size:13px;font-weight:bold;}"
        "#codecPage #codecExample{color:#8CE0B7;font-size:12px;font-weight:bold;}"
        "#codecPage QTableWidget{background:#22293C;"
        "    border:1px solid rgba(255,255,255,0.10);border-radius:6px;"
        "    gridline-color:rgba(255,255,255,0.08);color:#fff;font-size:12px;}"
        "#codecPage QHeaderView::section{background:#1B2233;"
        "    color:rgba(255,255,255,0.65);border:none;"
        "    border-bottom:1px solid rgba(255,255,255,0.10);"
        "    font-size:12px;font-weight:bold;height:26px;}"
        "#codecPage QTableWidget::item{padding:2px 6px;}"
        ));

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setStyleSheet(QStringLiteral("background:transparent;border:none;"));
    auto *content = new QWidget(scroll);
    auto *lay = new QVBoxLayout(content);
    lay->setContentsMargins(20, 14, 20, 16);
    lay->setSpacing(9);

    auto *title = new QLabel(QStringLiteral("编码转换"), content);
    title->setObjectName(QStringLiteral("codecTitle"));
    lay->addWidget(title);

    auto *intro = new QLabel(QStringLiteral(
        "本页说明中小拖型号与转换编码的对照规则，并可输入原型号直接得到转换后的编码型号。"
        "本规则仅适用于中小拖型号，其他型号请按原型号使用。"), content);
    intro->setWordWrap(true);
    lay->addWidget(intro);

    // 输入 / 输出卡片
    auto *card = new QFrame(content);
    card->setObjectName(QStringLiteral("codecCard"));
    auto *cardLay = new QVBoxLayout(card);
    cardLay->setContentsMargins(14, 12, 14, 12);
    cardLay->setSpacing(9);

    auto *inRow = new QHBoxLayout();
    inRow->addWidget(new QLabel(QStringLiteral("原型号"), card));
    m_edtInput = new QLineEdit(card);
    m_edtInput->setPlaceholderText(
        QStringLiteral("输入原型号，例如 MF1004-8C-48-0530A"));
    m_edtInput->setClearButtonEnabled(true);
    inRow->addWidget(m_edtInput, 1);
    auto *btnGo = new QPushButton(QStringLiteral("转 换"), card);
    btnGo->setObjectName(QStringLiteral("chipGreen"));
    btnGo->setStyleSheet(QString::fromUtf8(kGoButtonQss));
    btnGo->setCursor(Qt::PointingHandCursor);
    inRow->addWidget(btnGo);
    auto *btnExample = new QPushButton(QStringLiteral("填入示例"), card);
    btnExample->setObjectName(QStringLiteral("codecMinor"));
    btnExample->setStyleSheet(QString::fromUtf8(kMinorButtonQss));
    btnExample->setCursor(Qt::PointingHandCursor);
    inRow->addWidget(btnExample);
    cardLay->addLayout(inRow);

    auto *outRow = new QHBoxLayout();
    outRow->addWidget(new QLabel(QStringLiteral("转换后"), card));
    m_edtOutput = new QLineEdit(card);
    m_edtOutput->setObjectName(QStringLiteral("codecOutput"));
    m_edtOutput->setReadOnly(true);
    m_edtOutput->setPlaceholderText(QStringLiteral("转换结果显示在这里"));
    outRow->addWidget(m_edtOutput, 1);
    m_btnCopy = new QPushButton(QStringLiteral("复制结果"), card);
    m_btnCopy->setObjectName(QStringLiteral("codecMinor"));
    m_btnCopy->setStyleSheet(QString::fromUtf8(kMinorButtonQss));
    m_btnCopy->setCursor(Qt::PointingHandCursor);
    m_btnCopy->setEnabled(false);
    outRow->addWidget(m_btnCopy);
    cardLay->addLayout(outRow);

    m_lblHint = new QLabel(card);
    m_lblHint->setWordWrap(true);
    cardLay->addWidget(m_lblHint);
    setHint(QStringLiteral("输入型号后点「转 换」，或点「填入示例」查看效果。"), false);
    lay->addWidget(card);

    connect(btnGo, &QPushButton::clicked, this, &CodeConvertPage::onConvert);
    connect(m_edtInput, &QLineEdit::returnPressed, this, &CodeConvertPage::onConvert);
    connect(btnExample, &QPushButton::clicked, this, &CodeConvertPage::onFillExample);
    connect(m_btnCopy, &QPushButton::clicked, this, &CodeConvertPage::onCopyResult);

    // 规则对照表
    auto *ruleTitle = new QLabel(QStringLiteral("转换编码对照表"), content);
    ruleTitle->setObjectName(QStringLiteral("codecTitle"));
    lay->addWidget(ruleTitle);

    auto *sec1 = new QLabel(QStringLiteral("机型号与数字对应关系表"), content);
    sec1->setObjectName(QStringLiteral("codecSection"));
    lay->addWidget(sec1);
    lay->addWidget(buildMachineTable(content));

    auto *note = new QLabel(QStringLiteral(
        "注：1. 如图号带有字母「Z」，则字母「Z」不进行转换；\n"
        "　　2. 机型号后带「A」「C」「L」「S」「G」等字母，则这些字母不进行转换。"), content);
    note->setWordWrap(true);
    lay->addWidget(note);

    auto *sec2 = new QLabel(QStringLiteral("功率代码数字与字母对应关系表"), content);
    sec2->setObjectName(QStringLiteral("codecSection"));
    lay->addWidget(sec2);
    lay->addWidget(buildPowerTable(content));

    auto *how = new QLabel(QStringLiteral(
        "转换方式：只替换第一个「-」之前的机型号与功率代码数字，"
        "后面的规格段（含其中的数字、字母）原样保留。"), content);
    how->setWordWrap(true);
    lay->addWidget(how);

    auto *example = new QLabel(QStringLiteral(
        "示例：MF1004-8C-48-0530A → 63BAAE-8C-48-0530A　"
        "（MF→63，功率代码 1004→BAAE）"), content);
    example->setObjectName(QStringLiteral("codecExample"));
    example->setWordWrap(true);
    lay->addWidget(example);
    lay->addStretch(1);

    scroll->setWidget(content);
    scroll->viewport()->setStyleSheet(QStringLiteral("background:transparent;"));
    outer->addWidget(scroll);
}

QTableWidget *CodeConvertPage::buildMachineTable(QWidget *parent)
{
    const QVector<ModelCode::MachineCode> &codes = ModelCode::machineCodes();
    const int half = (codes.size() + 1) / 2;
    auto *t = new QTableWidget(half, 4, parent);
    t->setHorizontalHeaderLabels({QStringLiteral("机型号"), QStringLiteral("替代号"),
                                  QStringLiteral("机型号"), QStringLiteral("替代号")});
    setupRuleTable(t, half);
    for (int r = 0; r < half; ++r) {
        const ModelCode::MachineCode &left = codes.at(r);
        setRuleCell(t, r, 0, left.code);
        setRuleCell(t, r, 1, left.number);
        const int ri = r + half;
        if (ri < codes.size()) {
            const ModelCode::MachineCode &right = codes.at(ri);
            setRuleCell(t, r, 2, right.code);
            setRuleCell(t, r, 3, right.number);
        }
    }
    return t;
}

QTableWidget *CodeConvertPage::buildPowerTable(QWidget *parent)
{
    auto *t = new QTableWidget(5, 4, parent);
    t->setHorizontalHeaderLabels({QStringLiteral("功率代码数字"), QStringLiteral("替代号"),
                                  QStringLiteral("功率代码数字"), QStringLiteral("替代号")});
    setupRuleTable(t, 5);
    for (int r = 0; r < 5; ++r) {
        setRuleCell(t, r, 0, QString::number(r));
        setRuleCell(t, r, 1, ModelCode::powerLetter(r));
        setRuleCell(t, r, 2, QString::number(r + 5));
        setRuleCell(t, r, 3, ModelCode::powerLetter(r + 5));
    }
    return t;
}

void CodeConvertPage::setHint(const QString &text, bool error)
{
    m_lblHint->setText(text);
    m_lblHint->setStyleSheet(QStringLiteral(
        "background:transparent;font-size:12px;color:%1;")
        .arg(error ? QStringLiteral("#E06C75") : QStringLiteral("#8CE0B7")));
}

void CodeConvertPage::onFillExample()
{
    m_edtInput->setText(kExample);
    onConvert();
}

void CodeConvertPage::onConvert()
{
    QString err;
    const QString out = ModelCode::convert(m_edtInput->text(), &err);
    m_edtOutput->setText(out);
    m_btnCopy->setEnabled(!out.isEmpty());
    if (out.isEmpty())
        setHint(err.isEmpty() ? QStringLiteral("无法转换该型号") : err, true);
    else
        setHint(QStringLiteral("已按对照表转换：机型号与功率代码数字被替换，其余部分原样保留。"),
                false);
}

void CodeConvertPage::onCopyResult()
{
    const QString text = m_edtOutput->text();
    if (text.isEmpty())
        return;
    QGuiApplication::clipboard()->setText(text);
    QToolTip::showText(QCursor::pos(), QStringLiteral("已复制转换编码型号"), this);
}
