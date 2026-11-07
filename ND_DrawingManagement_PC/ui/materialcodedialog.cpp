#include "materialcodedialog.h"
#include "db/databasemanager.h"
#include "ndmessagebox.h"

#include <QGridLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QProgressBar>
#include <QTableWidget>
#include <QPlainTextEdit>
#include <QHeaderView>
#include <QFont>
#include <QCheckBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QDir>
#include <QProcess>
#include <QCoreApplication>
#include <QEventLoop>
#include <QColor>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QRegularExpression>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QGuiApplication>
#include <QClipboard>
#include <QMimeData>
#include <QImage>
#include <QDateTime>
#include <QDateEdit>
#include <QShortcut>
#include <QKeySequence>
#include <QUrl>
#include <QSet>
#include <QTableWidgetItem>
#include <algorithm>

// OCR 有时会把单号/序号/相邻列的数字串当成物料编码，识别结果的位数与库中登记的编码明显不同。
// 库里的编码是人工核对过的，可作为位数基准：两者都是纯数字且位数不一致即判定「编码识别错误」，
// 这类结果既不显示成中性的「已有编码」，也绝不写回数据库（即使勾选了覆盖）。
static bool isOcrCodeLengthError(const QString &ocrCode, const QString &dbCode)
{
    static const QRegularExpression digits(QStringLiteral("^\\d+$"));
    if (ocrCode.isEmpty() || dbCode.isEmpty())
        return false;
    if (!digits.match(ocrCode).hasMatch() || !digits.match(dbCode).hasMatch())
        return false;
    return ocrCode.size() != dbCode.size();
}

// 离线 OCR 引擎：程序目录/ocr/PaddleOCR-json/PaddleOCR-json.exe（与录入页一致）
static QString ocrEnginePath()
{
    const QString exe = QCoreApplication::applicationDirPath()
        + QStringLiteral("/ocr/PaddleOCR-json/PaddleOCR-json.exe");
    return QFileInfo::exists(exe) ? exe : QString();
}

// 解析引擎输出：{code:100, data:[{box, text}]} → 按坐标从上到下拼成多行文本
// （与 EntryDialog 中的实现保持一致的输出格式：每个文字块一行）
static bool parseOcrJsonText(const QByteArray &out, QString *text)
{
    const int b = out.indexOf('{');
    const int e = out.lastIndexOf('}');
    if (b < 0 || e <= b)
        return false;
    QJsonParseError perr;
    const QJsonDocument doc = QJsonDocument::fromJson(out.mid(b, e - b + 1), &perr);
    if (perr.error != QJsonParseError::NoError)
        return false;
    const QJsonObject obj = doc.object();
    if (obj.value(QStringLiteral("code")).toInt() != 100)
        return false;

    struct Line { int y; int x; QString text; };
    QList<Line> lines;
    const QJsonArray data = obj.value(QStringLiteral("data")).toArray();
    for (const QJsonValue &v : data) {
        const QJsonObject o = v.toObject();
        const QJsonArray box = o.value(QStringLiteral("box")).toArray();
        const int y = box.isEmpty() ? 0 : int(box.first().toArray().at(1).toDouble());
        const int x = box.isEmpty() ? 0 : int(box.first().toArray().at(0).toDouble());
        lines.append({y, x, o.value(QStringLiteral("text")).toString()});
    }
    std::sort(lines.begin(), lines.end(), [](const Line &a, const Line &b2) {
        return a.y == b2.y ? a.x < b2.x : a.y < b2.y;
    });
    QStringList parts;
    for (const Line &l : lines)
        parts.append(l.text);
    *text = parts.join(QLatin1Char('\n'));
    return !text->trimmed().isEmpty();
}

// OCR 尾部噪声：中文全角括号「（U）」、截图截断产生的「·」「…」「.·」等。
// 原图里型号后面是「英文括号 + 大写 U」，即「HHZ012-1H6GR(U)」；
// OCR 常输出成中文全角括号「（U）」「（u）」「（Ｕ）」或把括号拆成单独文字块「 (U)」。
// 统一成半角括号 + 大写 U，保证行尾 ASCII 型号能完整取出，
// 否则整行会被当成「取不到型号」丢弃 → 后面的编码全部错位。
static QString normalizeOcrLine(const QString &line)
{
    QString s = line.trimmed();
    QString out;
    out.reserve(s.size());
    for (int i = 0; i < s.size(); ++i) {
        const ushort u = s.at(i).unicode();
        if (u >= 0xFF01 && u <= 0xFF5E)      // 全角 ASCII（含中文全角括号、全角数字/字母）
            out.append(QChar(u - 0xFEE0));
        else if (u == 0x3000)                 // 全角空格
            out.append(QLatin1Char(' '));
        else
            out.append(s.at(i));
    }
    // 连续的中点/省略号/句点/星号等视为截图截断符号，去掉并标记。
    // OCR 常把看不清的型号尾部识别成「.*」，如「电源总开关及熔断器盒LW2304-6-48-02.*」；
    // 行尾的「*」不在型号字符集里，不清掉会让 extractModel 取不到型号，
    // 整行描述被丢弃，后面的编码与描述就会整体错位。
    static const QRegularExpression tailNoise(
        QStringLiteral("[\\x{00B7}\\x{2026}\\x{30FB}.*?~]+[\\s]*$"));
    out.replace(tailNoise, QString());
    // 括号里的 U：小写 u / 全角 Ｕ（上一步已转半角）统一成大写 U
    static const QRegularExpression parenU(
        QStringLiteral("\\(\\s*[uU]\\s*\\)"));
    out.replace(parenU, QStringLiteral("(U)"));
    // 括号被 OCR 拆成单独文字块时，型号与 (U) 之间会多出空格，合并回同一个型号
    static const QRegularExpression spaceBeforeParen(
        QStringLiteral("\\s+(\\([A-Za-z0-9]*\\))\\s*$"));
    out.replace(spaceBeforeParen, QStringLiteral("\\1"));
    return out.trimmed();
}

// 行尾是否带截断符号（用于把该行型号标记为「可能不完整」）
static bool hasTruncationMark(const QString &line)
{
    // 「.*」「*」「?」也是 OCR 对看不清/被截断的型号尾部的常见输出，同样要标记为不完整
    static const QRegularExpression re(
        QStringLiteral("[\\x{00B7}\\x{2026}\\x{30FB}]|\\.\\.|\\.\\s*$|[*?]+\\s*$"));
    return re.match(line).hasMatch();
}

// 从物料描述里取型号：形如「电源开关 JK451」「中央控制盒 LY1104-4-48A-020」
// 「YTO 多功率省油开关 6M3LR-T41.670801」「限位制动开关 15.117.000」
// 「直流转换器(48V.6kw)LE1604-48-023」「逆变器(220V,6kw)LE1604-48-024」。
// 做法：按空白切词，从后往前找第一个「不是小数量」的词，再取出其中的 ASCII 片段。
// 这样既能跳过行尾数量（780.0），也不会把纯数字型号（148000201 / 15.117.000）误删。
// 名称里的规格括号与型号紧贴时，括号前（规格参数）属于名称，只保留「)」之后的型号。
static QString extractModel(const QString &desc)
{
    static const QRegularExpression decimalOnly(QStringLiteral("^\\d+\\.\\d+$"));
    // 允许型号尾部带括号后缀，如 HHZ012-1H6GR(U)、MFZ804-48-031A
    static const QRegularExpression tailAscii(
        QStringLiteral("([A-Za-z0-9][A-Za-z0-9.\\-_/()]*)\\s*$"));
    static const QRegularExpression ws(QStringLiteral("\\s+"));
    // 「48V.6kw)LE1604-48-023」→「LE1604-48-023」；「)」在末尾（如 (U) 后缀）时不会命中，
    // 型号自带的括号后缀因此被完整保留。
    static const QRegularExpression modelAfterParen(
        QStringLiteral("\\)([A-Za-z0-9][A-Za-z0-9.\\-_/()]*)$"));

    // 先归一化：全角括号/数字 → 半角，去掉截图截断产生的行尾「· … .」
    const QStringList tokens = normalizeOcrLine(desc).split(ws, Qt::SkipEmptyParts);
    for (int i = tokens.size() - 1; i >= 0; --i) {
        const QString t = tokens.at(i);
        if (decimalOnly.match(t).hasMatch())
            continue; // 数量（如 780.0 / 1355.0）
        const auto m = tailAscii.match(t);
        if (m.hasMatch()) {
            QString model = m.captured(1);
            // 名称规格括号被粘进来时，剥掉括号及其之前的规格参数
            const auto pm = modelAfterParen.match(model);
            if (pm.hasMatch() && pm.captured(1).size() >= 3)
                model = pm.captured(1);
            // 截断符号被去掉后可能留下孤立尾点/连字符
            static const QRegularExpression dangling(QStringLiteral("[.\\-_/]+$"));
            model.remove(dangling);
            // 过滤掉纯数字的“序号/数量”残留（但允许 9 位以上数字型号，如 148000201）
            if (model.size() >= 3)
                return model;
        }
    }
    return QString();
}

