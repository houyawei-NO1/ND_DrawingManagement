#include "ocrresultpopup.h"
#include "ocrhelper.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPlainTextEdit>
#include <QToolButton>
#include <QAction>
#include <QTextDocument>
#include <QTextCursor>
#include <QTextCharFormat>
#include <QTextEdit>
#include <QFont>
#include <QSize>

OcrResultPopup::OcrResultPopup(QWidget *parent) : QFrame(parent)
{
    setObjectName(QStringLiteral("ocrPopup"));
    setAttribute(Qt::WA_StyledBackground, true);
    setFixedWidth(378);
    setStyleSheet(QStringLiteral(R"(
#ocrPopup { background:rgba(8,12,20,0.92); border:1px solid rgba(160,180,220,0.22);
            border-radius:9px; }
#ocrPopup QWidget { background:transparent; }
#ocrPopup #ocrPopHead { background:rgba(255,255,255,0.035);
            border-bottom:1px solid rgba(255,255,255,0.10); }
#ocrPopup #ocrGlow { background:#43CF7C; border-radius:4px; }
#ocrPopTitle { color:#E6EBF7; font-size:13px; font-weight:bold; }
#ocrFileLabel { color:#7E8AA2; font-size:10px; }
#ocrCloseBtn { color:#8B95AC; font-size:17px; border:none; padding:0; }
#ocrCloseBtn:hover { color:#FF5252; }
#ocrSecTitle { color:#9AA6BD; font-size:11px; }
#ocrAutoTAG { color:#43CF7C; font-size:10px; }
#ocrHint { color:#6D7890; font-size:10px; }
#ocrModelList { background:transparent; border:none; }
#ocrModelList::item { background:rgba(255,255,255,0.04);
            border:1px solid rgba(255,255,255,0.09); border-radius:5px; }
#ocrModelList::item:selected { background:rgba(67,207,124,0.14);
            border:1px solid rgba(67,207,124,0.75); }
#ocrPopup #ocrModelIdx { background:#2B364C; color:#A9B5CC; border-radius:9px; font-size:10px; }
#ocrModelName { color:#E7ECF8; font-family:Consolas; font-size:12px; }
#ocrModelHit { color:#43CF7C; font-size:10px; }
#ocrModelMiss { color:#8A95AC; font-size:10px; }
#ocrPopup #ocrFixTag { color:#F0C674; background:rgba(240,198,116,0.12);
            border:1px solid rgba(240,198,116,0.35); border-radius:3px;
            padding:0 4px; font-size:9px; }
#ocrPopup #ocrFindSec { background:rgba(255,255,255,0.022);
            border-top:1px solid rgba(255,255,255,0.08);
            border-bottom:1px solid rgba(255,255,255,0.08); }
#ocrPopup QLineEdit { background:#111722; border:1px solid #3A465F;
            border-radius:5px; padding:5px 8px; color:#fff; font-size:11px; }
#ocrPopup QLineEdit:focus { border:1px solid #43CF7C; }
#ocrFindCount { color:#8CE0B7; font-size:11px; }
#ocrPopup #ocrRawText { background:rgba(3,6,12,0.55);
            border:1px solid rgba(255,255,255,0.10); border-radius:5px; color:#A9B6CC; }
