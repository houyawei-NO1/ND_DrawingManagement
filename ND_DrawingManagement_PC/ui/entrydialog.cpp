#include "entrydialog.h"
#include "db/databasemanager.h"
#include "image/heifconverter.h"
#include "materialcodedialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
#include <QDateEdit>
#include <QFileDialog>
#include "ndmessagebox.h"
#include "roundedmenu.h"
#include <QInputDialog>
#include <QFileInfo>
#include <QGroupBox>
#include <QScrollArea>
#include <QSizePolicy>
#include <QProgressBar>
#include <QPlainTextEdit>
#include <QTimer>
#include <QEventLoop>
#include <QDate>
#include <QResizeEvent>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QUrl>
#include <QPixmap>
#include <QButtonGroup>
#include <QClipboard>
#include <QGuiApplication>
#include <QShortcut>
#include <QKeySequence>
#include <QDateTime>
#include <QProcess>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QImageReader>
#include <QPainter>
#include <QRegularExpression>
#include <QApplication>
#include <QAbstractButton>
#include <QPdfDocument>
#include <QPdfDocumentRenderOptions>
#include <QPdfView>
#include <QStackedWidget>
#include <QListWidget>
#include <QListView>
#include <QGridLayout>
#include <QFrame>
#include <QIcon>
#include <QStyle>
#include <QMenu>
#include <QAction>
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QStringList>
#include <algorithm>

// 读图并自动应用 EXIF 方向标记：手机拍的 JPEG 常带旋转标记，
// QImage 直读不处理该标记，预览方向会不对（系统看图软件则会自动转正）
static QImage loadImageOriented(const QString &path)
{
    QImageReader r(path);
    r.setAutoTransform(true);
    return r.read();
}

// 表单标签：必填项的红星
static QLabel *reqLabel(const QString &text, bool required)
{
    QLabel *lb = new QLabel(required
        ? QStringLiteral("%1 <span style=\"color:#E06C75;font-weight:bold;\">*</span>").arg(text)
        : text);
    lb->setTextFormat(Qt::RichText);
    return lb;
}

// 可作为图纸的扩展名。HEIC / HEIF 允许选择，但在 addPickedFiles 里会先转码成 JPG
static bool isDrawingFile(const QString &path)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    return suffix == QStringLiteral("jpg") || suffix == QStringLiteral("jpeg")
           || suffix == QStringLiteral("png") || suffix == QStringLiteral("bmp")
           || suffix == QStringLiteral("pdf")
           || HeifConverter::isHeifFile(path);
}

// 图纸右下角标题栏区域（约整图 1/4，起点取 45% 略放宽以防裁到边缘文字）
static QRect titleBlockRect(const QImage &img)
{
    const int x = qRound(img.width() * 0.45);
    const int y = qRound(img.height() * 0.45);
    return QRect(x, y, img.width() - x, img.height() - y);
}

// 生成缩略图：图片直接等比缩放，PDF 渲染首页并铺白底（透明底在深色主题下会发黑）
static QPixmap makeThumbPixmap(const QString &path)
{
    if (path.isEmpty()) return QPixmap();
    if (QFileInfo(path).suffix().compare(QStringLiteral("pdf"), Qt::CaseInsensitive) == 0) {
        QPdfDocument doc;
        if (doc.load(path) == QPdfDocument::Error::None && doc.pageCount() > 0) {
            const QSizeF ps = doc.pagePointSize(0);
            QSize target(84, 60);
            if (ps.width() > 0 && ps.height() > 0)
                target = ps.scaled(QSizeF(84, 60), Qt::KeepAspectRatio).toSize();
            const QImage img = doc.render(0, target);
            if (!img.isNull()) {
                QImage canvas(img.size(), QImage::Format_RGB32);
                canvas.fill(Qt::white);
                QPainter p(&canvas);
                p.drawImage(0, 0, img);
                p.end();
                return QPixmap::fromImage(canvas);
            }
        }
        return QPixmap();
    }
    const QImage img = loadImageOriented(path);
    if (img.isNull()) return QPixmap();
    return QPixmap::fromImage(img.scaled(QSize(84, 60), Qt::KeepAspectRatio,
                                         Qt::SmoothTransformation));
}

// 缩略图卡片：大按钮显示图纸并负责切换预览，右上角红色 × 负责删除这一张
class ThumbCard : public QFrame
{
public:
    ThumbCard(const QPixmap &pm, const QString &caption, const QString &tip, QWidget *parent)
        : QFrame(parent)
    {
        setFixedSize(94, 92);
        setStyleSheet(cardStyle(false));
        m_preview = new QPushButton(this);
        m_preview->setGeometry(5, 5, 84, 60);
        m_preview->setCursor(Qt::PointingHandCursor);
        m_preview->setToolTip(tip);
        m_preview->setIcon(QIcon(pm.isNull()
            ? style()->standardIcon(QStyle::SP_FileIcon).pixmap(QSize(40, 40)) : pm));
        m_preview->setIconSize(QSize(80, 56));
        m_preview->setStyleSheet(QStringLiteral(
            "QPushButton{background:#1A2032;border:none;border-radius:4px;}"
            "QPushButton:hover{background:#2A344C;}"));
        m_del = new QPushButton(QStringLiteral("×"), this);
        m_del->setGeometry(71, 1, 20, 20);
        m_del->setCursor(Qt::PointingHandCursor);
        m_del->setToolTip(QStringLiteral("删除这张图纸"));
        m_del->setStyleSheet(QStringLiteral(
            "QPushButton{background:#E06C75;color:#ffffff;border:1px solid #22293C;"
            "border-radius:10px;font:bold 15px;padding:0;}"
            "QPushButton:hover{background:#F07C85;}"));
        QLabel *cap = new QLabel(caption, this);
        cap->setGeometry(4, 67, 86, 20);
        cap->setAlignment(Qt::AlignCenter);
        cap->setToolTip(tip);
        cap->setStyleSheet(QStringLiteral(
            "QLabel{color:rgba(255,255,255,0.8);background:transparent;}"));
    }

    QPushButton *previewButton() const { return m_preview; }
    QPushButton *deleteButton() const { return m_del; }
    void setSelected(bool on) { setStyleSheet(cardStyle(on)); }

private:
    static QString cardStyle(bool selected)
    {
        return QStringLiteral("QFrame{background:#22293C;border:1px solid %1;border-radius:6px;}")
            .arg(selected ? QStringLiteral("#43CF7C") : QStringLiteral("#4A5468"));
    }

    QPushButton *m_preview = nullptr;
    QPushButton *m_del = nullptr;
};

// 进度条样式：与深色主题一致，默认 0..0 表示“处理中”的不确定进度
static void styleBar(QProgressBar *bar)
{
    bar->setTextVisible(false);
    bar->setFixedHeight(6);
    bar->setRange(0, 0);
    bar->setStyleSheet(QStringLiteral(
        "QProgressBar{background:#242B3E;border:1px solid #4A5468;border-radius:3px;}"
        "QProgressBar::chunk{background:#43CF7C;border-radius:2px;}"));
    bar->hide();
}

// 进度行：提示文字 + 进度条
static QHBoxLayout *makeBusyRow(QWidget *parent, QLabel **label, QProgressBar **bar)
{
    QHBoxLayout *row = new QHBoxLayout();
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(8);
    *label = new QLabel(QString(), parent);
    (*label)->setStyleSheet(QStringLiteral("color:rgba(255,255,255,0.65);"));
    (*label)->hide();
    *bar = new QProgressBar(parent);
    styleBar(*bar);
    row->addWidget(*label);
    row->addWidget(*bar, 1);
    return row;
}