// OCR 常把「大写产品名 + 空格 + 型号」中间的空格吃掉：
// 原图「TCU LF2204E-48-0921」被识别成「TCULF2204E-48-0921」。
// 这种行没有中文，型号前面会多出产品名，需要拆回数据库里登记的型号。
// 优先用库里已有的型号反推（取能精确匹配的最长后缀），再兜底常见大写缩写。
static QString splitUpperPrefix(const QString &model, QString *note)
{
    if (model.isEmpty())
        return model;
    static const QRegularExpression hasDigit(QStringLiteral("\\d"));
    DatabaseManager &db = DatabaseManager::instance();
    if (db.productIdByModel(model) >= 0)
        return model; // 本来就能匹配上，不动

    static const QStringList prefixes = {
        QStringLiteral("TCU"), QStringLiteral("ECU"), QStringLiteral("VCU"),
        QStringLiteral("HCU"), QStringLiteral("BCM"), QStringLiteral("PTO"),
        QStringLiteral("YTO"), QStringLiteral("DCU"), QStringLiteral("MCU"),
        QStringLiteral("BMS"), QStringLiteral("ABS"), QStringLiteral("GPS"),
        QStringLiteral("CCU"), QStringLiteral("ACU"), QStringLiteral("PCU"),
        QStringLiteral("TCM"), QStringLiteral("ICU"), QStringLiteral("LCM")
    };
    for (const QString &p : prefixes) {
        if (model.size() > p.size() + 3
            && model.startsWith(p, Qt::CaseInsensitive)
            && model.mid(p.size()).contains(hasDigit)) {
            if (note) *note = QStringLiteral("　【拆分】%1 → %2（产品名 %3 后空格被 OCR 合并）")
                                  .arg(model, model.mid(p.size()), p);
            return model.mid(p.size());
        }
    }
    // 不认识的大写缩写前缀：只剥离开头连续的大写字母，取能精确匹配到数据库型号的后缀
    int prefixLen = 0;
    while (prefixLen < model.size() && model.at(prefixLen).isUpper()
           && model.at(prefixLen).isLetter())
        ++prefixLen;
    for (int i = 1; i <= prefixLen - 1; ++i) {
        const QString c = model.mid(i);
        if (!c.contains(hasDigit))
            continue;
        if (db.productIdByModel(c) >= 0) {
            if (note) *note = QStringLiteral("　【拆分】%1 → %2（按数据库型号反推）").arg(model, c);
            return c;
        }
    }
    return model;
}

// 型号在库里可能登记成带 (U) 或不带括号；OCR 又可能把 (U) 识别成全角括号或漏识别。
// 依次尝试：原型号 → 去掉尾部括号 → 补上 (U)，返回命中的产品 id 与实际匹配的型号。
static int lookupProductId(const QString &model, QString *matched)
{
    DatabaseManager &db = DatabaseManager::instance();
    static const QRegularExpression parenTail(QStringLiteral("\\([^()]*\\)\\s*$"));
    QStringList candidates;
    candidates << model;
    if (parenTail.match(model).hasMatch()) {
        QString bare = model;
        bare.remove(parenTail);
        if (!bare.isEmpty())
            candidates << bare;
    } else {
        candidates << model + QStringLiteral("(U)");
    }
    for (const QString &c : candidates) {
        const int id = db.productIdByModel(c);
        if (id >= 0) {
            if (matched) *matched = c;
            return id;
        }
    }
    if (matched) matched->clear();
    return -1;
}

// 型号比较前归一化：去掉分隔符/括号/空格，统一大写，只留字母数字
static QString normalizeModelKey(const QString &s)
{
    QString out;
    out.reserve(s.size());
    for (const QChar ch : s) {
        if (ch.isLetterOrNumber())
            out.append(ch.toUpper());
    }
    return out;
}

// 容忍 OCR 多读/漏读 1 个字符：短串去掉任意一个字符后仍是长串前缀，也算「只显示一半」
static bool isPrefixWithinOneChar(const QString &shortKey, const QString &longKey)
{
    if (longKey.startsWith(shortKey))
        return true;
    for (qsizetype i = 0; i < shortKey.size(); ++i) {
        QString t = shortKey;
        t.remove(i, 1);
        if (longKey.startsWith(t))
            return true;
    }
    return false;
}

// 物料编码反查只适用于「截图不全、型号只显示一半」的情况：
// 归一化后两者必须构成前缀关系（容忍 1 个字符的 OCR 误差）。
// 像 MV1004-8C-48-0530 ↔ HB2204-1-48-022 这种完全不同的型号一律拒绝，
// 否则会把数量记到别的产品上。
static bool isModelRelated(const QString &ocrModel, const QString &dbModel)
{
    const QString a = normalizeModelKey(ocrModel);
    const QString b = normalizeModelKey(dbModel);
    if (a.size() < 5 || b.size() < 5)
        return false; // 太短的片段不足以判定，避免误命中
    const bool aShorter = a.size() <= b.size();
    const QString &shorter = aShorter ? a : b;
    const QString &longer  = aShorter ? b : a;
    return isPrefixWithinOneChar(shorter, longer);
}

// 截图被截断时型号读不全，但物料编码是独立的一列数字，通常完整。
// 用识别到的编码在库里精确反查 material_code，并要求型号相关（前缀关系）；
// 同一编码对应多个产品时再用型号相关性消歧，仍不唯一就不猜。
// 因型号差异过大被拒绝时，rejectedModel 带回库中型号供界面提示。
static int lookupProductByCode(const QString &code, const QString &model,
                               QString *matchedModel, QString *rejectedModel = nullptr)
{
    const QString c = code.trimmed();
    if (c.isEmpty())
        return -1;
    QList<Product> hits;
    const QList<Product> all = DatabaseManager::instance().searchProducts(QString(), c, -1);
    for (const Product &p : all) {
        if (p.materialCode.trimmed().compare(c, Qt::CaseInsensitive) == 0)
            hits << p;
    }
    if (hits.isEmpty())
        return -1;
    const QString m = model.trimmed();
    if (hits.size() > 1 && !m.isEmpty()) {
        QList<Product> narrowed;
        for (const Product &p : hits) {
            if (isModelRelated(m, p.model))
                narrowed << p;
        }
        if (!narrowed.isEmpty())
            hits = narrowed;
    }
    if (hits.size() != 1)
        return -1;
    const Product &p = hits.first();
    if (!isModelRelated(m, p.model)) {
        if (rejectedModel) *rejectedModel = p.model;
        return -1;
    }
    if (matchedModel) *matchedModel = p.model;
    return p.id;
}

static bool isImageFile(const QString &path)
{
    const QString s = QFileInfo(path).suffix().toLower();
    return s == QStringLiteral("jpg") || s == QStringLiteral("jpeg")
        || s == QStringLiteral("png") || s == QStringLiteral("bmp");
}