#ocrPopup #ocrFoot { background:rgba(67,207,124,0.08); color:#8DD9B3; font-size:10px;
            border-top:1px solid rgba(255,255,255,0.10); padding-left:12px; }
)"));

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // 头部：标题 + 图片文件名 + 关闭
    auto *head = new QWidget(this);
    head->setObjectName(QStringLiteral("ocrPopHead"));
    head->setFixedHeight(42);
    auto *hl = new QHBoxLayout(head);
    hl->setContentsMargins(12, 0, 10, 0);
    hl->setSpacing(8);
    auto *glow = new QLabel(head);
    glow->setObjectName(QStringLiteral("ocrGlow"));
    glow->setFixedSize(8, 8);
    auto *title = new QLabel(QStringLiteral("OCR 识别结果"), head);
    title->setObjectName(QStringLiteral("ocrPopTitle"));
    m_fileLabel = new QLabel(head);
    m_fileLabel->setObjectName(QStringLiteral("ocrFileLabel"));
    auto *closeBtn = new QToolButton(head);
    closeBtn->setObjectName(QStringLiteral("ocrCloseBtn"));
    closeBtn->setText(QStringLiteral("×"));
    closeBtn->setCursor(Qt::PointingHandCursor);
    closeBtn->setToolTip(QStringLiteral("关闭（搜索结果保留）"));
    connect(closeBtn, &QToolButton::clicked, this, [this] {
        hide();
        emit closed();
    });
    hl->addWidget(glow);
    hl->addWidget(title);
    hl->addWidget(m_fileLabel, 1);
    hl->addWidget(closeBtn);
    root->addWidget(head);

    // 上半部分：识别到的所有型号
    auto *modelSec = new QWidget(this);
    modelSec->setFixedHeight(158);
    auto *ml = new QVBoxLayout(modelSec);
    ml->setContentsMargins(12, 10, 12, 6);
    ml->setSpacing(7);
    auto *mtRow = new QWidget(modelSec);
    auto *mtr = new QHBoxLayout(mtRow);
    mtr->setContentsMargins(0, 0, 0, 0);
    m_modelTitle = new QLabel(QStringLiteral("识别型号"), mtRow);
    m_modelTitle->setObjectName(QStringLiteral("ocrSecTitle"));
    auto *autoTag = new QLabel(QStringLiteral("● 已自动搜索"), mtRow);
    autoTag->setObjectName(QStringLiteral("ocrAutoTAG"));
    mtr->addWidget(m_modelTitle);
    mtr->addStretch();
    mtr->addWidget(autoTag);
    ml->addWidget(mtRow);

    m_modelList = new QListWidget(modelSec);
    m_modelList->setObjectName(QStringLiteral("ocrModelList"));
    m_modelList->setFixedHeight(112);
    m_modelList->setSpacing(4);
    m_modelList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    connect(m_modelList, &QListWidget::currentRowChanged,
            this, &OcrResultPopup::onModelClicked);
    ml->addWidget(m_modelList);
    root->addWidget(modelSec);

    // 中间：在原文里快速定位的查找框（叉号与搜索框同款）
    auto *findSec = new QWidget(this);
    findSec->setObjectName(QStringLiteral("ocrFindSec"));
    findSec->setFixedHeight(48);
    auto *fl = new QHBoxLayout(findSec);
    fl->setContentsMargins(12, 8, 12, 8);
    fl->setSpacing(8);
    m_findEdit = new QLineEdit(findSec);
    m_findEdit->setPlaceholderText(QStringLiteral("在 OCR 原文中查找…"));
    m_findEdit->setToolTip(QStringLiteral("输入型号或关键字，回车跳转到下一个匹配"));
    auto *clearAct = new QAction(OcrHelper::makeClearIcon(),
                                 QStringLiteral("清除查找内容"), m_findEdit);
    clearAct->setVisible(false);
    m_findEdit->addAction(clearAct, QLineEdit::TrailingPosition);
    for (QObject *o : clearAct->associatedObjects()) {
        if (auto *w = qobject_cast<QWidget *>(o)) {
            w->setAttribute(Qt::WA_Hover, true);
            w->setCursor(Qt::PointingHandCursor);
        }
    }
    connect(clearAct, &QAction::triggered, this, [this] {
        m_findEdit->clear();
        m_findEdit->setFocus();
    });
    connect(m_findEdit, &QLineEdit::textChanged, this,
            [clearAct](const QString &s) { clearAct->setVisible(!s.isEmpty()); });
    connect(m_findEdit, &QLineEdit::textChanged,
            this, &OcrResultPopup::onFindTextChanged);
    connect(m_findEdit, &QLineEdit::returnPressed,
            this, &OcrResultPopup::onFindNext);
    fl->addWidget(m_findEdit, 1);
    m_countLabel = new QLabel(QStringLiteral("0/0"), findSec);
    m_countLabel->setObjectName(QStringLiteral("ocrFindCount"));
    m_countLabel->setFixedWidth(34);
    m_countLabel->setAlignment(Qt::AlignCenter);
    fl->addWidget(m_countLabel);
    root->addWidget(findSec);

    // 下半部分：OCR 原文
    auto *rawSec = new QWidget(this);
    auto *rl = new QVBoxLayout(rawSec);
    rl->setContentsMargins(12, 8, 12, 8);
    rl->setSpacing(6);
    auto *rt = new QWidget(rawSec);
    auto *rtr = new QHBoxLayout(rt);
    rtr->setContentsMargins(0, 0, 0, 0);
    auto *rawTitle = new QLabel(QStringLiteral("OCR 原文"), rt);
    rawTitle->setObjectName(QStringLiteral("ocrSecTitle"));
    auto *rawHint = new QLabel(QStringLiteral("回车跳转下一个匹配"), rt);
    rawHint->setObjectName(QStringLiteral("ocrHint"));
    rtr->addWidget(rawTitle);
    rtr->addStretch();
    rtr->addWidget(rawHint);
    rl->addWidget(rt);

    m_rawText = new QPlainTextEdit(rawSec);
    m_rawText->setObjectName(QStringLiteral("ocrRawText"));
    m_rawText->setReadOnly(true);
    m_rawText->setLineWrapMode(QPlainTextEdit::NoWrap);
    QFont mono(QStringLiteral("Consolas"));
    mono.setPointSize(9);
    m_rawText->setFont(mono);
    rl->addWidget(m_rawText, 1);
    root->addWidget(rawSec, 1);

    m_footLabel = new QLabel(this);
    m_footLabel->setObjectName(QStringLiteral("ocrFoot"));
    m_footLabel->setFixedHeight(32);
    root->addWidget(m_footLabel);
}