EntryDialog::EntryDialog(QWidget *parent) : QWidget(parent)
{
    setWindowTitle(QStringLiteral("图纸录入"));
    setMinimumWidth(620);
    setFocusPolicy(Qt::StrongFocus); // 本页可获取焦点，Ctrl+V 首次进入即可用

    QVBoxLayout *root = new QVBoxLayout(this);
    root->setContentsMargins(16, 16, 16, 16);
    root->setSpacing(12);

    // 顶部两个录入 Tab（与首页分类标签同款风格）
    QWidget *tabBar = new QWidget(this);
    QHBoxLayout *tabLay = new QHBoxLayout(tabBar);
    tabLay->setContentsMargins(0, 0, 0, 0);
    tabLay->setSpacing(8);
    m_tabGroup = new QButtonGroup(tabBar);
    m_tabGroup->setExclusive(true);
    const QStringList tabNames = {QStringLiteral("产品录入"), QStringLiteral("数量录入")};
    for (const QString &name : tabNames) {
        QPushButton *b = new QPushButton(name, tabBar);
        b->setObjectName(QStringLiteral("catTab"));
        b->setCheckable(true);
        b->setCursor(Qt::PointingHandCursor);
        b->setFocusPolicy(Qt::NoFocus); // 去掉点击后的焦点虚线/灰边
        b->setProperty("index", m_tabGroup->buttons().size());
        m_tabGroup->addButton(b, m_tabGroup->buttons().size()); // 显式 id，避免 button(0) 取到 nullptr
        tabLay->addWidget(b);
    }
    tabLay->addStretch();
    if (QAbstractButton *first = m_tabGroup->button(0))
        first->setChecked(true);
    root->addWidget(tabBar);

    m_tabs = new QStackedWidget(this);
    root->addWidget(m_tabs, 1);

    // 左右分栏：左侧图纸预览（2/3），右侧核对表单（1/3）
    QHBoxLayout *split = new QHBoxLayout();
    split->setSpacing(14);
    QWidget *productPage = new QWidget(this);
    QVBoxLayout *ppLay = new QVBoxLayout(productPage);
    ppLay->setContentsMargins(0, 0, 0, 0);
    ppLay->addLayout(split);
    m_tabs->addWidget(productPage);
    QVBoxLayout *leftCol = new QVBoxLayout();
    leftCol->setSpacing(10);
    QVBoxLayout *rightCol = new QVBoxLayout();
    rightCol->setSpacing(10);
    split->addLayout(leftCol, 2);
    split->addLayout(rightCol, 1);

    // ① 图纸文件
    QGroupBox *grpFile = new QGroupBox(QStringLiteral("① 选择图纸文件（图片 / PDF，可直接拖入，或按 Ctrl+V 粘贴）"), this);
    QVBoxLayout *grpFileLay = new QVBoxLayout(grpFile);
    // 拖放/预览区：图片用 QLabel 缩略，PDF 用 QPdfView 渲染，二者用 QStackedWidget 切换。
    // 外面套一层网格布局，用于把「删除当前图纸」按钮叠在大预览右上角
    QWidget *previewHost = new QWidget(grpFile);
    QGridLayout *previewLay = new QGridLayout(previewHost);
    previewLay->setContentsMargins(0, 0, 0, 0);
    previewLay->setSpacing(0);
    m_previewStack = new QStackedWidget(previewHost);
    m_previewStack->setMinimumHeight(200);
    m_filePreview = new QLabel(QStringLiteral("点击「选择文件…」、拖入文件，或按 Ctrl+V 粘贴"), m_previewStack);
    m_filePreview->setAlignment(Qt::AlignCenter);
    m_filePreview->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_filePreview->setToolTip(QStringLiteral("预览显示图纸右下角标题栏区域，保存时仍保存原图"));
    m_filePreview->setStyleSheet(QStringLiteral(
        "QLabel{border:2px dashed #4A5468;border-radius:8px;"
        "color:rgba(255,255,255,0.5);background:rgba(0,0,0,0.15);}"));
    m_previewStack->addWidget(m_filePreview);
    // PDF 预览：与主界面同样基于 QPdfDocument + QPdfView
    m_pdfDoc = new QPdfDocument(this);
    m_pdfView = new QPdfView(m_previewStack);
    m_pdfView->setObjectName(QStringLiteral("preview"));
    m_pdfView->setDocument(m_pdfDoc);
    m_pdfView->setPageMode(QPdfView::PageMode::SinglePage);
    m_pdfView->setZoomMode(QPdfView::ZoomMode::FitInView);
    m_previewStack->addWidget(m_pdfView);
    previewLay->addWidget(m_previewStack, 0, 0);
    m_btnDeleteCurrent = new QPushButton(QStringLiteral("删除当前图纸"), previewHost);
    m_btnDeleteCurrent->setObjectName(QStringLiteral("btnDanger"));
    m_btnDeleteCurrent->setCursor(Qt::PointingHandCursor);
    m_btnDeleteCurrent->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    m_btnDeleteCurrent->setStyleSheet(QStringLiteral(
        "QPushButton{background:#E06C75;color:#ffffff;border:none;"
        "border-radius:6px;padding:5px 10px;font-weight:bold;}"
        "QPushButton:hover{background:#C9606A;}"));
    m_btnDeleteCurrent->setToolTip(QStringLiteral(
        "删除当前预览的图纸；若是本次新增的附件，则只从本次录入中移除"));
    m_btnDeleteCurrent->hide();
    previewLay->addWidget(m_btnDeleteCurrent, 0, 0, Qt::AlignTop | Qt::AlignRight);
    connect(m_btnDeleteCurrent, &QPushButton::clicked,
            this, &EntryDialog::onDeleteCurrentDrawing);
    grpFileLay->addWidget(previewHost);

    // 缩略图条：编辑模式列出该产品已挂接的全部图纸，新增模式列出本次选入的全部附件。
    // 每张卡片都能点开预览，并各自带一个删除键（类似主界面「更多图纸」，但可逐张删除）
    m_thumbTitle = new QLabel(QStringLiteral("图纸文件"), grpFile);
    m_thumbTitle->setStyleSheet(QStringLiteral("color:rgba(255,255,255,0.65);"));
    m_attachList = new QListWidget(grpFile);
    // 用 ListMode 水平排列自定义卡片：itemWidget 在这种模式下布局最稳定
    m_attachList->setViewMode(QListView::ListMode);
    m_attachList->setFlow(QListView::LeftToRight);
    m_attachList->setWrapping(false);
    m_attachList->setMovement(QListView::Static);
    m_attachList->setResizeMode(QListView::Fixed);
    m_attachList->setSelectionMode(QAbstractItemView::NoSelection);
    m_attachList->setFixedHeight(104);
    m_attachList->setSpacing(4);
    m_attachList->setStyleSheet(QStringLiteral(
        "QListWidget{background:#1A2032;border:1px solid #4A5468;border-radius:6px;"
        "color:rgba(255,255,255,0.85);}"));
    m_attachList->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_attachList, &QListWidget::itemClicked, this, &EntryDialog::onAttachClicked);
    connect(m_attachList, &QListWidget::customContextMenuRequested,
            this, &EntryDialog::onAttachContextMenu);
    m_thumbTitle->hide();
    m_attachList->hide();
    grpFileLay->addWidget(m_thumbTitle);
    grpFileLay->addWidget(m_attachList);
    QHBoxLayout *fileLay = new QHBoxLayout();
    m_fileLabel = new QLabel(QStringLiteral("未选择文件"), grpFile);
    QPushButton *btnPick = new QPushButton(QStringLiteral("选择文件…"), grpFile);
    btnPick->setObjectName(QStringLiteral("btnEntry")); // 绿色强调按钮
    btnPick->setCursor(Qt::PointingHandCursor);
    QPushButton *btnPaste = new QPushButton(QStringLiteral("粘贴"), grpFile);
    btnPaste->setObjectName(QStringLiteral("btnGhost")); // 次级按钮
    btnPaste->setCursor(Qt::PointingHandCursor);
    btnPaste->setToolTip(QStringLiteral("粘贴剪贴板中的图片（截图）或图纸文件，快捷键 Ctrl+V"));
    connect(btnPaste, &QPushButton::clicked, this, &EntryDialog::onPaste);
    // 图纸类型：分段按钮（下发图 / 会签图），与整体风格一致
    QButtonGroup *dtypeGroup = new QButtonGroup(grpFile);
    dtypeGroup->setExclusive(true);
    m_dtypeGroup = dtypeGroup;
    QPushButton *btnIssued = new QPushButton(QStringLiteral("下发图"), grpFile);
    QPushButton *btnCounter = new QPushButton(QStringLiteral("会签图"), grpFile);
    for (auto *b : {btnIssued, btnCounter}) {
        b->setCheckable(true);
        b->setObjectName(QStringLiteral("chip"));
        b->setCursor(Qt::PointingHandCursor);
        dtypeGroup->addButton(b);
    }
    btnIssued->setChecked(true);
    fileLay->addWidget(m_fileLabel, 1);
    fileLay->addWidget(btnPick);
    fileLay->addWidget(btnPaste);
    fileLay->addWidget(btnIssued);
    fileLay->addWidget(btnCounter);
    grpFileLay->addLayout(fileLay);
    // 粘贴进度条：紧跟在「粘贴」按钮下方
    grpFileLay->addLayout(makeBusyRow(grpFile, &m_pasteLabel, &m_pasteBar));

    setAcceptDrops(true);

    m_btnOcr = new QPushButton(QStringLiteral("识别图纸"), this);
    m_btnOcr->setObjectName(QStringLiteral("btnEntry"));
    m_btnOcr->setCursor(Qt::PointingHandCursor);
    leftCol->addWidget(grpFile, 1);
    leftCol->addWidget(m_btnOcr, 0, Qt::AlignLeft);
    // 识别进度条：位于「识别图纸」按钮下方
    leftCol->addLayout(makeBusyRow(this, &m_ocrLabel, &m_ocrBar));

    // ② 信息表单
    QGroupBox *grpInfo = new QGroupBox(QStringLiteral("② 核对并补充信息"), this);
    QFormLayout *form = new QFormLayout(grpInfo);
    form->setLabelAlignment(Qt::AlignLeft);

    m_cmbCategory = new QComboBox(grpInfo);
    m_cmbCategory->addItems(DatabaseManager::categories());
    m_edtMaterial = new QLineEdit(grpInfo);
    m_edtName = new QLineEdit(grpInfo);
    m_edtModel = new QLineEdit(grpInfo);
    m_edtDrawingDate = new QDateEdit(QDate::currentDate(), grpInfo);
    m_edtDrawingDate->setCalendarPopup(true);
    m_edtDrawingDate->setDisplayFormat(QStringLiteral("yyyy-MM-dd"));
    m_edtRemark = new QLineEdit(grpInfo);
    m_edtChangeNote = new QLineEdit(grpInfo);
    m_edtCreator = new QLineEdit(grpInfo);
    // 录入时间：自动生成、只读；补充信息保存后由数据库刷新
    m_edtCreated = new QLineEdit(grpInfo);
    m_edtCreated->setReadOnly(true);
    m_edtCreated->setStyleSheet(QStringLiteral("QLineEdit{color:rgba(255,255,255,0.55);}"));

    form->addRow(reqLabel(QStringLiteral("产品分类"), true), m_cmbCategory);
    form->addRow(reqLabel(QStringLiteral("物料编码"), false), m_edtMaterial);
    form->addRow(reqLabel(QStringLiteral("名称"), true), m_edtName);
    form->addRow(reqLabel(QStringLiteral("型号"), true), m_edtModel);

    // 特有字段（随分类动态变化）从这一行开始插入，与其他字段共用同一套行列，保证对齐
    m_form = form;
    m_extFirstRow = form->rowCount();

    // 硬件/软件版本号不再对所有分类显示，改为“控制器”专属可配置特有字段
    // （由 field_config 驱动，在分类特有字段区动态渲染、随产品存入 product_ext）
    form->addRow(QStringLiteral("图纸时间"), m_edtDrawingDate);
    form->addRow(QStringLiteral("录入时间"), m_edtCreated);
    form->addRow(QStringLiteral("备注"), m_edtRemark);
    form->addRow(QStringLiteral("更改说明"), m_edtChangeNote);
    form->addRow(QStringLiteral("录入人"), m_edtCreator);

    m_edtQty = new QLineEdit(grpInfo);
    m_edtQty->setPlaceholderText(QStringLiteral("当前库存数量"));
    m_edtMinQty = new QLineEdit(grpInfo);
    m_edtMinQty->setPlaceholderText(QStringLiteral("库存下限（低于报警）"));
    m_edtMaxQty = new QLineEdit(grpInfo);
    m_edtMaxQty->setPlaceholderText(QStringLiteral("库存上限（高于提醒）"));
    form->addRow(QStringLiteral("数量"), m_edtQty);
    form->addRow(QStringLiteral("最小数量"), m_edtMinQty);
    form->addRow(QStringLiteral("最大数量"), m_edtMaxQty);

    // 右侧：表单放进滚动区，窗口变矮时可滚动
    // 固定标签列最小宽度，避免列被压缩后“硬件版本号”“图纸时间”等标签被裁切
    for (int r = 0; r < form->rowCount(); ++r) {
        if (QLayoutItem *it = form->itemAt(r, QFormLayout::LabelRole)) {
            if (auto *lb = qobject_cast<QLabel *>(it->widget())) {
                lb->setMinimumWidth(88);
                // 固定字段标签统一左对齐，作为整列对齐基准
                lb->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
            }
        }
    }
    grpInfo->setMinimumWidth(340);
    QScrollArea *infoScroll = new QScrollArea(this);
    infoScroll->setWidgetResizable(true);
    infoScroll->setFrameShape(QFrame::NoFrame);
    infoScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    infoScroll->setMinimumWidth(340);
    infoScroll->setWidget(grpInfo);
    rightCol->addWidget(infoScroll, 1);

    // 右下角：识别到的 OCR 原文（核对参考，不参与保存）
    m_ocrGroup = new QGroupBox(QStringLiteral("OCR 识别原文（核对参考）"), this);
    QVBoxLayout *ocrLay = new QVBoxLayout(m_ocrGroup);
    ocrLay->setContentsMargins(8, 6, 8, 6);
    m_ocrText = new QPlainTextEdit(m_ocrGroup);
    m_ocrText->setReadOnly(true);
    m_ocrText->setMinimumHeight(88);
    m_ocrText->setMaximumHeight(120);
    m_ocrText->setPlaceholderText(QStringLiteral("识别后在此显示图纸上的全部文字"));
    m_ocrText->setStyleSheet(QStringLiteral(
        "QPlainTextEdit{background:#242B3E;border:1px solid #4A5468;border-radius:4px;"
        "color:rgba(255,255,255,0.75);}"));
    ocrLay->addWidget(m_ocrText);
    // OCR 原文下方加搜索框：输入文字即在原文中高亮全部匹配并滚动定位，
    // 便于自动识别未覆盖时，手动输入关键字快速找到并复制
    m_ocrSearch = new QLineEdit(m_ocrGroup);
    m_ocrSearch->setPlaceholderText(QStringLiteral("未自动识别时，在此输入文字，自动定位到上方原文"));
    m_ocrSearch->setClearButtonEnabled(true);
    ocrLay->addWidget(m_ocrSearch);
    connect(m_ocrSearch, &QLineEdit::textChanged, this, &EntryDialog::searchOcrText);
    rightCol->addWidget(m_ocrGroup);

    // 底部按钮
    QHBoxLayout *foot = new QHBoxLayout();
    foot->addStretch();
    QPushButton *btnCancel = new QPushButton(QStringLiteral("取 消"), this);
    btnCancel->setObjectName(QStringLiteral("btnDanger")); // 柔和红
    btnCancel->setCursor(Qt::PointingHandCursor);
    QPushButton *btnSave = new QPushButton(QStringLiteral("保 存"), this);
    btnSave->setObjectName(QStringLiteral("btnEntry"));
    btnSave->setCursor(Qt::PointingHandCursor);
    foot->addWidget(btnCancel);
    foot->addWidget(btnSave);
    rightCol->addLayout(foot);

    connect(m_btnOcr, &QPushButton::clicked, this, &EntryDialog::onOcr);
    connect(btnPick, &QPushButton::clicked, this, &EntryDialog::onPickFile);
    connect(btnCancel, &QPushButton::clicked, this, &EntryDialog::cancelled);
    connect(btnSave, &QPushButton::clicked, this, &EntryDialog::onSave);
    connect(m_cmbCategory, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &EntryDialog::onCategoryChanged);

    // Ctrl+V 粘贴图片或图纸文件：只绑定到「产品录入」页，
    // 避免抢占「数量录入」页里内嵌 OCR 弹窗自己的 Ctrl+V 粘贴
    m_pasteShortcut = new QShortcut(QKeySequence::Paste, productPage);
    m_pasteShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(m_pasteShortcut, &QShortcut::activated, this, &EntryDialog::onPaste);

    // 数量录入页：内联嵌入的「数量录入」弹窗（最小/最大数量在管理页「数量管理」单产品设置）
    QWidget *qtyPage = new QWidget(this);
    QVBoxLayout *qpLay = new QVBoxLayout(qtyPage);
    qpLay->setContentsMargins(16, 16, 16, 16);
    qpLay->setSpacing(12);

    m_qtyPanel = new MaterialCodeDialog(qtyPage, MaterialCodeDialog::Quantity, true);
    m_qtyPanel->setPasteEnabled(false); // 默认在「产品录入」页，数量页的 Ctrl+V 先关掉
    qpLay->addWidget(m_qtyPanel, 1);

    m_tabs->addWidget(qtyPage);

    connect(m_tabGroup, QOverload<QAbstractButton *>::of(&QButtonGroup::buttonClicked),
            this, [this](QAbstractButton *b) {
        setEntryTab(b->property("index").toInt()); // Ctrl+V 归属一并在 setEntryTab 里同步
    });

    rebuildExtFields();
}