// 拖放区样式：默认灰虚线框，拖入时变绿（与录入页图纸拖入的提示一致）
static QString dropAreaStyle(bool highlight)
{
    if (highlight)
        return QStringLiteral("QLabel#dropArea{border:2px dashed #43CF7C;border-radius:8px;"
                              "color:#43CF7C;background:rgba(67,207,124,0.12);}");
    return QStringLiteral("QLabel#dropArea{border:2px dashed #4A5468;border-radius:8px;"
                          "color:rgba(255,255,255,0.5);background:rgba(0,0,0,0.15);}");
}

MaterialCodeDialog::MaterialCodeDialog(QWidget *parent, Mode mode, bool embedded)
    : QWidget(parent), m_mode(mode), m_embedded(embedded)
{
    // 自定义 QWidget 子类必须显式开启该属性，否则样式表背景不会绘制
    setAttribute(Qt::WA_StyledBackground, true);
    setAcceptDrops(true);
    if (!m_embedded) {
        // 覆盖式面板：复用「关于窗口」的半透明遮罩样式，点遮罩空白处关闭
        setObjectName(QStringLiteral("aboutOverlay"));
        hide();
        installEventFilter(this);
    }

    QLayout *root;
    if (m_embedded) {
        auto *v = new QVBoxLayout(this);
        v->setContentsMargins(0, 0, 0, 0);
        root = v;
    } else {
        auto *g = new QGridLayout(this);
        g->setContentsMargins(0, 0, 0, 0);
        root = g;
    }

    m_card = new QWidget(this);
    m_card->setObjectName(QStringLiteral("aboutCard")); // 与关于卡片同款：圆角深色卡
    if (!m_embedded)
        m_card->setFixedSize(1120, 700); // 右侧多一列展示 OCR 原始数据
    QVBoxLayout *cl = new QVBoxLayout(m_card);
    cl->setContentsMargins(26, 20, 26, 18);
    cl->setSpacing(10);

    QLabel *title = new QLabel(m_mode == Quantity
        ? QStringLiteral("数量录入（OCR）") : QStringLiteral("物料编码补充（OCR）"), m_card);
    title->setObjectName(QStringLiteral("aboutTitle"));
    title->setAlignment(Qt::AlignCenter);
    cl->addWidget(title);

    QLabel *info = new QLabel(m_mode == Quantity
        ? QStringLiteral("把「物料编码 / 物料描述 / 数量」表格截图拖到下面的拖放区（可多张，约 8 张），"
                         "点「开始识别」自动提取型号与数量。\n"
                         "按型号回填：库中没有的型号会跳过；数量只在为空或勾选覆盖时写入。")
        : QStringLiteral("把「物料编码 / 物料描述」表格截图拖到下面的拖放区（可多张，约 8 张），"
                         "点「开始识别」自动提取编码与型号。\n"
                         "按型号回填：库中没有的型号会跳过；编码为空才补充，已有编码默认不改动。"),
        m_card);
    info->setObjectName(QStringLiteral("aboutInfo"));
    info->setWordWrap(true);
    cl->addWidget(info);

    // 主体分左右两栏：左侧操作与结果表，右侧显示 OCR 识别到的全部原始数据（便于核对识别效果）
    QHBoxLayout *body = new QHBoxLayout();
    QVBoxLayout *left = new QVBoxLayout();
    left->setSpacing(10);

    // 拖放区（类似录入界面的图纸拖入，同时支持 Ctrl+V 粘贴截图）
    m_dropArea = new QLabel(QStringLiteral("把截图拖到这里，或点「选择图片…」、按 Ctrl+V 粘贴（可多张）"), m_card);
    m_dropArea->setObjectName(QStringLiteral("dropArea"));
    m_dropArea->setAlignment(Qt::AlignCenter);
    m_dropArea->setFixedHeight(76);
    m_dropArea->setStyleSheet(dropAreaStyle(false));
    left->addWidget(m_dropArea);

    // 图片列表 + 操作按钮
    QHBoxLayout *imgRow = new QHBoxLayout();
    m_imgList = new QListWidget(m_card);
    m_imgList->setObjectName(QStringLiteral("productTable"));
    m_imgList->setFixedHeight(110);
    imgRow->addWidget(m_imgList, 1);

    QVBoxLayout *imgBtns = new QVBoxLayout();
    QPushButton *btnPick = new QPushButton(QStringLiteral("选择图片…"), m_card);
    btnPick->setObjectName(QStringLiteral("btnEntry"));
    btnPick->setCursor(Qt::PointingHandCursor);
    QPushButton *btnRemove = new QPushButton(QStringLiteral("移除选中"), m_card);
    btnRemove->setObjectName(QStringLiteral("btnSearch"));
    btnRemove->setCursor(Qt::PointingHandCursor);
    QPushButton *btnClear = new QPushButton(QStringLiteral("清 空"), m_card);
    btnClear->setObjectName(QStringLiteral("btnSearch"));
    btnClear->setCursor(Qt::PointingHandCursor);
    imgBtns->addWidget(btnPick);
    imgBtns->addWidget(btnRemove);
    imgBtns->addWidget(btnClear);
    imgRow->addLayout(imgBtns);
    left->addLayout(imgRow);

    // 识别进度
    QHBoxLayout *runRow = new QHBoxLayout();
    m_btnOcr = new QPushButton(QStringLiteral("开始识别"), m_card);
    m_btnOcr->setObjectName(QStringLiteral("btnEntry"));
    m_btnOcr->setCursor(Qt::PointingHandCursor);
    m_progress = new QProgressBar(m_card);
    m_progress->setVisible(false);
    m_status = new QLabel(m_card);
    m_status->setObjectName(QStringLiteral("aboutInfo"));
    runRow->addWidget(m_btnOcr);
    runRow->addWidget(m_progress, 1);
    runRow->addWidget(m_status, 2);
    left->addLayout(runRow);

    // 结果表：数量模式同时展示「数量状态 / 编码状态」，让数量与物料编码都能录入
    const bool qtyModeTable = (m_mode == Quantity);
    const QStringList headers = qtyModeTable
        ? QStringList{QStringLiteral("序号"), QStringLiteral("型号"), QStringLiteral("数量"),
                      QStringLiteral("匹配产品"), QStringLiteral("数量状态"), QStringLiteral("编码状态")}
        : QStringList{QStringLiteral("序号"), QStringLiteral("物料编码"), QStringLiteral("型号"),
                      QStringLiteral("匹配产品"), QStringLiteral("状态")};
    m_table = new QTableWidget(0, headers.size(), m_card);
    m_table->setObjectName(QStringLiteral("productTable"));
    m_table->setHorizontalHeaderLabels(headers);
    m_table->verticalHeader()->setVisible(false);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents); // 序号
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch); // 匹配产品
    m_table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    if (qtyModeTable)
        m_table->horizontalHeader()->setSectionResizeMode(5, QHeaderView::ResizeToContents);
    left->addWidget(m_table, 1);

    body->addLayout(left, 1);

    // 右侧：OCR 原始数据（每张图识别出的全部文本，便于核对识别效果）
    QVBoxLayout *right = new QVBoxLayout();
    right->setSpacing(6);
    QLabel *rawTitle = new QLabel(QStringLiteral("OCR 识别原始数据"), m_card);
    rawTitle->setObjectName(QStringLiteral("aboutInfo"));
    right->addWidget(rawTitle);
    m_ocrRaw = new QPlainTextEdit(m_card);
    m_ocrRaw->setObjectName(QStringLiteral("productTable"));
    m_ocrRaw->setReadOnly(true);
    m_ocrRaw->setLineWrapMode(QPlainTextEdit::NoWrap); // 保持原始分行，便于对照
    m_ocrRaw->setFixedWidth(380);
    QFont mono = m_ocrRaw->font();
    mono.setFamily(QStringLiteral("Consolas"));
    m_ocrRaw->setFont(mono);
    m_ocrRaw->setPlaceholderText(QStringLiteral("识别后这里显示每张图识别出的全部原始文本"));
    right->addWidget(m_ocrRaw, 1);
    body->addLayout(right);

    cl->addLayout(body, 1);

    // 底部
    QHBoxLayout *foot = new QHBoxLayout();
    const bool qtyModeFoot = (m_mode == Quantity);
    m_chkOverwrite = new QCheckBox(qtyModeFoot
        ? QStringLiteral("覆盖已有数量") : QStringLiteral("覆盖已有编码"), m_card);
    m_chkOverwrite->setCursor(Qt::PointingHandCursor);
    m_chkOverwrite->setToolTip(qtyModeFoot
        ? QStringLiteral("默认勾选：把已有数量也改成识别结果")
        : QStringLiteral("默认不勾选：只补充空编码；勾选后把已有编码也改成识别结果（位数识别错误的除外）"));
    m_chkOverwrite->setChecked(qtyModeFoot); // 覆盖数量默认勾选，覆盖编码默认不勾选
    foot->addWidget(m_chkOverwrite);

    if (qtyModeFoot) {
        // 数量录入额外支持「覆盖已有编码」，并记录录入日期（供用量统计）
        m_chkOverwriteCode = new QCheckBox(QStringLiteral("覆盖已有编码"), m_card);
        m_chkOverwriteCode->setCursor(Qt::PointingHandCursor);
        m_chkOverwriteCode->setToolTip(QStringLiteral("默认不勾选：只补充空编码；勾选后把已有编码也改成识别结果（位数识别错误的除外）"));
        m_chkOverwriteCode->setChecked(false);
        foot->addWidget(m_chkOverwriteCode);
        foot->addSpacing(10);
        QLabel *dateLb = new QLabel(QStringLiteral("数量日期"), m_card);
        dateLb->setObjectName(QStringLiteral("aboutInfo"));
        m_qtyDate = new QDateEdit(QDate::currentDate(), m_card);
        m_qtyDate->setCalendarPopup(true);
        m_qtyDate->setDisplayFormat(QStringLiteral("yyyy-MM-dd"));
        m_qtyDate->setToolTip(QStringLiteral("本次录入的数量按这个日期记录，用于后续日/周/月/年用量统计"));
        foot->addWidget(dateLb);
        foot->addWidget(m_qtyDate);
        // 换了日期就按新日期重新判定「数量状态」（是否已录入 / 是否一致）
        connect(m_qtyDate, &QDateEdit::dateChanged, this, [this](const QDate &) {
            rebuildTable();
        });
    }

    m_btnApply = new QPushButton(QStringLiteral("应用到数据库"), m_card);
    m_btnApply->setObjectName(QStringLiteral("btnEntry"));
    m_btnApply->setCursor(Qt::PointingHandCursor);
    m_btnApply->setEnabled(false);
    foot->addStretch();
    foot->addWidget(m_btnApply);

    // 数量录入不需要关闭按钮；物料编码补充（覆盖式面板）才需要
    if (!m_embedded && !qtyModeFoot) {
        QPushButton *btnClose = new QPushButton(QStringLiteral("关 闭"), m_card);
        btnClose->setObjectName(QStringLiteral("btnSearch"));
        btnClose->setCursor(Qt::PointingHandCursor);
        foot->addWidget(btnClose);
        connect(btnClose, &QPushButton::clicked, this, &MaterialCodeDialog::closePanel);
    }
    cl->addLayout(foot);

    if (m_embedded)
        root->addWidget(m_card);
    else
        static_cast<QGridLayout *>(root)->addWidget(m_card, 0, 0, Qt::AlignCenter);

    connect(btnPick, &QPushButton::clicked, this, &MaterialCodeDialog::onPickImages);
    connect(btnRemove, &QPushButton::clicked, this, &MaterialCodeDialog::onRemoveSelected);
    connect(btnClear, &QPushButton::clicked, this, &MaterialCodeDialog::onClear);
    connect(m_btnOcr, &QPushButton::clicked, this, &MaterialCodeDialog::onStartOcr);
    connect(m_btnApply, &QPushButton::clicked, this, &MaterialCodeDialog::onApply);

    // Ctrl+V 粘贴剪贴板截图（与录入界面一致）；内联嵌入时始终可用，覆盖面板在 showPanel 时启用
    m_pasteShortcut = new QShortcut(QKeySequence::Paste, this);
    m_pasteShortcut->setContext(Qt::WindowShortcut);
    m_pasteShortcut->setEnabled(m_embedded);
    connect(m_pasteShortcut, &QShortcut::activated, this, &MaterialCodeDialog::pasteFromClipboard);
}