QWidget *OcrResultPopup::makeModelRow(int index, const QString &model, int count,
                                      const QString &rawModel)
{
    auto *w = new QWidget(m_modelList);
    auto *l = new QHBoxLayout(w);
    l->setContentsMargins(9, 2, 9, 2);
    l->setSpacing(9);
    auto *idx = new QLabel(QString::number(index), w);
    idx->setObjectName(QStringLiteral("ocrModelIdx"));
    idx->setFixedSize(18, 18);
    idx->setAlignment(Qt::AlignCenter);
    auto *name = new QLabel(model, w);
    name->setObjectName(QStringLiteral("ocrModelName"));
    auto *match = new QLabel(count > 0 ? QStringLiteral("匹配 %1 条 ✓").arg(count)
                                       : QStringLiteral("未匹配"), w);
    match->setObjectName(count > 0 ? QStringLiteral("ocrModelHit")
                                   : QStringLiteral("ocrModelMiss"));
    l->addWidget(idx);
    l->addWidget(name, 1);

    const bool fixed = !rawModel.isEmpty()
        && OcrHelper::normalizeModelKey(rawModel) != OcrHelper::normalizeModelKey(model);
    if (fixed) {
        auto *tag = new QLabel(QStringLiteral("已纠错"), w);
        tag->setObjectName(QStringLiteral("ocrFixTag"));
        const QString tip = QStringLiteral("OCR 原文：%1\n已按数据库型号纠正为：%2")
                                .arg(rawModel).arg(model);
        tag->setToolTip(tip);
        name->setToolTip(tip);
        l->addWidget(tag);
    }
    // 查找框要用原始 OCR 型号，才能在下方原文里定位到那一行
    w->setProperty("ocrRawModel", fixed ? rawModel : QString());
    l->addWidget(match);
    return w;
}