// 切换「产品录入 / 数量录入」子菜单，并同步 Ctrl+V 归属。
// QButtonGroup::buttonClicked 只在用户点击时发出，程序化 setChecked 不会连带切页，
// 所以这里必须显式同步 m_tabs，供补充信息 / 修改等跳转入口调用。
void EntryDialog::setEntryTab(int idx)
{
    if (!m_tabs || !m_tabGroup) return;
    m_tabs->setCurrentIndex(idx);
    if (QAbstractButton *b = m_tabGroup->button(idx))
        b->setChecked(true);
    if (m_pasteShortcut) m_pasteShortcut->setEnabled(idx == 0);
    if (m_qtyPanel) m_qtyPanel->setPasteEnabled(idx == 1);
}

void EntryDialog::switchToProductEntry()
{
    setEntryTab(0);
}

EntryDialog::~EntryDialog()
{
    // 同 MainWindow：子控件（分类下拉框）析构发出的信号不再投递到已析构的 EntryDialog
    const QList<QObject *> kids = findChildren<QObject *>();
    for (QObject *o : kids)
        disconnect(o, nullptr, this, nullptr);
    clearHeifTemps();
}

void EntryDialog::clearHeifTemps()
{
    for (const QString &f : m_heifTemps)
        QFile::remove(f);
    m_heifTemps.clear();
}

void EntryDialog::onCategoryChanged(int) { rebuildExtFields(); }

// 按当前分类动态生成特有字段输入框
void EntryDialog::rebuildExtFields()
{
    if (!m_form || m_extFirstRow < 0) return;

    // 清掉上次插入的特有字段行（removeRow 会一并删除该行控件）
    const int old = m_extEdits.size();
    for (int i = 0; i < old; ++i)
        m_form->removeRow(m_extFirstRow);
    m_extEdits.clear();

    // 直接插入表单行：标签宽度、对齐方式与固定字段完全一致
    const QString cat = m_cmbCategory->currentText();
    const QList<FieldConfig> fields = DatabaseManager::instance().fieldConfigs(cat);
    int row = m_extFirstRow;
    for (const FieldConfig &f : fields) {
        QLabel *lb = new QLabel(f.fieldLabel);
        lb->setMinimumWidth(88); // 与固定字段标签列同宽
        // 针数类字段：可输入 + 下拉快速选择（28/53/121 针）
        const bool isPins = f.fieldKey.contains(QStringLiteral("针"))
                            || f.fieldKey.contains(QStringLiteral("pins"), Qt::CaseInsensitive);
        QLineEdit *edt = nullptr;
        QWidget *editor = nullptr;
        if (isPins) {
            auto *cmb = new QComboBox();
            cmb->setEditable(true);
            cmb->addItems({QStringLiteral("28针"), QStringLiteral("53针"),
                           QStringLiteral("121针")});
            cmb->setInsertPolicy(QComboBox::NoInsert); // 手输内容不进下拉列表
            edt = cmb->lineEdit();
            edt->setPlaceholderText(QStringLiteral("可输入或下拉选择"));
            editor = cmb;
        } else {
            edt = new QLineEdit();
            editor = edt;
        }
        // 标签统一左对齐，与固定字段（产品分类/物料编码…）保持同一列左边缘
        lb->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        lb->setBuddy(editor);
        m_form->insertRow(row++, lb, editor);
        lb->show();
        editor->show();
        m_extEdits.append({f.fieldKey, edt}); // 下拉框用其内置输入框，读写逻辑与普通输入框一致
    }
}

void EntryDialog::onPickFile()
{
    // 支持一次多选：同一型号可能既有 PDF 又有图片
    const QStringList paths = QFileDialog::getOpenFileNames(
        this, QStringLiteral("选择图纸文件（可多选：PDF + 图片）"), QString(),
        QStringLiteral("图纸文件 (*.jpg *.jpeg *.png *.bmp *.pdf *.heic *.heif)"));
    if (paths.isEmpty()) return;
    addPickedFiles(paths);
}

// 统一设置图纸文件：更新文件名提示 + 缩略预览（图片显示原图缩放，PDF 用 QPdfView 渲染）
void EntryDialog::setPickedFile(const QString &path)
{
    if (m_editingId < 0) clearSupplement(); // 录入新图纸：先清掉上一张图纸的信息
    m_pickedFile = path;
    m_pickedFiles = QStringList{path}; // 单文件流程：重置为只有这一张
    m_pastedImage = QImage(); // 选了真实文件，清掉内存中的粘贴图
    m_pastedTempPath.clear();
    refreshAttachList();
    previewFile(path);
    autoOcrAfterLoad(); // 图纸加载完成后自动识别
}