void MaterialCodeDialog::showPanel()
{
    if (m_embedded)
        return; // 内联嵌入模式由父布局管理，不覆盖父窗口
    if (parentWidget()) {
        resize(parentWidget()->size());
        // 窗口较小时把卡片收进可视范围，避免右侧原始数据被裁掉
        m_card->setFixedSize(qMin(1120, width() - 40), qMin(700, height() - 40));
    }
    raise();
    show();
    if (m_pasteShortcut)
        m_pasteShortcut->setEnabled(true);
}

void MaterialCodeDialog::closePanel()
{
    if (m_embedded) { // 内联嵌入时不销毁/隐藏自身，仅通知调用方
        emit closed();
        return;
    }
    hide();
    if (m_pasteShortcut)
        m_pasteShortcut->setEnabled(false);
    emit closed();
}

void MaterialCodeDialog::setPasteEnabled(bool on)
{
    if (m_pasteShortcut)
        m_pasteShortcut->setEnabled(on);
}

// 点遮罩空白处关闭（点到卡片上不触发）
bool MaterialCodeDialog::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == this && event->type() == QEvent::MouseButtonRelease) {
        auto *me = static_cast<QMouseEvent *>(event);
        if (m_card && !m_card->geometry().contains(me->pos()))
            closePanel();
        return true;
    }
    return QWidget::eventFilter(watched, event);
}

void MaterialCodeDialog::highlightDropArea(bool on)
{
    if (m_dropArea)
        m_dropArea->setStyleSheet(dropAreaStyle(on));
}

void MaterialCodeDialog::dragEnterEvent(QDragEnterEvent *event)
{
    if (!event->mimeData()->hasUrls())
        return;
    for (const QUrl &u : event->mimeData()->urls()) {
        if (isImageFile(u.toLocalFile())) {
            event->acceptProposedAction();
            highlightDropArea(true); // 拖入时虚线框变绿，给出落点提示
            return;
        }
    }
}

void MaterialCodeDialog::dragLeaveEvent(QDragLeaveEvent *event)
{
    highlightDropArea(false);
    QWidget::dragLeaveEvent(event);
}

void MaterialCodeDialog::dropEvent(QDropEvent *event)
{
    highlightDropArea(false);
    QStringList paths;
    for (const QUrl &u : event->mimeData()->urls()) {
        const QString p = u.toLocalFile();
        if (QFileInfo::exists(p) && isImageFile(p))
            paths << p;
    }
    if (paths.isEmpty())
        return;
    event->acceptProposedAction();
    addImages(paths);
}

void MaterialCodeDialog::addImages(const QStringList &paths)
{
    for (const QString &p : paths) {
        if (!m_images.contains(p))
            m_images << p;
    }
    refreshImageList();
}

void MaterialCodeDialog::refreshImageList()
{
    m_imgList->clear();
    for (const QString &p : m_images) {
        auto *it = new QListWidgetItem(QFileInfo(p).fileName(), m_imgList);
        it->setData(Qt::UserRole, p);
        it->setToolTip(p);
    }
}

void MaterialCodeDialog::onPickImages()
{
    const QStringList paths = QFileDialog::getOpenFileNames(
        this, QStringLiteral("选择表格截图（可多选）"), QString(),
        QStringLiteral("图片 (*.jpg *.jpeg *.png *.bmp)"));
    if (paths.isEmpty())
        return;
    addImages(paths);
}