void OcrResultPopup::setResult(const QString &rawText, const QStringList &models,
                               const QList<int> &matchCounts, const QString &fileName,
                               const QStringList &rawModels)
{
    m_fileLabel->setText(fileName);
    m_modelTitle->setText(QStringLiteral("识别型号（%1）").arg(models.size()));
    m_modelList->clear();
    int fixedCount = 0;
    for (int i = 0; i < models.size(); ++i) {
        const QString rawModel = rawModels.value(i);
        if (!rawModel.isEmpty()
            && OcrHelper::normalizeModelKey(rawModel) != OcrHelper::normalizeModelKey(models.at(i)))
            ++fixedCount;
        auto *item = new QListWidgetItem(m_modelList);
        item->setSizeHint(QSize(340, 32));
        m_modelList->addItem(item);
        m_modelList->setItemWidget(item,
            makeModelRow(i + 1, models.at(i), matchCounts.value(i, 0), rawModel));
    }
    m_rawText->setPlainText(rawText);
    m_footLabel->setText(models.isEmpty()
        ? QStringLiteral("  未识别到型号，请对照下方 OCR 原文核对")
        : (fixedCount > 0
            ? QStringLiteral("  已按数据库型号纠正 %1 个识别错误，搜索 %2 个型号")
                  .arg(fixedCount).arg(models.size())
            : QStringLiteral("  已搜索 %1 个型号，表格显示全部匹配结果").arg(models.size())));

    // 原文里是误识别的型号，查找框必须用原始 OCR 型号才能定位到那一行
    const QString firstFind = rawModels.value(0, models.value(0));
    m_findEdit->blockSignals(true);
    m_findEdit->setText(models.isEmpty() ? QString() : firstFind);
    m_findEdit->blockSignals(false);
    if (models.isEmpty()) {
        m_matches.clear();
        m_current = -1;
        m_rawText->setExtraSelections(QList<QTextEdit::ExtraSelection>());
        updateCountLabel();
    } else {
        m_modelList->setCurrentRow(0);
        highlight(firstFind, 0); // setText 相同文本不会触发 textChanged，这里显式高亮
    }
}

void OcrResultPopup::highlight(const QString &kw, int current)
{
    m_keyword = kw.trimmed();
    m_matches.clear();
    m_current = -1;
    QList<QTextEdit::ExtraSelection> sels;
    if (!m_keyword.isEmpty() && m_rawText->document()) {
        QTextDocument *doc = m_rawText->document();
        QTextCursor cur(doc);
        for (;;) {
            cur = doc->find(m_keyword, cur);
            if (cur.isNull())
                break;
            m_matches << cur;
        }
        QTextCharFormat normal, active;
        normal.setBackground(QColor(67, 207, 124, 70));
        normal.setForeground(QColor(0x8C, 0xE0, 0xB7));
        active.setBackground(QColor(0xD8, 0xB2, 0x4A));
        active.setForeground(QColor(0x1A, 0x14, 0x05));
        if (!m_matches.isEmpty()) {
            m_current = ((current % m_matches.size()) + m_matches.size()) % m_matches.size();
            for (int i = 0; i < m_matches.size(); ++i) {
                QTextEdit::ExtraSelection sel;
                sel.cursor = m_matches.at(i);
                sel.format = (i == m_current) ? active : normal;
                sels << sel;
            }
            m_rawText->setTextCursor(m_matches.at(m_current));
            m_rawText->centerCursor();
        }
    }
    m_rawText->setExtraSelections(sels);
    updateCountLabel();
}

void OcrResultPopup::updateCountLabel()
{
    m_countLabel->setText(m_matches.isEmpty()
        ? QStringLiteral("0/0")
        : QStringLiteral("%1/%2").arg(m_current + 1).arg(m_matches.size()));
}

void OcrResultPopup::onFindTextChanged(const QString &kw)
{
    highlight(kw, 0);
}

void OcrResultPopup::onFindNext()
{
    if (m_matches.isEmpty())
        return;
    highlight(m_keyword, m_current + 1);
}

void OcrResultPopup::onModelClicked(int row)
{
    if (row < 0 || row >= m_modelList->count())
        return;
    auto *w = m_modelList->itemWidget(m_modelList->item(row));
    if (!w)
        return;
    auto *name = w->findChild<QLabel *>(QStringLiteral("ocrModelName"));
    if (!name)
        return;
    const QString raw = w->property("ocrRawModel").toString();
    m_findEdit->setText(raw.isEmpty() ? name->text() : raw);
}