// 追加多张图纸：列表为空时的第一张走完整流程（预览 + 自动识别），其余仅作为附件
void EntryDialog::addPickedFiles(const QStringList &paths)
{
    QStringList valid, failures;
    for (const QString &p : paths) {
        if (!isDrawingFile(p)) continue;
        QString use = p;
        // HEIC/HEIF 先解码转存成 JPG：图纸库、网页端、手机端、OCR 都只认原有格式
        if (HeifConverter::isHeifFile(p)) {
            QString convErr;
            use = HeifConverter::toJpeg(p, &convErr);
            if (use.isEmpty()) {
                failures << QStringLiteral("%1：%2").arg(QFileInfo(p).fileName(), convErr);
                continue;
            }
            m_heifTemps << use;
        }
        if (!valid.contains(use))
            valid << use;
    }
    if (!failures.isEmpty())
        NdMessageBox::warning(this, QStringLiteral("HEIC 图纸未能录入"),
                              failures.join(QStringLiteral("\n")));
    if (valid.isEmpty()) return;

    if (m_pickedFiles.isEmpty()) {
        m_pastedImage = QImage(); // 拖入真实文件时替换之前粘贴的图片
        m_pastedTempPath.clear();
        setPickedFile(valid.first()); // 触发预览 + 自动识别
        valid.removeFirst();
    }
    for (const QString &p : valid) {
        if (!m_pickedFiles.contains(p))
            m_pickedFiles << p;
    }
    if (!valid.isEmpty()) {
        // 已有主图时追加：预览最后一张便于确认，不重复识别
        m_pickedFile = valid.last();
        previewFile(m_pickedFile);
    }
    refreshAttachList();
}

// 仅刷新预览（图片/PDF），不触发识别、不清空已填信息
void EntryDialog::previewFile(const QString &path, bool fromExisting)
{
    if (!fromExisting) { // 预览本次新增的图纸时，当前项不再是数据库里的记录
        m_existingDrawingId = -1;
        m_existingDrawingPath.clear();
    }
    if (path.isEmpty()) {
        updateCurrentDeleteButton();
        return;
    }
    m_fileLabel->setText(QFileInfo(path).fileName());
    m_fileLabel->setToolTip(path);

    if (QFileInfo(path).suffix().toLower() == QStringLiteral("pdf")) {
        m_previewImage = QImage();
        m_filePreview->setPixmap(QPixmap());
        m_filePreview->clear();
        // PDF：用 QPdfView 直接渲染预览（与主界面一致），而非只显示文件名
        if (m_pdfDoc->load(path) == QPdfDocument::Error::None
            && m_pdfDoc->pageCount() > 0) {
            m_previewStack->setCurrentWidget(m_pdfView);
        } else {
            m_previewStack->setCurrentWidget(m_filePreview);
            m_filePreview->setText(QStringLiteral("PDF 加载失败"));
        }
    } else {
        m_previewImage = loadImageOriented(path);
        m_previewStack->setCurrentWidget(m_filePreview); // 从 PDF 切回图片时切回 QLabel
        if (m_previewImage.isNull())
            m_filePreview->setText(QStringLiteral("图片加载失败"));
        else
            updatePreview();
    }
    updateCurrentDeleteButton();
}

// 清空大预览与文件名提示（没有任何可预览的图纸时）
void EntryDialog::clearFilePreview()
{
    m_previewImage = QImage();
    m_filePreview->setPixmap(QPixmap());
    m_filePreview->setText(QStringLiteral("点击「选择文件…」、拖入文件，或按 Ctrl+V 粘贴"));
    m_previewStack->setCurrentWidget(m_filePreview);
    if (m_pdfDoc) m_pdfDoc->close();
    m_fileLabel->clear();
    m_fileLabel->setToolTip(QString());
    m_existingDrawingId = -1;
    m_existingDrawingPath.clear();
    m_pickedFile.clear();
}

// 缩略图条：左边是产品已挂接的图纸（点 × 真正删除归档文件），
// 右边是本次选入、尚未保存的附件（点 × 只移出本次录入）
void EntryDialog::refreshAttachList()
{
    if (!m_attachList) return;
    // setItemWidget 的卡片挂在 viewport 上，clear() 只删条目；
    // 先摘掉并显式销毁，避免反复刷新时旧卡片残留叠加或泄漏
    for (int i = 0; i < m_attachList->count(); ++i) {
        QListWidgetItem *item = m_attachList->item(i);
        if (QWidget *w = m_attachList->itemWidget(item)) {
            m_attachList->removeItemWidget(item);
            w->hide();
            w->deleteLater(); // 可能正处在卡片删除按钮的点击栈里，不能直接 delete
        }
    }
    m_attachList->clear();

    int no = 0;
    for (const Drawing &d : m_existingDrawings) {
        auto *item = new QListWidgetItem(m_attachList);
        item->setSizeHint(QSize(94, 92));
        item->setData(Qt::UserRole, QStringLiteral("e:%1").arg(d.id));
        const QString tip = QStringLiteral("%1\n%2\n%3\n点击预览，点 × 删除")
            .arg(QFileInfo(d.filePath).fileName(),
                 d.dtype == TypeCountersign ? QStringLiteral("会签图") : QStringLiteral("下发图"),
                 d.drawingDate.isValid() ? d.drawingDate.toString(QStringLiteral("yyyy-MM-dd"))
                                         : QStringLiteral("未标注日期"));
        auto *card = new ThumbCard(makeThumbPixmap(d.filePath),
                                   QStringLiteral("图纸%1").arg(++no), tip, m_attachList);
        card->setSelected(d.id == m_existingDrawingId);
        m_attachList->setItemWidget(item, card);
        const int id = d.id;
        connect(card->previewButton(), &QPushButton::clicked, this,
                [this, id] { showExistingDrawing(id); });
        connect(card->deleteButton(), &QPushButton::clicked, this,
                [this, id] { onDeleteExistingDrawing(id); });
    }

    int newNo = 0;
    for (const QString &p : m_pickedFiles) {
        auto *item = new QListWidgetItem(m_attachList);
        item->setSizeHint(QSize(94, 92));
        item->setData(Qt::UserRole, QStringLiteral("n:%1").arg(p));
        auto *card = new ThumbCard(makeThumbPixmap(p),
                                   QStringLiteral("新增%1").arg(++newNo),
                                   QStringLiteral("%1\n本次录入，尚未保存\n点击预览，点 × 移除").arg(p),
                                   m_attachList);
        card->setSelected(m_existingDrawingId < 0 && p == m_pickedFile);
        m_attachList->setItemWidget(item, card);
        const QString path = p;
        connect(card->previewButton(), &QPushButton::clicked, this, [this, path] {
            m_pickedFile = path;
            previewFile(path);
            refreshAttachList();
        });
        connect(card->deleteButton(), &QPushButton::clicked, this,
                [this, path] { onDeletePickedFile(path); });
    }

    const bool visible = !m_existingDrawings.isEmpty() || !m_pickedFiles.isEmpty();
    m_thumbTitle->setText(QStringLiteral("图纸文件（共 %1 张 · 点击缩略图预览 · 点 × 删除）")
                              .arg(no + newNo));
    m_thumbTitle->setVisible(visible);
    m_attachList->setVisible(visible);
    updateCurrentDeleteButton();
}

// 按当前预览来源刷新大预览右上角的删除键
void EntryDialog::updateCurrentDeleteButton()
{
    if (!m_btnDeleteCurrent) return;
    const bool existing = m_existingDrawingId >= 0;
    const bool picked = !m_pickedFile.isEmpty() && m_pickedFiles.contains(m_pickedFile);
    if (!existing && !picked && m_pastedImage.isNull()) {
        m_btnDeleteCurrent->hide();
        return;
    }
    m_btnDeleteCurrent->setText(existing ? QStringLiteral("删除当前图纸")
                                         : QStringLiteral("移除当前图纸"));
    m_btnDeleteCurrent->show();
    m_btnDeleteCurrent->raise();
}

// 预览某张已挂接图纸，并带出这张图纸自己的日期/类型（保存时回写到这一张）
void EntryDialog::showExistingDrawing(int drawingId)
{
    for (const Drawing &d : m_existingDrawings) {
        if (d.id != drawingId) continue;
        m_existingDrawingId = d.id;
        m_existingDrawingPath = d.filePath;
        m_pickedFile.clear();
        previewFile(d.filePath, true);
        if (d.drawingDate.isValid())
            m_edtDrawingDate->setDate(d.drawingDate);
        setDrawingType(d.dtype);
        refreshAttachList();
        return;
    }
}

// 删除一张已挂接图纸：确认后删数据库记录与归档文件（客户端由服务器代删）
void EntryDialog::onDeleteExistingDrawing(int drawingId)
{
    QString fileName;
    for (const Drawing &d : m_existingDrawings) {
        if (d.id == drawingId) {
            fileName = QFileInfo(d.filePath).fileName();
            break;
        }
    }
    if (!NdMessageBox::question(
            this, QStringLiteral("删除图纸"),
            QStringLiteral("确定删除图纸「%1」吗？\n\n将同时删除归档文件与数据库记录，且不可恢复。")
                .arg(fileName.isEmpty() ? QStringLiteral("（无文件名）") : fileName)))
        return;

    QString err;
    if (!DatabaseManager::instance().deleteDrawing(drawingId, &err)) {
        NdMessageBox::critical(this, QStringLiteral("删除失败"),
                               err.isEmpty() ? QStringLiteral("图纸删除失败") : err);
        return;
    }
    emit drawingsChanged(); // 立即生效，其它页面不要继续显示已删的图纸
    for (int i = 0; i < m_existingDrawings.size(); ++i) {
        if (m_existingDrawings.at(i).id == drawingId) {
            m_existingDrawings.removeAt(i);
            break;
        }
    }
    if (m_existingDrawingId == drawingId) {
        m_existingDrawingId = -1;
        m_existingDrawingPath.clear();
        m_pickedFile.clear();
        if (!m_existingDrawings.isEmpty()) {
            showExistingDrawing(m_existingDrawings.first().id); // 内部会刷新缩略图条
            return;
        }
        if (!m_pickedFiles.isEmpty()) {
            m_pickedFile = m_pickedFiles.first();
            previewFile(m_pickedFile);
        } else {
            clearFilePreview();
        }
    }
    refreshAttachList();
}

// 从本次录入中移除一张尚未保存的新附件（不动磁盘上已归档的图纸）
void EntryDialog::onDeletePickedFile(const QString &path)
{
    m_pickedFiles.removeAll(path);
    if (m_pickedFile == path) {
        m_pickedFile.clear();
        if (!m_pickedFiles.isEmpty()) {
            m_pickedFile = m_pickedFiles.first();
            previewFile(m_pickedFile);
        } else if (!m_existingDrawings.isEmpty()) {
            showExistingDrawing(m_existingDrawings.first().id);
            return;
        } else if (m_pastedImage.isNull()) {
            clearFilePreview();
        }
    }
    refreshAttachList();
}