// Ctrl+V：粘贴剪贴板里的截图（截图工具/QQ/微信复制的图都可），
// 与录入界面一样先落盘到临时目录再加入列表，便于后续统一走 OCR。
void MaterialCodeDialog::pasteFromClipboard()
{
    const QMimeData *mime = QGuiApplication::clipboard()->mimeData();
    if (!mime)
        return;

    // 1. 剪贴板是图片（截图 / 复制的图）
    // PNG 无损保存：剪贴板位图不再经 JPEG 二次压缩，细笔画字符（L/1、X/x、0/O）更稳
    if (mime->hasImage()) {
        const QImage img = qvariant_cast<QImage>(mime->imageData());
        if (!img.isNull()) {
            const QString path = QDir::tempPath() + QStringLiteral("/nd_mcode_%1.png")
                .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss_zzz")));
            if (img.save(path, "PNG")) {
                addImages({path});
                m_status->setText(QStringLiteral("已粘贴 1 张截图"));
                return;
            }
        }
    }
    // 2. 剪贴板是文件（资源管理器里复制的图片文件，可一次多张）
    if (mime->hasUrls()) {
        QStringList paths;
        for (const QUrl &u : mime->urls()) {
            const QString p = u.toLocalFile();
            if (QFileInfo::exists(p) && isImageFile(p) && !paths.contains(p))
                paths << p;
        }
        if (!paths.isEmpty()) {
            addImages(paths);
            m_status->setText(QStringLiteral("已粘贴 %1 张图片").arg(paths.size()));
            return;
        }
    }
    m_status->setText(QStringLiteral("剪贴板中没有图片"));
}

void MaterialCodeDialog::onRemoveSelected()
{
    const auto items = m_imgList->selectedItems();
    for (const QListWidgetItem *it : items)
        m_images.removeAll(it->data(Qt::UserRole).toString());
    refreshImageList();
}

void MaterialCodeDialog::onClear()
{
    m_images.clear();
    m_rows.clear();
    refreshImageList();
    m_table->setRowCount(0);
    m_ocrRaw->clear();
    m_btnApply->setEnabled(false);
    m_status->clear();
}

bool MaterialCodeDialog::runOcr(const QString &path, QString *text)
{
    const QString exe = ocrEnginePath();
    if (exe.isEmpty())
        return false;
    QProcess proc;
    proc.setWorkingDirectory(QFileInfo(exe).absolutePath()); // 引擎按相对路径找模型
    proc.start(exe, {QStringLiteral("-image_path=") + QDir::toNativeSeparators(path),
                     QStringLiteral("-ensure_ascii=false")});
    if (!proc.waitForStarted(15000))
        return false;
    if (!proc.waitForFinished(300000)) {
        proc.kill();
        proc.waitForFinished(3000);
        return false;
    }
    return parseOcrJsonText(proc.readAllStandardOutput(), text);
}

// 从识别文本中提取「物料编码 + 型号」：先按内容把文字块分成 5 类，再按序号配对。
int MaterialCodeDialog::parseAndAppend(const QString &text)
{
    static const QRegularExpression codeOnly(QStringLiteral("^\\d{10,14}$"));
    static const QRegularExpression anyCode(QStringLiteral("(?<!\\d)(\\d{10,14})(?!\\d)"));
    static const QRegularExpression quantity(QStringLiteral("^\\d+(?:\\.\\d+)?$")); // 数量/序号
    static const QRegularExpression chinese(QStringLiteral("[\\x{4e00}-\\x{9fff}]"));

    struct Desc {
        QString model;
        QString raw;
        bool pureAscii = false;
        bool incomplete = false; // 截图被截断，型号可能没识别全
    };
    QStringList codes;
    QList<Desc> descs;
    QList<int> qtys;            // 数量模式：收集每行的数量（小数形式），按序与型号配对
    QList<int> seqs;            // 截图里的序号列，按出现顺序与配对结果对应，供人工核对
    QStringList normalizedNotes; // 归一化前后对照，便于核对中文括号等 OCR 噪声

    // OCR 按文字块的 y 坐标排序，表格各列高度不一致时会把一行的 5 个字段打散成交错输出，
    // 所以不能靠「相邻行」配对。改为先把文字块分成 5 类（序号/客户名称/物料编码/物料描述/数量），
    // 再按「第 N 个物料编码 ↔ 第 N 个物料描述」配对 —— 同一张图里这个次序是稳定的。
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (const QString &raw : lines) {
        const QString line = normalizeOcrLine(raw);
        if (line.isEmpty())
            continue;
        const bool truncated = hasTruncationMark(raw);
        if (line != raw.trimmed() && normalizedNotes.size() < 40)
            normalizedNotes << QStringLiteral("  原始：%1\n  归一：%2").arg(raw.trimmed(), line);

        // 表头文字（含 OCR 噪声，如「物料描述Y」）直接忽略
        bool isHeader = false;
        for (const QString &h : {QStringLiteral("物料编码"), QStringLiteral("物料描述"),
                                 QStringLiteral("数量"), QStringLiteral("客户名称"),
                                 QStringLiteral("序号")}) {
            if (line.startsWith(h) && line.size() <= h.size() + 3) {
                isHeader = true;
                break;
            }
        }
        if (isHeader)
            continue;

        // 1) 物料编码：10~14 位纯数字
        if (codeOnly.match(line).hasMatch()) {
            codes << line;
            continue;
        }
        // 2) 数量（780.0 / 1355.0）与序号（121~135）等纯数字
        if (quantity.match(line).hasMatch()) {
            // 数量模式：收集小数形式的数量（如 780.0），按行序与型号配对（精度后续再优化）
            if (m_mode == Quantity) {
                static const QRegularExpression decQty(QStringLiteral("^\\d+\\.\\d+$"));
                if (decQty.match(line).hasMatch())
                    qtys.append(qRound(line.toDouble()));
            }
            // 序号列是短的纯整数：物料编码（10~14 位）已在上面收走，
            // 纯数字型号（148000201 等）位数也远超序号，用 1~4 位把它们排除。
            static const QRegularExpression seqOnly(QStringLiteral("^\\d{1,4}$"));
            if (seqOnly.match(line).hasMatch()) {
                const int v = line.toInt();
                if (v > 0)
                    seqs.append(v);
            }
            continue;
        }
        // 3) 表头噪声 / 客户名称：取不到型号的直接忽略
        const QString model = extractModel(line);
        if (model.isEmpty())
            continue;
        const bool hasChinese = chinese.match(line).hasMatch();

        // 4) 编码与描述被 OCR 合并到同一行：一次收下两者
        const auto cm = anyCode.match(line);
        if (cm.hasMatch()) {
            codes << cm.captured(1);
            const QString rest = line.mid(cm.capturedEnd(1));
            const QString restModel = extractModel(rest);
            if (!restModel.isEmpty())
                descs.append({restModel, rest.trimmed(), !hasChinese, truncated});
            continue;
        }
        // 5) 物料描述：含中文 + 行尾型号；或纯 ASCII 的「大写产品名+型号」
        //    （如 TCULF2204E-48-0921，原图是 "TCU LF2204E-48-0921"，空格被 OCR 吃掉）
        //    截图被截断的行也要收下（型号可能不完整），否则后面的编码会整体错位。
        descs.append({model, line, !hasChinese, truncated});
    }

    if (!normalizedNotes.isEmpty()) {
        m_ocrRaw->appendPlainText(QStringLiteral("【行归一化】中文括号 / 小写 u / 截断噪声已统一成半角 (U)"));
        for (const QString &s : normalizedNotes)
            m_ocrRaw->appendPlainText(s);
    }

    // 原表格里同一产品会连续占两行、各带一个数量：例如每天都有两条
    // 「旋转手油门 MFZ554-48-037B」（216.0 与 2500.0），两行物料编码完全相同。
    // 按「第 N 个编码 ↔ 第 N 个描述」配对时，多出来的编码会让后面所有组整体错位，
    // 数量串到别的产品上。这里先按编码聚合：数量累加、序号取首次出现的，
    // 描述里连续重复的型号也只保留一条，让两边次序重新对齐。
    QStringList mergedCodes;
    QList<int> mergedQtys, mergedSeqs;
    QStringList mergeNotes;
    for (int i = 0; i < codes.size(); ++i) {
        int p = -1;
        for (int j = 0; j < mergedCodes.size(); ++j) {
            if (mergedCodes.at(j).compare(codes.at(i), Qt::CaseInsensitive) == 0) {
                p = j;
                break;
            }
        }
        if (p < 0) {
            mergedCodes << codes.at(i);
            mergedQtys << qtys.value(i, 0);
            mergedSeqs << seqs.value(i, 0);
            continue;
        }
        const int add = qtys.value(i, 0);
        if (add > 0) {
            const int before = mergedQtys.at(p);
            mergedQtys[p] = before + add;
            mergeNotes << QStringLiteral("  编码 %1：%2 + %3 = %4")
                            .arg(codes.at(i)).arg(before).arg(add).arg(before + add);
        }
    }
    codes = mergedCodes;
    qtys = mergedQtys;
    seqs = mergedSeqs;
    QList<Desc> mergedDescs;
    for (const Desc &d : descs) {
        if (!mergedDescs.isEmpty()
            && mergedDescs.last().model.compare(d.model, Qt::CaseInsensitive) == 0)
            continue;
        mergedDescs << d;
    }
    descs = mergedDescs;

    const int n = qMin(codes.size(), descs.size());
    // 把配对结果也写进右侧原始数据区，方便核对识别/解析是否对得上
    m_ocrRaw->appendPlainText(QStringLiteral("【配对结果】编码 %1 条 / 描述 %2 条 → 成功 %3 组")
                                  .arg(codes.size()).arg(descs.size()).arg(n));
    if (!mergeNotes.isEmpty()) {
        m_ocrRaw->appendPlainText(QStringLiteral("  【重复行合并】同一产品多行，数量已累加："));
        for (const QString &s : mergeNotes)
            m_ocrRaw->appendPlainText(s);
    }
    for (int i = 0; i < n; ++i) {
        const Desc &d = descs.at(i);
        QString note;
        // 纯 ASCII 行可能被 OCR 吃掉了「产品名 型号」之间的空格，拆回真实型号
        const QString model = d.pureAscii ? splitUpperPrefix(d.model, &note) : d.model;
        if (d.incomplete)
            note += QStringLiteral("　【截图截断】型号可能不完整，需人工核对");
        const int qty = qtys.value(i, 0);
        const int seq = seqs.value(i, 0);
        m_rows.append({codes.at(i), model, QString(), -1, d.incomplete, QString(), qty, seq});
        const QString seqTag = seq > 0 ? QStringLiteral("[%1] ").arg(seq) : QString();
        m_ocrRaw->appendPlainText(QStringLiteral("  %1%2 → %3%4　（%5）%6")
                                      .arg(seqTag, codes.at(i), model,
                                           qty > 0 ? QStringLiteral(" ×%1").arg(qty) : QString(),
                                           d.raw, note));
    }
    const int unpaired = qAbs(codes.size() - descs.size());
    if (unpaired > 0)
        m_ocrRaw->appendPlainText(QStringLiteral("  ⚠ 有 %1 个编码/描述未能配对（可能漏识别）；"
                                                 "按序配对会让后面的型号与数量整体错位，请对照上方原始文本核对后再写入")
                                      .arg(unpaired));
    if (seqs.size() != n)
        m_ocrRaw->appendPlainText(QStringLiteral("  ⚠ 识别到序号 %1 个 / 配对 %2 组，表格序号可能漏识别或错位")
                                      .arg(seqs.size()).arg(n));
    m_ocrRaw->appendPlainText(QString());
    return unpaired;
}