// 大预览右上角删除键：已挂接图纸走真实删除，新增附件/粘贴图只移出本次录入
void EntryDialog::onDeleteCurrentDrawing()
{
    if (m_existingDrawingId >= 0) {
        onDeleteExistingDrawing(m_existingDrawingId);
        return;
    }
    if (!m_pickedFile.isEmpty() && m_pickedFiles.contains(m_pickedFile)) {
        onDeletePickedFile(m_pickedFile);
        return;
    }
    if (!m_pastedImage.isNull()) {
        m_pastedImage = QImage();
        m_pastedTempPath.clear();
        clearFilePreview();
        refreshAttachList();
    }
}

// 点击缩略图条目：切换预览（卡片按钮不可用时的兜底）
void EntryDialog::onAttachClicked(QListWidgetItem *item)
{
    if (!item) return;
    const QString key = item->data(Qt::UserRole).toString();
    if (key.startsWith(QLatin1String("e:"))) {
        showExistingDrawing(key.mid(2).toInt());
    } else if (key.startsWith(QLatin1String("n:"))) {
        const QString p = key.mid(2);
        m_pickedFile = p;
        previewFile(p);
        refreshAttachList();
    }
}

// 缩略图右键：预览 / 删除（与卡片上的按钮等价）
void EntryDialog::onAttachContextMenu(const QPoint &pos)
{
    QListWidgetItem *item = m_attachList->itemAt(pos);
    if (!item) return;
    const QString key = item->data(Qt::UserRole).toString();
    const bool existing = key.startsWith(QLatin1String("e:"));
    RoundedMenu menu(this);
    QAction *aPreview = menu.addAction(QStringLiteral("预览此图纸"));
    QAction *aDelete = menu.addAction(existing ? QStringLiteral("删除此图纸")
                                               : QStringLiteral("从本次录入中移除"));
    QAction *chosen = menu.exec(m_attachList->viewport()->mapToGlobal(pos));
    if (!chosen) return;
    if (chosen == aPreview) {
        onAttachClicked(item);
        return;
    }
    if (existing)
        onDeleteExistingDrawing(key.mid(2).toInt());
    else
        onDeletePickedFile(key.mid(2));
}

// 预览图按预览区可用尺寸等比缩放：只显示右下角标题栏区域（约整图 1/4），
// 便于核对型号/版本等信息；保存时仍使用原图
void EntryDialog::updatePreview()
{
    if (m_previewImage.isNull()) return;
    const QSize target = m_filePreview->size() - QSize(12, 12);
    if (target.width() < 2 || target.height() < 2) return;
    const QImage crop = m_previewImage.copy(titleBlockRect(m_previewImage));
    m_filePreview->setPixmap(QPixmap::fromImage(crop.scaled(
        target, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
}

void EntryDialog::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    updatePreview();
}

// OCR 原文搜索：高亮全部匹配（绿色），并把视图滚动到第一个匹配位置
void EntryDialog::searchOcrText(const QString &text)
{
    QList<QTextEdit::ExtraSelection> sels;
    m_ocrText->setExtraSelections(sels); // 先清除上一次高亮
    const QString kw = text.trimmed();
    if (kw.isEmpty() || !m_ocrText->document()) return;

    QTextCharFormat fmt;
    fmt.setBackground(QColor(67, 207, 124, 140)); // 与主色一致的绿色半透明
    fmt.setForeground(QColor(11, 31, 20));

    QTextDocument *doc = m_ocrText->document();
    QTextCursor cur(doc);
    for (;;) {
        cur = doc->find(kw, cur); // 默认即不区分大小写
        if (cur.isNull()) break;
        QTextEdit::ExtraSelection sel;
        sel.cursor = cur;
        sel.format = fmt;
        sels.append(sel);
    }
    m_ocrText->setExtraSelections(sels);
    if (!sels.isEmpty())
        m_ocrText->setTextCursor(sels.first().cursor); // 滚动到首个匹配
}

// 进入本页时主动获取焦点：否则 Ctrl+V 快捷键（WidgetWithChildrenShortcut）
// 在第一次点击页内控件前不生效
void EntryDialog::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    setFocus();
    // 首次进入时布局尚未完成，预览区还是默认小尺寸，按它缩放会导致图片很小；
    // 延后到布局结束后再按实际尺寸重绘预览
    QTimer::singleShot(0, this, [this] { updatePreview(); });
}

// 粘贴的图片：只在保存/识别时编码成临时文件。用 JPEG（质量 92）而不是 PNG，
// 大图编码快一个数量级，识别前几乎不再等待
QString EntryDialog::ensureImageFile()
{
    if (m_pastedImage.isNull()) return QString();
    if (!m_pastedTempPath.isEmpty() && QFileInfo::exists(m_pastedTempPath))
        return m_pastedTempPath;

    QImage img = m_pastedImage;
    if (img.hasAlphaChannel()) { // JPEG 无透明通道，先铺白底
        QImage flat(img.size(), QImage::Format_RGB32);
        flat.fill(Qt::white);
        QPainter p(&flat);
        p.drawImage(0, 0, img);
        img = flat;
    }
    const QString path = QDir::tempPath() + QStringLiteral("/nd_paste_%1.jpg")
        .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss_zzz")));
    if (!img.save(path, "JPEG", 92)) return QString();
    m_pastedTempPath = path;
    return path;
}

// 清空表单，准备下一次录入
void EntryDialog::resetForm()
{
    cancelRunningOcr(); // 重置/载入其它产品时，丢弃上一次未完成的识别
    m_editingId = -1;
    m_pickedFile.clear();
    m_pickedFiles.clear(); // 清空多附件
    m_existingDrawings.clear(); // 清掉上一个产品挂接的全部图纸
    m_existingDrawingPath.clear();
    m_existingDrawingId = -1;
    m_pastedImage = QImage();
    m_pastedTempPath.clear();
    clearHeifTemps();
    clearFilePreview();   // 预览区、文件名、删除键一并复位
    refreshAttachList();  // 缩略图条随之为空并隐藏

    m_cmbCategory->setCurrentIndex(0);
    clearSupplement();
}

// 清掉上一张图纸的补充信息（识别结果、手工填写、特有字段、绿框提示、OCR 原文），
// 换新图纸时调用；正在编辑已有产品（管理页跳转）时不调用
void EntryDialog::clearSupplement()
{
    for (auto *e : {m_edtMaterial, m_edtName, m_edtModel,
                    m_edtRemark, m_edtChangeNote,
                    m_edtQty, m_edtMinQty, m_edtMaxQty}) {
        e->clear();
        e->setStyleSheet(QString()); // 去掉识别结果绿框
    }
    m_edtDrawingDate->setDate(QDate::currentDate());
    m_edtCreated->setText(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm")));
    m_edtCreator->setText(m_currentUser);
    setDrawingType(TypeIssued);
    rebuildExtFields(); // 特有字段整体重建，等于清空
    if (m_ocrText) {
        m_ocrText->clear();
        m_ocrGroup->setTitle(QStringLiteral("OCR 识别原文（核对参考）"));
    }
    if (m_ocrSearch) m_ocrSearch->clear(); // 同时清空定位框（会触发清空高亮）
}

// 显示/隐藏处理进度（0..0 的来回滚动条，让用户知道程序在干活）
void EntryDialog::beginBusy(QProgressBar *bar, QLabel *label, const QString &text)
{
    label->setText(text);
    label->show();
    bar->setRange(0, 0);
    bar->show();
}

void EntryDialog::endBusy(QProgressBar *bar, QLabel *label)
{
    bar->hide();
    label->hide();
    label->clear();
}

// 从剪贴板粘贴：先亮进度条再处理，避免大图解码时界面像卡死
void EntryDialog::onPaste()
{
    // 焦点在文本框中时，Ctrl+V 保持普通文字粘贴（剪贴板是图片时仍按图纸粘贴）
    if (auto *le = qobject_cast<QLineEdit *>(QApplication::focusWidget())) {
        const QMimeData *m = QGuiApplication::clipboard()->mimeData();
        if (!m || !m->hasImage()) {
            le->paste();
            return;
        }
    }

    beginBusy(m_pasteBar, m_pasteLabel, QStringLiteral("正在读取剪贴板…"));
    // 延后到下一轮事件循环执行，让进度条先绘制出来
    QTimer::singleShot(0, this, [this] {
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        pasteFromClipboard();
        endBusy(m_pasteBar, m_pasteLabel);
    });
}

void EntryDialog::pasteFromClipboard()
{
    const QMimeData *mime = QGuiApplication::clipboard()->mimeData();
    if (!mime) return;

    // 1. 剪贴板是图片（截图/复制的图）→ 先留在内存并即时预览，保存/识别时再落盘
    if (mime->hasImage()) {
        const QImage img = qvariant_cast<QImage>(mime->imageData());
        if (!img.isNull()) {
            if (m_editingId < 0) clearSupplement(); // 录入新图纸：清掉上一张的信息
            m_pastedImage = img;
            m_pastedTempPath.clear();
            m_pickedFile.clear();
            m_pickedFiles.clear(); // 粘贴图片替换掉之前的所有附件
            m_existingDrawingId = -1; // 当前预览改为内存图片，不再指向已挂接图纸
            m_existingDrawingPath.clear();
            refreshAttachList();
            m_fileLabel->setText(QStringLiteral("粘贴的图片"));
            m_fileLabel->setToolTip(QStringLiteral("保存时将写入图纸库"));
            m_previewImage = img;
            updatePreview();
            autoOcrAfterLoad(); // 粘贴的图片加载完成后自动识别
            return;
        }
    }
    // 2. 剪贴板是文件（资源管理器复制，可能一次复制多张）
    if (mime->hasUrls()) {
        QStringList paths;
        for (const QUrl &u : mime->urls()) {
            const QString p = u.toLocalFile();
            if (!p.isEmpty() && isDrawingFile(p) && !paths.contains(p))
                paths << p;
        }
        if (!paths.isEmpty()) {
            addPickedFiles(paths);
            return;
        }
    }
    // 3. 剪贴板是文件路径文本
    if (mime->hasText()) {
        const QString p = mime->text().trimmed().remove(QLatin1Char('"'));
        if (QFileInfo::exists(p) && isDrawingFile(p)) {
            addPickedFiles({p});
            return;
        }
    }
    NdMessageBox::information(this, QStringLiteral("提示"),
                             QStringLiteral("剪贴板中没有图片或图纸文件"));
}

// 拖入图纸文件
void EntryDialog::dragEnterEvent(QDragEnterEvent *event)
{
    if (!event->mimeData()->hasUrls()) return;
    // 只要拖入的文件里有任意一张图纸就接受（支持一次拖多张）
    for (const QUrl &u : event->mimeData()->urls()) {
        if (isDrawingFile(u.toLocalFile())) {
            event->acceptProposedAction();
            return;
        }
    }
}

void EntryDialog::dropEvent(QDropEvent *event)
{
    const QList<QUrl> urls = event->mimeData()->urls();
    if (urls.isEmpty()) return;
    QStringList paths;
    for (const QUrl &u : urls) {
        const QString p = u.toLocalFile();
        if (!p.isEmpty() && isDrawingFile(p) && !paths.contains(p))
            paths << p;
    }
    if (paths.isEmpty()) return;
    event->acceptProposedAction();
    addPickedFiles(paths); // 多张：第一张负责预览+识别，其余作为附件
}

// 当前登录用户：录入人自动填入并只读
void EntryDialog::setCurrentUser(const QString &name)
{
    m_currentUser = name;
    m_edtCreator->setText(name);
    m_edtCreator->setReadOnly(!name.isEmpty());
}

// 图纸类型分段按钮读写
int EntryDialog::drawingType() const
{
    auto *b = m_dtypeGroup ? qobject_cast<QPushButton *>(m_dtypeGroup->checkedButton())
                           : nullptr;
    return (b && b->text().contains(QStringLiteral("会签"))) ? TypeCountersign : TypeIssued;
}

void EntryDialog::setDrawingType(int t)
{
    if (!m_dtypeGroup) return;
    const auto btns = m_dtypeGroup->buttons();
    for (auto *b : btns) {
        const bool isCounter = b->text().contains(QStringLiteral("会签"));
        if ((t == TypeCountersign) == isCounter)
            b->setChecked(true);
    }
}

// 离线 OCR 引擎：程序目录/ocr/PaddleOCR-json/PaddleOCR-json.exe
static QString ocrEnginePath()
{
    const QString exe = QCoreApplication::applicationDirPath()
        + QStringLiteral("/ocr/PaddleOCR-json/PaddleOCR-json.exe");
    return QFileInfo::exists(exe) ? exe : QString();
}

// 静默中止正在进行的识别：换图纸时用，避免上一次识别结果回填到新图纸上
void EntryDialog::cancelRunningOcr()
{
    if (!m_ocrProc) return;
    if (m_ocrTimeout) m_ocrTimeout->stop();
    m_ocrProc->disconnect(); // 不再接收其 finished/errorOccurred，避免弹旧结果的提示
    m_ocrProc->kill();
    m_ocrProc->deleteLater();
    m_ocrProc = nullptr;
    m_btnOcr->setEnabled(true);
    endBusy(m_ocrBar, m_ocrLabel);
}

// 图纸加载完成（选择文件 / 拖入 / 粘贴）后自动识别：
// 延后一轮事件循环再启动，让预览先绘制出来；编辑已有产品时不自动识别，避免覆盖已填信息
void EntryDialog::autoOcrAfterLoad()
{
    if (m_editingId >= 0) return;       // 管理页跳转的编辑模式：保留原有录入内容
    if (m_pickedFile.isEmpty() && m_pastedImage.isNull()) return;
    if (ocrEnginePath().isEmpty()) return; // 未部署识别引擎则静默跳过，不打扰录入

    cancelRunningOcr(); // 连续换图时只认最后一张
    QTimer::singleShot(0, this, [this] { onOcr(); });
}

// 解析引擎输出：{code:100, data:[{box, text}]} → 按坐标从上到下拼成多行文本
static bool parseOcrJson(const QByteArray &out, QString *text)
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

// 识别图纸：PDF 先渲染首页为图片，再异步调用离线引擎（界面不阻塞，进度条持续滚动）
void EntryDialog::onOcr()
{
    if (m_ocrProc) return; // 正在识别，避免重复启动

    const QString exe = ocrEnginePath();
    if (exe.isEmpty()) {
        NdMessageBox::warning(this, QStringLiteral("识别失败"),
                             QStringLiteral("未找到离线识别引擎：ocr/PaddleOCR-json/PaddleOCR-json.exe"));
        return;
    }

    // 先亮进度条再准备图片：点击瞬间就有反馈，不会“先卡几秒才见进度条”
    beginBusy(m_ocrBar, m_ocrLabel, QStringLiteral("正在准备图纸…"));
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);

    // 1. 取整页图像：粘贴的图直接用内存图（不落盘），文件直接读，PDF 渲染首页
    QImage page;
    if (!m_pastedImage.isNull()) {
        page = m_pastedImage;
    } else {
        // 补充信息（编辑模式）未选新图纸时，识别该产品已挂接的那张图纸
        const QString srcFile = m_pickedFile.isEmpty() ? m_existingDrawingPath : m_pickedFile;
        if (srcFile.isEmpty()) {
            endBusy(m_ocrBar, m_ocrLabel);
            NdMessageBox::information(this, QStringLiteral("提示"),
                                     QStringLiteral("请先选择、拖入或粘贴图纸"));
            return;
        }
        if (QFileInfo(srcFile).suffix().compare(QStringLiteral("pdf"), Qt::CaseInsensitive) == 0) {
            QApplication::setOverrideCursor(Qt::WaitCursor);
            QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
            QPdfDocument doc;
            if (doc.load(srcFile) != QPdfDocument::Error::None || doc.pageCount() < 1) {
                QApplication::restoreOverrideCursor();
                endBusy(m_ocrBar, m_ocrLabel);
                NdMessageBox::warning(this, QStringLiteral("识别失败"), QStringLiteral("无法读取该 PDF 图纸"));
                return;
            }
            const QSizeF ps = doc.pagePointSize(0);
            const int w = 1600;
            const int h = qMax(1, qRound(w * ps.height() / qMax(1.0, ps.width())));
            const QImage pg = doc.render(0, QSize(w, h));
            page = QImage(w, h, QImage::Format_RGB32);
            page.fill(Qt::white);
            {
                QPainter p(&page);
                p.drawImage(0, 0, pg);
            }
            QApplication::restoreOverrideCursor();
        } else {
            page = loadImageOriented(srcFile);
        }
    }
    if (page.isNull()) {
        endBusy(m_ocrBar, m_ocrLabel);
        NdMessageBox::warning(this, QStringLiteral("识别失败"), QStringLiteral("无法读取该图片"));
        return;
    }

    // 2. 只识别图纸右下角的标题栏区域（与预览一致，约整图 1/4）：
    //    信息集中、干扰少，小字清晰度更高；区域偏小时先放大到 1800px 宽再识别
    QImage roi = page.copy(titleBlockRect(page));
    if (roi.width() < 1800)
        roi = roi.scaledToWidth(1800, Qt::SmoothTransformation);

    const QString imagePath = QDir::tempPath()
        + QStringLiteral("/nd_ocr_roi_%1.jpg")
              .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss_zzz")));
    if (!roi.save(imagePath, "JPEG", 95)) {
        endBusy(m_ocrBar, m_ocrLabel);
        NdMessageBox::warning(this, QStringLiteral("识别失败"), QStringLiteral("图纸区域保存失败"));
        return;
    }

    startOcr(imagePath);
}

void EntryDialog::startOcr(const QString &imagePath)
{
    const QString exe = ocrEnginePath();

    m_ocrProc = new QProcess(this);
    m_ocrProc->setWorkingDirectory(QFileInfo(exe).absolutePath()); // 引擎按相对路径找模型
    connect(m_ocrProc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int, QProcess::ExitStatus) { onOcrFinished(); });
    connect(m_ocrProc, &QProcess::errorOccurred, this, [this](QProcess::ProcessError err) {
        if (err == QProcess::FailedToStart) onOcrFinished();
    });

    m_btnOcr->setEnabled(false);
    beginBusy(m_ocrBar, m_ocrLabel, QStringLiteral("正在识别图纸（引擎首次启动约需数秒）…"));
    m_ocrProc->start(exe, {QStringLiteral("-image_path=") + QDir::toNativeSeparators(imagePath),
                           QStringLiteral("-ensure_ascii=false")});

    // 兜底超时：引擎异常时不让界面一直转圈
    if (!m_ocrTimeout) {
        m_ocrTimeout = new QTimer(this);
        m_ocrTimeout->setSingleShot(true);
        connect(m_ocrTimeout, &QTimer::timeout, this, [this] {
            if (m_ocrProc) {
                m_ocrProc->kill();
                onOcrFinished();
            }
        });
    }
    m_ocrTimeout->start(300000);
}

void EntryDialog::onOcrFinished()
{
    if (!m_ocrProc) return;
    if (m_ocrTimeout) m_ocrTimeout->stop();
    if (m_ocrProc->state() != QProcess::NotRunning) {
        m_ocrProc->kill();
        m_ocrProc->waitForFinished(3000);
    }
    const QByteArray out = m_ocrProc->readAllStandardOutput();
    m_ocrProc->deleteLater();
    m_ocrProc = nullptr;

    m_btnOcr->setEnabled(true);
    endBusy(m_ocrBar, m_ocrLabel);

    QString text;
    if (!parseOcrJson(out, &text)) {
        NdMessageBox::warning(this, QStringLiteral("识别失败"),
                             QStringLiteral("未能识别出文字。请确认 ocr/PaddleOCR-json 完整，或改用手工填写。"));
        return;
    }

    m_ocrText->setPlainText(text); // 右下角展示识别原文，便于核对
    const QStringList filled = applyOcrText(text, QFileInfo(m_pickedFile).completeBaseName());
    m_ocrGroup->setTitle(filled.isEmpty()
        ? QStringLiteral("OCR 识别原文（未匹配到可填字段，请手工填写）")
        : QStringLiteral("OCR 识别原文（已填入：%1）").arg(filled.join(QStringLiteral("、"))));
}