void MaterialCodeDialog::rebuildTable()
{
    // 按「编码+型号」去重，避免多张图重复识别同一条
    QList<Row> uniq;
    QSet<QString> seen;
    for (const Row &r : m_rows) {
        const QString key = r.code + QLatin1Char('|') + r.model;
        if (seen.contains(key))
            continue;
        seen.insert(key);
        uniq << r;
    }
    m_rows = uniq;

    DatabaseManager &db = DatabaseManager::instance();
    m_table->setRowCount(m_rows.size());
    for (int i = 0; i < m_rows.size(); ++i) {
        Row &r = m_rows[i];
        // (U) 可能只是表格后缀、库里登记的是不带括号的型号，也可能库里带 (U)
        QString matchedModel;
        r.productId = lookupProductId(r.model, &matchedModel);
        // 型号被截图截断时匹配不上，改用识别到的物料编码反查（编码是独立的一列数字，通常完整）；
        // 反查还要求型号相关，否则不采用，避免把数量记到别的产品上
        r.byCode = false;
        QString rejectedModel;
        if (r.productId < 0 && !r.code.trimmed().isEmpty()) {
            QString codeModel;
            const int id = lookupProductByCode(r.code, r.model, &codeModel, &rejectedModel);
            if (id >= 0) {
                r.productId = id;
                matchedModel = codeModel;
                r.byCode = true;
            }
        }
        const QString rejectTip = rejectedModel.isEmpty() ? QString()
            : QStringLiteral("编码「%1」在库中对应型号「%2」，与识别型号差异过大，已跳过，不写入数据库")
                  .arg(r.code.trimmed(), rejectedModel);
        QString info;
        const QString modelText = (!matchedModel.isEmpty() && matchedModel != r.model)
            ? r.model + QStringLiteral(" → ") + matchedModel : r.model;

        if (m_mode == Quantity) {
            // 数量状态
            QString qtyStatus;
            QColor qtyColor = QColor(0xB0, 0xB0, 0xB0);
            if (r.productId < 0) {
                qtyStatus = QStringLiteral("—");
            } else {
                // 以「数量日期」当天已录入的数量为基准判定，而不是产品当前总数量，
                // 否则选了个没录过数的日期也会拿今天的数据去比对，状态会误导。
                const QDate qtyDay = m_qtyDate ? m_qtyDate->date() : QDate::currentDate();
                const int dayQty = db.qtyLogForDate(r.productId, qtyDay);
                if (r.qty <= 0) {
                    qtyStatus = QStringLiteral("数量未识别");
                    qtyColor = QColor(0xE0, 0xA6, 0x4C);
                } else if (dayQty < 0) {
                    qtyStatus = QStringLiteral("待录入");
                    qtyColor = QColor(0x43, 0xCF, 0x7C);
                } else if (dayQty == r.qty) {
                    qtyStatus = QStringLiteral("数量一致");
                    qtyColor = QColor(0x43, 0xCF, 0x7C);
                } else {
                    qtyStatus = QStringLiteral("当日已录 %1").arg(dayQty);
                    qtyColor = QColor(0xE0, 0xA6, 0x4C);
                }
            }
            // 编码状态（与物料编码模式同口径）
            QString codeStatus;
            QColor codeColor = QColor(0xB0, 0xB0, 0xB0);
            QString cur;                  // 库中已登记的编码
            bool codeLengthError = false;
            if (r.productId < 0) {
                if (!rejectTip.isEmpty()) {
                    codeStatus = QStringLiteral("型号差异过大（跳过）");
                    codeColor = QColor(0xE0, 0x6C, 0x4C);
                } else {
                    codeStatus = r.incomplete ? QStringLiteral("型号不完整（跳过）")
                                              : QStringLiteral("未收录型号（跳过）");
                    if (r.incomplete)
                        codeColor = QColor(0xE0, 0x6C, 0x4C);
                }
            } else {
                const Product p = db.productById(r.productId);
                info = p.category + QLatin1Char('/') + p.name;
                cur = p.materialCode.trimmed();
                if (cur.isEmpty()) {
                    codeStatus = QStringLiteral("待补充");
                    codeColor = QColor(0x43, 0xCF, 0x7C);
                } else if (r.code.isEmpty()) {
                    codeStatus = QStringLiteral("截图为空");
                    codeColor = QColor(0xE0, 0xA6, 0x4C);
                } else if (cur.compare(r.code, Qt::CaseInsensitive) == 0) {
                    codeStatus = r.byCode ? QStringLiteral("编码一致（按编码匹配）")
                                          : QStringLiteral("编码一致");
                    codeColor = QColor(0x43, 0xCF, 0x7C);
                } else if (isOcrCodeLengthError(r.code, cur)) {
                    codeStatus = QStringLiteral("编码识别错误");
                    codeColor = QColor(0xE0, 0x6C, 0x4C);
                    codeLengthError = true;
                } else {
                    codeStatus = QStringLiteral("已有编码：%1").arg(cur);
                    codeColor = QColor(0xE0, 0xA6, 0x4C);
                }
                // 已按编码可靠反查到产品时，不再追加「截图截断」的警告
                if (r.incomplete && !r.byCode) {
                    codeStatus.append(QStringLiteral("（截图截断）"));
                    if (!codeLengthError)
                        codeColor = QColor(0xE0, 0xA6, 0x4C);
                }
            }
            auto *cSeq = new QTableWidgetItem(r.seq > 0 ? QString::number(r.seq)
                                                        : QStringLiteral("—"));
            auto *c0 = new QTableWidgetItem(modelText);
            if (r.byCode)
                c0->setToolTip(QStringLiteral("型号未匹配，按物料编码「%1」反查到库中型号「%2」")
                                   .arg(r.code.trimmed(), matchedModel));
            else if (modelText != r.model)
                c0->setToolTip(QStringLiteral("按库中型号「%1」匹配").arg(matchedModel));
            auto *c1 = new QTableWidgetItem(QString::number(r.qty));
            auto *c2 = new QTableWidgetItem(info);
            auto *c3 = new QTableWidgetItem(qtyStatus);
            c3->setForeground(qtyColor);
            auto *c4 = new QTableWidgetItem(codeStatus);
            c4->setForeground(codeColor);
            if (!r.code.isEmpty()) {
                QString tip = QStringLiteral("识别编码：%1").arg(r.code);
                if (codeLengthError)
                    tip += QStringLiteral("\n库中已有正确编码：%1（%2 位，识别结果 %3 位，不会写入）")
                               .arg(cur).arg(cur.size()).arg(r.code.size());
                if (!rejectTip.isEmpty())
                    tip += QStringLiteral("\n") + rejectTip;
                c4->setToolTip(tip);
            }
            m_table->setItem(i, 0, cSeq);
            m_table->setItem(i, 1, c0);
            m_table->setItem(i, 2, c1);
            m_table->setItem(i, 3, c2);
            m_table->setItem(i, 4, c3);
            m_table->setItem(i, 5, c4);
            continue;
        }

        // ---- 物料编码模式 ----
        QString status;
        QColor statusColor = QColor(0xB0, 0xB0, 0xB0);
        QString cur;                  // 库中已登记的编码
        bool codeLengthError = false;
        if (r.productId < 0) {
            if (!rejectTip.isEmpty()) {
                status = QStringLiteral("型号差异过大（跳过）");
                statusColor = QColor(0xE0, 0x6C, 0x4C);
            } else {
                status = r.incomplete ? QStringLiteral("型号不完整（跳过）")
                                      : QStringLiteral("未收录型号（跳过）");
                if (r.incomplete)
                    statusColor = QColor(0xE0, 0x6C, 0x4C);
            }
        } else {
            const Product p = db.productById(r.productId);
            info = p.category + QLatin1Char('/') + p.name;
            cur = p.materialCode.trimmed();
            if (cur.isEmpty()) {
                status = QStringLiteral("待补充");
                statusColor = QColor(0x43, 0xCF, 0x7C);
            } else if (cur.compare(r.code, Qt::CaseInsensitive) == 0) {
                status = r.byCode ? QStringLiteral("编码一致（按编码匹配）")
                                  : QStringLiteral("编码一致");
                statusColor = QColor(0x43, 0xCF, 0x7C);
            } else if (isOcrCodeLengthError(r.code, cur)) {
                status = QStringLiteral("编码识别错误");
                statusColor = QColor(0xE0, 0x6C, 0x4C);
                codeLengthError = true;
            } else {
                status = QStringLiteral("已有编码：%1").arg(cur);
                statusColor = QColor(0xE0, 0xA6, 0x4C);
            }
            if (r.incomplete && !r.byCode) {
                status.append(QStringLiteral("（截图截断，请核对）"));
                if (!codeLengthError)
                    statusColor = QColor(0xE0, 0xA6, 0x4C);
            }
        }
        r.status = status;

        auto *cSeq = new QTableWidgetItem(r.seq > 0 ? QString::number(r.seq)
                                                    : QStringLiteral("—"));
        auto *c0 = new QTableWidgetItem(r.code);
        auto *c1 = new QTableWidgetItem(modelText);
        if (r.byCode)
            c1->setToolTip(QStringLiteral("型号未匹配，按物料编码「%1」反查到库中型号「%2」")
                               .arg(r.code.trimmed(), matchedModel));
        else if (modelText != r.model)
            c1->setToolTip(QStringLiteral("按库中型号「%1」匹配（(U) 后缀已归一化）").arg(matchedModel));
        auto *c2 = new QTableWidgetItem(info);
        auto *c3 = new QTableWidgetItem(status);
        c3->setForeground(statusColor);
        if (codeLengthError)
            c3->setToolTip(QStringLiteral("库中已有正确编码：%1（%2 位，识别结果 %3 位，不会写入）")
                               .arg(cur).arg(cur.size()).arg(r.code.size()));
        else if (!rejectTip.isEmpty())
            c3->setToolTip(rejectTip);
        m_table->setItem(i, 0, cSeq);
        m_table->setItem(i, 1, c0);
        m_table->setItem(i, 2, c1);
        m_table->setItem(i, 3, c2);
        m_table->setItem(i, 4, c3);
    }
    m_table->resizeRowsToContents();
}