// 日期解析：兼容图纸上的多种写法
// 2026-09-28 / 2026.9.30 / 2026年9月30日 / 2026.0928（月日连写） / 20260928 / 2026.928
static QDate parseOcrDate(const QString &s)
{
    static const QRegularExpression patterns[] = {
        QRegularExpression(QStringLiteral("(?<!\\d)(20\\d{2})[-/年.](\\d{1,2})[-/月.](\\d{1,2})")),
        QRegularExpression(QStringLiteral("(?<!\\d)(20\\d{2})[-/年.]?(\\d{2})(\\d{2})(?!\\d)")),
        QRegularExpression(QStringLiteral("(?<!\\d)(20\\d{2})[-/年.](\\d)(\\d{2})(?!\\d)"))};
    for (const QRegularExpression &re : patterns) {
        const auto m = re.match(s);
        if (!m.hasMatch()) continue;
        const QDate d(m.captured(1).toInt(), m.captured(2).toInt(), m.captured(3).toInt());
        if (d.isValid()) return d;
    }
    return QDate();
}

// 按规则从 OCR 文本中提取字段并回填，返回已填入的字段名
QStringList EntryDialog::applyOcrText(const QString &text, const QString &fileName)
{
    QStringList filled;
    auto markFilled = [](QLineEdit *e) {
        e->setStyleSheet(QStringLiteral("border:1px solid #43CF7C;")); // 识别结果绿框提示
    };
    // 优先按“标签：值”提取，失败再用通用规则（不跨行，避免抓到下一行的内容）
    auto labeled = [&text](const QStringList &labels) -> QString {
        for (const QString &lb : labels) {
            QRegularExpression re(lb + QStringLiteral("[：:]?[ \\t]*([^\\s，,；;]{2,})"));
            auto m = re.match(text);
            if (m.hasMatch()) return m.captured(1).trimmed();
        }
        return QString();
    };
    QString compact = text;
    compact.remove(QLatin1Char(' '));

    // 物料编码
    QString code = labeled({QStringLiteral("物料编码"), QStringLiteral("物料号"),
                            QStringLiteral("料号")});
    if (code.isEmpty()) {
        auto m = QRegularExpression(QStringLiteral("(2\\d{12})")).match(compact);
        if (m.hasMatch()) code = m.captured(1);
    }
    if (!code.isEmpty() && m_edtMaterial->text().isEmpty()) {
        m_edtMaterial->setText(code);
        markFilled(m_edtMaterial);
        filled << QStringLiteral("物料编码");
    }

    // 硬件版本号：图纸里可能写作“硬件号：NDC9-2-B”（不能用“版本号”，否则会抓到软件版本）
    QString hw = labeled({QStringLiteral("硬件版本号"), QStringLiteral("硬件版本"),
                          QStringLiteral("硬件号")});
    if (hw.isEmpty()) {
        auto m = QRegularExpression(QStringLiteral(
                     "(?:硬件号|硬件版本号|型号)\\s*[：:]\\s*([A-Za-z0-9][A-Za-z0-9._\\-]{2,})"))
                     .match(compact);
        if (m.hasMatch()) hw = m.captured(1);
    }
    hw.remove(QRegularExpression(QStringLiteral("[。，,；;]+$")));
    if (!hw.isEmpty()) {
        // 硬件版本号现在是控制器特有字段（hw_version），OCR 结果写回对应扩展字段
        for (auto &pair : m_extEdits) {
            if (pair.first == QStringLiteral("hw_version") && pair.second->text().isEmpty()) {
                pair.second->setText(hw);
                markFilled(pair.second);
                filled << QStringLiteral("硬件版本号");
                break;
            }
        }
    }

    // 标题栏锚点行号：备注/硬件号/软件版本/图样标记/图纸编号 的第一行
    const QStringList lines = text.split(QLatin1Char('\n'));
    int anchor = -1;
    {
        static const QRegularExpression anchorRe(QStringLiteral(
            "硬件号|硬件版本|软件版本|备注|图样标记|图纸编号"));
        for (int i = 0; i < lines.size() && anchor < 0; ++i) {
            if (anchorRe.match(lines.at(i)).hasMatch())
                anchor = i; // 标题栏文字都在锚点附近
        }
    }
    // 在“整行匹配”的候选中取离锚点最近的一行（避免抓到明细表或版本号里的片段）
    auto closestFullLine = [&](const QRegularExpression &re) -> QString {
        int best = -1;
        int bestDist = 1 << 30;
        for (int i = 0; i < lines.size(); ++i) {
            QString t = lines.at(i).trimmed();
            t.remove(QRegularExpression(QStringLiteral("[。，,；;.]+$"))); // 去掉行尾标点
            if (!re.match(t).hasMatch())
                continue;
            const int dist = anchor >= 0 ? qAbs(i - anchor) : -i;
            if (dist < bestDist) {
                bestDist = dist;
                best = i;
            }
        }
        if (best < 0)
            return QString();
        QString t = lines.at(best).trimmed();
        t.remove(QRegularExpression(QStringLiteral("[。，,；;.]+$")));
        return re.match(t).captured(1);
    };

    // 型号（图号）：标题栏里如“LP2604-C7-48-0530”，要求整行匹配；
    // 否则会从软件版本号“LP2604-C7_TCU_B1.0.00.001”里截出半截“LP2604-C7”
    QString model = labeled({QStringLiteral("规格型号")});
    if (model.isEmpty()) {
        static const QRegularExpression modelRe(QStringLiteral(
            "^([A-Z]{2,4}\\d{3,4}[A-Za-z0-9]*(?:-[A-Za-z0-9]+)*)$"));
        model = closestFullLine(modelRe);
    }
    if (model.isEmpty()) { // 兜底：整段文字里找
        auto m = QRegularExpression(
                     QStringLiteral("[A-Z]{2,4}\\d{3,4}[A-Za-z0-9]*(?:-[A-Za-z0-9]+)*"))
                     .match(text);
        if (m.hasMatch()) model = m.captured(0);
    }
    if (!model.isEmpty() && model == hw) model.clear();
    if (!model.isEmpty() && m_edtModel->text().isEmpty()) {
        m_edtModel->setText(model);
        markFilled(m_edtModel);
        filled << QStringLiteral("型号");
    }

    // 名称：标题栏里通常单独占一行，如“TCU控制器”。
    // 优先取最靠近标题栏锚点（备注/硬件号/软件版本号…）的那一行，
    // 避免抓到图纸明细表里的“接近开关”“压力传感器”等零件名
    QString name = labeled({QStringLiteral("产品名称")});
    if (name.isEmpty()) {
        static const QRegularExpression nameRe(QStringLiteral(
            "^([A-Za-z0-9]{0,8}[\\x{4e00}-\\x{9fa5}]{0,8}"
            "(?:控制器|电器盒|开关|传感器|仪表|线束|方向盘))$"));
        name = closestFullLine(nameRe);
    }
    if (name.isEmpty()) { // 兜底：在整段文字里找
        auto m = QRegularExpression(QStringLiteral(
                     "([A-Za-z0-9]{0,8}[\\x{4e00}-\\x{9fa5}]{0,8}"
                     "(?:控制器|电器盒|开关|传感器|仪表|线束|方向盘))"))
                     .match(text);
        if (m.hasMatch()) name = m.captured(1);
    }
    if (!name.isEmpty() && m_edtName->text().isEmpty()) {
        m_edtName->setText(name);
        markFilled(m_edtName);
        filled << QStringLiteral("名称");
    }

    // 文件名辅助识别：OCR 为主，仅当 OCR 仍未识别到名称/型号时用文件名兜底。
    // 文件名可能带微信下载后缀（如 “（1）”“(1)”“（2）(2)”），先剔除再识别。
    if (!fileName.isEmpty()) {
        QString fn = fileName;
        fn.remove(QRegularExpression(QStringLiteral(
            "[ \t（(]*[（(][ \t]*[0-9]+[ \t]*[）)][ \t）)]*")));
        fn = fn.trimmed();
        if (model.isEmpty()) {
            auto m = QRegularExpression(QStringLiteral(
                "([A-Z]{2,4}[0-9]{3,4}[A-Za-z0-9]*(?:-[A-Za-z0-9]+)*)")).match(fn);
            if (m.hasMatch()) {
                const QString fm = m.captured(1);
                if (fm != hw) {
                    m_edtModel->setText(fm);
                    markFilled(m_edtModel);
                    filled << QStringLiteral("型号");
                }
            }
        }
        if (name.isEmpty()) {
            auto m = QRegularExpression(QStringLiteral(
                "([一-龥A-Za-z0-9]{0,8}"
                "(?:控制器|电器盒|开关|传感器|仪表|线束|方向盘))")).match(fn);
            if (m.hasMatch()) {
                m_edtName->setText(m.captured(1));
                markFilled(m_edtName);
                filled << QStringLiteral("名称");
            }
        }
    }

    // 软件版本号
    const QString sw = labeled({QStringLiteral("软件版本号"), QStringLiteral("软件版本"),
                                QStringLiteral("软件")});
    if (!sw.isEmpty()) {
        // 软件版本号现在是控制器特有字段（sw_version），OCR 结果写回对应扩展字段
        for (auto &pair : m_extEdits) {
            if (pair.first == QStringLiteral("sw_version") && pair.second->text().isEmpty()) {
                pair.second->setText(sw);
                markFilled(pair.second);
                filled << QStringLiteral("软件版本号");
                break;
            }
        }
    }

    // 备注/用途：如“四驱动力换向、无人驾驶用”
    QString remark;
    auto rm = QRegularExpression(QStringLiteral(
                  "([\\x{4e00}-\\x{9fa5}]{2,}(?:、[\\x{4e00}-\\x{9fa5}]{2,})+[\\x{4e00}-\\x{9fa5}]{0,4}用)"))
                  .match(text);
    if (rm.hasMatch()) remark = rm.captured(1);
    if (remark.isEmpty())
        remark = labeled({QStringLiteral("用途"), QStringLiteral("使用场合"), QStringLiteral("备注")});
    if (remark.size() < 4) remark.clear(); // 过滤掉“硬件号”这类误匹配
    if (!remark.isEmpty() && m_edtRemark->text().isEmpty()) {
        m_edtRemark->setText(remark);
        markFilled(m_edtRemark);
        filled << QStringLiteral("备注");
    }

    // 图纸时间：优先取“日期/审核/审定…”标签后的日期，否则取全文第一个日期
    {
        QDate d;
        auto lm = QRegularExpression(QStringLiteral(
                      "(?:日期|审核|审定|批准|校对|设计|标准化|会签)[^0-9\\n]{0,4}"
                      "(20\\d{2}[0-9./年\\-]{2,10})"))
                      .match(compact);
        if (lm.hasMatch())
            d = parseOcrDate(lm.captured(1));
        if (!d.isValid())
            d = parseOcrDate(compact);
        if (d.isValid()) {
            m_edtDrawingDate->setDate(d);
            filled << QStringLiteral("图纸时间");
        }
    }
    // 特有字段中的“针数”
    {
        auto m = QRegularExpression(QStringLiteral("(\\d{1,3})针")).match(compact);
        if (m.hasMatch()) {
            const QString pins = m.captured(1) + QStringLiteral("针");
            for (auto &pair : m_extEdits) {
                if ((pair.first.contains(QStringLiteral("针"))
                     || pair.first.contains(QStringLiteral("pins")))
                    && pair.second->text().isEmpty()) {
                    pair.second->setText(pins);
                    markFilled(pair.second);
                    filled << pair.first;
                }
            }
        }
    }
    // 图纸类型：标题栏带“图纸外发专用章”的一律按下发图
    if (text.contains(QStringLiteral("会签")))
        setDrawingType(TypeCountersign);
    else if (text.contains(QStringLiteral("下发")) || text.contains(QStringLiteral("外发")))
        setDrawingType(TypeIssued);

    return filled;
}

// 载入已有产品进入编辑模式（管理页跳转）
void EntryDialog::loadProduct(int id)
{
    resetForm();
    switchToProductEntry(); // 补充信息 / 修改一律进「产品录入」，不沿用上次的数量录入子菜单
    const Product p = DatabaseManager::instance().productById(id);
    if (p.id < 0) return;
    m_editingId = id;
    m_pickedFile.clear();
    m_existingDrawingPath.clear();
    m_existingDrawingId = -1;
    m_pastedImage = QImage();
    m_pastedTempPath.clear();
    m_previewImage = QImage();
    m_fileLabel->clear();

    const int catIdx = m_cmbCategory->findText(p.category);
    if (catIdx >= 0) m_cmbCategory->setCurrentIndex(catIdx); // 触发特有字段重建
    rebuildExtFields();

    m_edtModel->setText(p.model);
    m_edtMaterial->setText(p.materialCode);
    m_edtName->setText(p.name);
    // 硬件/软件版本号现作为 ext 字段，由下方 m_extEdits 循环从 p.extFields 回填
    m_edtRemark->setText(p.remark);
    m_edtQty->setText(QString::number(p.qty));
    m_edtMinQty->setText(QString::number(p.minQty));
    m_edtMaxQty->setText(QString::number(p.maxQty));
    for (const auto &pair : m_extEdits) {
        const auto it = p.extFields.constFind(pair.first);
        if (it != p.extFields.constEnd())
            pair.second->setText(it.value());
    }

    // 「补充信息 / 修改」按本次操作重新登记：录入时间取当前时间，录入人取当前登录账号。
    // 两个字段仍只读，不再回填数据库里的旧值
    m_edtCreated->setText(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm")));
    m_edtCreator->setText(m_currentUser.isEmpty() ? p.creator : m_currentUser);

    // 已挂接图纸：全部生成缩略图，逐张可预览、可删除；默认展示第一张并带出其日期/类型
    m_existingDrawings = DatabaseManager::instance().drawingsOf(id);
    if (!m_existingDrawings.isEmpty()) {
        showExistingDrawing(m_existingDrawings.first().id); // 内部会刷新缩略图条
        // 刚切页进来时布局尚未完成，延后一拍再按实际尺寸重绘预览
        QTimer::singleShot(0, this, [this] { updatePreview(); });
    } else {
        refreshAttachList();
    }
}

void EntryDialog::onSave()
{
    if (m_edtName->text().trimmed().isEmpty() || m_edtModel->text().trimmed().isEmpty()) {
        NdMessageBox::warning(this, QStringLiteral("提示"),
                             QStringLiteral("名称 / 型号 为必填项（物料编码可后续补录）"));
        return;
    }

    Product p;
    p.category = m_cmbCategory->currentText();
    p.materialCode = m_edtMaterial->text().trimmed();
    p.name = m_edtName->text().trimmed();
    p.model = m_edtModel->text().trimmed();
    // 硬件/软件版本号改为控制器专属特有字段，随 m_extEdits 一同写入 product_ext（不再写通用列）
    p.remark = m_edtRemark->text().trimmed();
    p.creator = m_currentUser.isEmpty() ? m_edtCreator->text().trimmed() : m_currentUser;
    p.createdAt = QDateTime::currentDateTime(); // 本次操作时间，保存后刷新「录入时间」
    p.qty = m_edtQty->text().trimmed().toInt();
    p.minQty = m_edtMinQty->text().trimmed().toInt();
    p.maxQty = m_edtMaxQty->text().trimmed().toInt();
    for (const auto &pair : m_extEdits)
        p.extFields.insert(pair.first, pair.second->text().trimmed());

    DatabaseManager &db = DatabaseManager::instance();
    const bool editing = m_editingId >= 0;
    p.id = editing ? m_editingId : -1; // 编辑模式按 id 回写，改分类也不会另建一条

    // 编辑时改了分类：明确告知影响（记录会移动、原分类特有字段会清除），避免误存到错的分类
    if (editing) {
        const Product old = db.productById(m_editingId);
        if (old.id >= 0 && old.category != p.category) {
            const int clash = db.productIdByCategoryModel(p.category, p.model);
            if (clash >= 0 && clash != m_editingId) {
                NdMessageBox::warning(this, QStringLiteral("无法保存"),
                                      QStringLiteral("「%1」分类下已存在型号「%2」，请先修改型号再保存。")
                                          .arg(p.category, p.model));
                return;
            }
            QStringList oldValues;
            for (const FieldConfig &f : db.fieldConfigs(old.category)) {
                const QString v = old.extFields.value(f.fieldKey).trimmed();
                if (!v.isEmpty())
                    oldValues << QStringLiteral("%1：%2").arg(f.fieldLabel, v);
            }
            const QString tip = QStringLiteral(
                "分类已从「%1」改为「%2」。\n\n"
                "继续保存会：\n"
                "· 把这条记录移动到「%2」分类下（不会新建重复记录）\n"
                "· 清除不属于「%2」分类的特有字段%3\n"
                "· 按新分类保存本次填写的特有字段（需重新填写）\n\n是否继续？")
                .arg(old.category, p.category,
                     oldValues.isEmpty() ? QString()
                                         : QStringLiteral("：\n  ") + oldValues.join(QStringLiteral("\n  ")));
            if (!NdMessageBox::question(this, QStringLiteral("分类已更改"), tip))
                return;
        }
    }

    // 新增录入时若「分类+型号」已存在，先明确告知用户，避免静默覆盖已有信息。
    // 编辑模式是从管理页带着原数据进来的，不需要再确认。
    if (!editing) {
        const int existId = db.productIdByCategoryModel(p.category, p.model);
        if (existId >= 0) {
            const Product old = db.productById(existId);
            QStringList conflicts;
            const auto addConflict = [&conflicts](const QString &label,
                                                  const QString &oldValue, const QString &newValue) {
                if (!oldValue.isEmpty() && !newValue.isEmpty()
                    && oldValue.compare(newValue, Qt::CaseInsensitive) != 0)
                    conflicts << QStringLiteral("  %1：%2 → %3").arg(label, oldValue, newValue);
            };
            addConflict(QStringLiteral("物料编码"), old.materialCode, p.materialCode);
            addConflict(QStringLiteral("名称"), old.name, p.name);
            addConflict(QStringLiteral("备注"), old.remark, p.remark);

            const QString tip = QStringLiteral(
                "该分类下型号「%1」已录入过：\n"
                "产品：%2\n物料编码：%3\n\n"
                "继续保存将补充/更新这个产品：本次留空的字段保留原值，不会删除已有信息；"
                "填写了不同值的字段会被更新%4。\n是否继续？")
                .arg(p.model)
                .arg(old.name)
                .arg(old.materialCode.isEmpty() ? QStringLiteral("（空）") : old.materialCode)
                .arg(conflicts.isEmpty() ? QString()
                                         : QStringLiteral("：\n") + conflicts.join(QLatin1Char('\n')));
            if (!NdMessageBox::question(this, QStringLiteral("产品已存在"), tip))
                return;
        }
    }

    QString err;
    // 新增模式用「补充式更新」（空字段保留原值）；编辑模式允许主动清空字段
    const int id = db.saveProduct(p, m_edtChangeNote->text(), &err, !editing);
    if (id < 0) {
        NdMessageBox::critical(this, QStringLiteral("保存失败"), err);
        return;
    }

    // 图纸附件（可选，允许先导数据后补图纸；同一型号可挂多张：PDF + 图片）
    QStringList attachFiles = m_pickedFiles;
    if (!m_pastedImage.isNull()) {
        const QString pastedPath = ensureImageFile();
        if (!pastedPath.isEmpty())
            attachFiles.prepend(pastedPath);
    }
    if (attachFiles.isEmpty()) {
        // 编辑模式未重新选择/粘贴图纸：仅把可能改动的图纸日期、类型回写到已挂接记录
        if (m_editingId >= 0 && m_existingDrawingId >= 0) {
            if (!DatabaseManager::instance().updateDrawingMeta(
                    m_existingDrawingId, drawingType(), m_edtDrawingDate->date(), &err))
                NdMessageBox::warning(this, QStringLiteral("提示"),
                                     QStringLiteral("信息已保存，但图纸信息更新失败：%1").arg(err));
        }
    } else {
        // 逐个挂接；addDrawing 内部按内容 MD5 去重，重复拖同一张不会产生重复记录
        QStringList failed;
        for (const QString &f : attachFiles) {
            if (!DatabaseManager::instance().addDrawing(
                    id, f, drawingType(), m_edtDrawingDate->date(), &err))
                failed << QFileInfo(f).fileName();
        }
        if (!failed.isEmpty())
            NdMessageBox::warning(this, QStringLiteral("提示"),
                                 QStringLiteral("信息已保存，但以下图纸挂接失败：%1")
                                     .arg(failed.join(QStringLiteral("、"))));
    }

    NdMessageBox::information(this, QStringLiteral("提示"), QStringLiteral("保存成功"));
    emit saved();
}