void MaterialCodeDialog::onStartOcr()
{
    if (m_images.isEmpty()) {
        NdMessageBox::information(this, QStringLiteral("提示"),
                                  QStringLiteral("请先选择或拖入表格截图"));
        return;
    }
    if (ocrEnginePath().isEmpty()) {
        NdMessageBox::warning(this, QStringLiteral("识别失败"),
                              QStringLiteral("未找到离线识别引擎：ocr/PaddleOCR-json/PaddleOCR-json.exe"));
        return;
    }

    m_rows.clear();
    m_table->setRowCount(0);
    m_ocrRaw->clear(); // 右侧原始数据一并清空
    m_btnApply->setEnabled(false);
    m_btnOcr->setEnabled(false);
    m_progress->setVisible(true);
    m_progress->setRange(0, m_images.size());
    m_progress->setValue(0);

    int failed = 0, unpaired = 0;
    for (int i = 0; i < m_images.size(); ++i) {
        m_status->setText(QStringLiteral("正在识别 %1/%2：%3")
                              .arg(i + 1).arg(m_images.size())
                              .arg(QFileInfo(m_images.at(i)).fileName()));
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);

        QString text;
        const bool ok = runOcr(m_images.at(i), &text);

        // 右侧原始数据：每张图一段，原样展示识别出的全部文本，便于核对识别效果
        m_ocrRaw->appendPlainText(QStringLiteral("【%1/%2】%3")
                                      .arg(i + 1).arg(m_images.size())
                                      .arg(QFileInfo(m_images.at(i)).fileName()));
        m_ocrRaw->appendPlainText(ok ? (text.isEmpty() ? QStringLiteral("（未识别到文字）") : text)
                                     : QStringLiteral("（识别失败）"));
        m_ocrRaw->appendPlainText(QStringLiteral("----------------------------------------"));
        m_ocrRaw->appendPlainText(QString());

        if (ok)
            unpaired += parseAndAppend(text); // 配对结果紧随原始文本显示
        else
            ++failed;

        m_progress->setValue(i + 1);
        rebuildTable();
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    }

    m_btnOcr->setEnabled(true);
    m_btnApply->setEnabled(!m_rows.isEmpty());

    int matched = 0;
    for (const Row &r : m_rows)
        if (r.productId >= 0)
            ++matched;
    QStringList tails;
    if (failed > 0) tails << QStringLiteral("%1 张图片识别失败").arg(failed);
    if (unpaired > 0) tails << QStringLiteral("%1 个字段未能配对").arg(unpaired);
    m_status->setText(QStringLiteral("识别完成：共 %1 条，其中 %2 条型号已收录%3")
                          .arg(m_rows.size()).arg(matched)
                          .arg(tails.isEmpty() ? QString()
                                               : QStringLiteral("（%1）").arg(tails.join(QStringLiteral("、")))));
}

void MaterialCodeDialog::onApply()
{
    if (m_rows.isEmpty())
        return;
    DatabaseManager &db = DatabaseManager::instance();
    const bool overwrite = m_chkOverwrite->isChecked();

    // ---- 数量模式：数量 + 物料编码一起回填（仅已收录型号）----
    if (m_mode == Quantity) {
        const bool overwriteCode = m_chkOverwriteCode && m_chkOverwriteCode->isChecked();
        const QDate logDate = m_qtyDate ? m_qtyDate->date() : QDate::currentDate();
        const QDate today = QDate::currentDate();
        const QString dayText = logDate.toString(QStringLiteral("yyyy-MM-dd"));
        const QString todayText = today.toString(QStringLiteral("yyyy-MM-dd"));
        // 未来日期会在日/周/月/年用量统计里凭空多出记录，属于脏数据，直接禁止保存
        if (logDate > today) {
            NdMessageBox::warning(this, QStringLiteral("选择时间不合理"),
                                  QStringLiteral("数量日期 %1 晚于今天（%2），不能录入未来日期的数据。\n"
                                                 "请改成今天或之前的日期后再应用。")
                                      .arg(dayText, todayText));
            return;
        }
        // 补录历史数据同样会改变用量统计口径，保存前先确认
        if (logDate < today
            && !NdMessageBox::question(this, QStringLiteral("非本日数据"),
                                       QStringLiteral("当前数量日期是 %1，不是今天（%2）。\n"
                                                      "确认按该日期保存数量吗？")
                                           .arg(dayText, todayText))) {
            return;
        }
        int qtyUpdated = 0, codeUpdated = 0, skipped = 0, fail = 0, incomplete = 0, codeErr = 0;
        int byCodeMatched = 0;
        for (const Row &r : m_rows) {
            if (r.byCode)
                ++byCodeMatched;
            if (r.incomplete)
                ++incomplete;
            if (r.productId < 0) {
                ++skipped; // 库中未收录该型号
                continue;
            }
            const Product p = db.productById(r.productId);
            bool didSth = false;
            // 数量：为空直接写；已有数量需勾选「覆盖已有数量」（默认已勾选）
            if (r.qty > 0 && (p.qty == 0 || overwrite)) {
                QString err;
                if (db.updateQuantities(r.productId, r.qty, p.minQty, p.maxQty, &err)) {
                    ++qtyUpdated;
                    didSth = true;
                    // 按「所选日期」判断是否已录过这个数量：同一天重复应用不重复记，
                    // 换到新日期即使数量相同也会各记一条，保证每日都有记录。
                    if (db.qtyLogForDate(r.productId, logDate) != r.qty)
                        db.addQtyLog(r.productId, r.qty, logDate, nullptr);
                } else {
                    ++fail;
                }
            }
            // 物料编码：为空直接补；已有编码需勾选「覆盖已有编码」（默认未勾选）。
            // 识别位数与库中编码不符的结果一律不写入，数量仍照常录入。
            const QString code = r.code.trimmed();
            if (!code.isEmpty()) {
                const QString cur = p.materialCode.trimmed();
                if (isOcrCodeLengthError(code, cur)) {
                    ++codeErr;
                } else if (cur.isEmpty()
                           || (overwriteCode && cur.compare(code, Qt::CaseInsensitive) != 0)) {
                    QString err;
                    if (db.updateMaterialCode(r.productId, code, &err)) {
                        ++codeUpdated;
                        didSth = true;
                    } else {
                        ++fail;
                    }
                }
            }
            if (!didSth)
                ++skipped;
        }
        rebuildTable();
        const QString incompleteTip = incomplete > 0
            ? QStringLiteral("\n其中 %1 条来自截图截断，请对照结果表核对。").arg(incomplete)
            : QString();
        const QString byCodeTip = byCodeMatched > 0
            ? QStringLiteral("\n其中 %1 条型号不完整，已按物料编码反查到产品并正常录入数量。").arg(byCodeMatched)
            : QString();
        const QString codeErrTip = codeErr > 0
            ? QStringLiteral("\n其中 %1 条编码位数识别错误，已跳过，库中编码保持不变。").arg(codeErr)
            : QString();
        NdMessageBox::information(this, QStringLiteral("录入完成"),
                                  QStringLiteral("更新数量 %1 条、更新编码 %2 条，跳过 %3 条"
                                                 "（未收录型号、数量为空或已有值未勾选覆盖），失败 %4 条")
                                      .arg(qtyUpdated).arg(codeUpdated)
                                      .arg(skipped).arg(fail)
                                  + byCodeTip + codeErrTip + incompleteTip);
        return;
    }

    // ---- 物料编码模式（原有逻辑）----
    int filled = 0, updated = 0, same = 0, skipped = 0, fail = 0, incomplete = 0, codeErr = 0;
    int byCodeMatched = 0;
    for (const Row &r : m_rows) {
        if (r.byCode)
            ++byCodeMatched;
        if (r.incomplete)
            ++incomplete;
        if (r.productId < 0) {
            ++skipped; // 库中没有该型号 / 截图截断导致型号不完整
            continue;
        }
        const Product p = db.productById(r.productId);
        const QString cur = p.materialCode.trimmed();
        // 识别编码位数与库中编码不符 = OCR 识别错误，即使勾选「覆盖已有编码」也不写入
        if (isOcrCodeLengthError(r.code.trimmed(), cur)) {
            ++codeErr;
            continue;
        }
        if (cur.compare(r.code, Qt::CaseInsensitive) == 0) {
            ++same;
            continue;
        }
        if (!cur.isEmpty() && !overwrite) {
            ++skipped; // 已有编码且未勾选覆盖
            continue;
        }
        QString err;
        if (db.updateMaterialCode(r.productId, r.code, &err)) {
            if (cur.isEmpty()) ++filled;
            else ++updated;
        } else {
            ++fail;
        }
    }

    rebuildTable(); // 重新查询，状态随之更新
    const QString incompleteTip = incomplete > 0
        ? QStringLiteral("\n其中 %1 条来自截图截断，请对照结果表核对。").arg(incomplete)
        : QString();
    const QString byCodeTip = byCodeMatched > 0
        ? QStringLiteral("\n其中 %1 条型号不完整，已按物料编码反查到产品。").arg(byCodeMatched)
        : QString();
    const QString codeErrTip = codeErr > 0
        ? QStringLiteral("\n其中 %1 条编码位数识别错误，已跳过，库中编码保持不变。").arg(codeErr)
        : QString();
    NdMessageBox::information(this, QStringLiteral("补充完成"),
                              QStringLiteral("新增编码 %1 条，更新 %2 条，已一致 %3 条，"
                                             "跳过 %4 条（未收录/不完整型号或已有编码），失败 %5 条")
                                  .arg(filled).arg(updated).arg(same).arg(skipped).arg(fail)
                              + byCodeTip + codeErrTip + incompleteTip);
}
