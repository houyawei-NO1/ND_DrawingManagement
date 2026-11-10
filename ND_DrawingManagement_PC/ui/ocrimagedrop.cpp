#include "ocrimagedrop.h"
#include "ocrhelper.h"

#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QUrl>
#include <QImage>
#include <QPixmap>
#include <QVariant>
#include <QFileInfo>
#include <QDir>
#include <QDateTime>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QToolButton>
#include <QMouseEvent>
#include <QGuiApplication>
#include <QClipboard>

OcrImageDrop::OcrImageDrop(QWidget *parent) : QFrame(parent)
{
    setObjectName(QStringLiteral("ocrDrop"));
    setFixedSize(232, 34);
    setAcceptDrops(true);
    setAttribute(Qt::WA_StyledBackground, true);
    setCursor(Qt::PointingHandCursor);
    setToolTip(QStringLiteral(
        "把订单截图拖到这里，或复制图片后按 Ctrl+V / 点这里粘贴，自动识别型号并搜索；"
        "点右侧叉号清除"));

    auto *lay = new QHBoxLayout(this);
    lay->setContentsMargins(7, 2, 4, 2);
    lay->setSpacing(7);

    m_thumb = new QLabel(this);
    m_thumb->setFixedSize(22, 20);
    m_thumb->setVisible(false);
    lay->addWidget(m_thumb);

    auto *textCol = new QWidget(this);
    textCol->setObjectName(QStringLiteral("ocrDropText"));
    auto *vlay = new QVBoxLayout(textCol);
    vlay->setContentsMargins(0, 0, 0, 0);
    vlay->setSpacing(0);
    m_title = new QLabel(QStringLiteral("图片拖入，Ctrl+V粘贴"), textCol);
    m_title->setObjectName(QStringLiteral("ocrDropTitle"));
    m_sub = new QLabel(textCol);
    m_sub->setObjectName(QStringLiteral("ocrDropSub"));
    m_sub->setVisible(false);
    vlay->addWidget(m_title);
    vlay->addWidget(m_sub);
    lay->addWidget(textCol, 1);

    m_badge = new QLabel(this);
    m_badge->setObjectName(QStringLiteral("ocrDropBadge"));
    m_badge->setVisible(false);
    m_badge->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    lay->addWidget(m_badge);

    m_clear = new QToolButton(this);
    m_clear->setIcon(OcrHelper::makeClearIcon());
    m_clear->setIconSize(QSize(16, 16));
    m_clear->setToolTip(QStringLiteral("清除已拖入的图片"));
    m_clear->setCursor(Qt::PointingHandCursor);
    m_clear->setVisible(false);
    m_clear->setAttribute(Qt::WA_Hover, true);
    lay->addWidget(m_clear);

    connect(m_clear, &QToolButton::clicked, this, [this] { clearImage(); });
    refreshStyle();
}

void OcrImageDrop::refreshStyle()
{
    const bool has = !m_path.isEmpty();
    // 与产品录入页「选择图纸文件」拖放区同款：2px 灰蓝虚线 + 8px 圆角。
    // 全局 QSS 有 QWidget{background:#2B3046}，会让文字容器露出一块实心方框，
    // 这里单独把容器和所有子控件设成透明。
    // 文字统一用搜索框 placeholder 的灰白 #91949E（即 rgba(255,255,255,0.5) 叠在深色底上），
    // 标题、副文字、状态标签都不再单独着色，避免出现两种文字颜色。
    const QString border = (m_hover || has)
        ? QStringLiteral("2px solid #43CF7C")
        : QStringLiteral("2px dashed #4A5468");
    const QString bg = m_hover
        ? QStringLiteral("rgba(67,207,124,0.16)")
        : (has ? QStringLiteral("rgba(67,207,124,0.08)")
               : QStringLiteral("rgba(0,0,0,0.15)"));
    const QString textColor = QStringLiteral("#91949E");
    setStyleSheet(QStringLiteral(
        "#ocrDrop{background:%1;border:%2;border-radius:8px;}"
        "#ocrDrop #ocrDropText{background:transparent;}"
        "#ocrDrop QLabel{background:transparent;}"
        "#ocrDropTitle{color:%3;font-size:11px;}"
        "#ocrDropSub{color:%3;font-size:9px;}"
        "#ocrDrop #ocrDropBadge{color:%3;font-size:9px;padding:0 2px;}"
        "#ocrDrop QToolButton{background:transparent;border:none;padding:0;}"
        "#ocrDrop QToolButton:hover{background:rgba(255,82,82,0.16);border-radius:10px;}")
        .arg(bg, border, textColor));
}

void OcrImageDrop::setImage(const QString &path)
{
    m_path = path;
    const QPixmap src(path);
    if (!src.isNull()) {
        m_thumb->setPixmap(src.scaled(m_thumb->size(), Qt::KeepAspectRatio,
                                      Qt::SmoothTransformation));
    } else {
        m_thumb->clear();
    }
    m_thumb->setVisible(!src.isNull());
    m_title->setText(QFileInfo(path).fileName());
    m_title->setToolTip(path);
    m_sub->setText(QStringLiteral("拖入或粘贴其它图片可替换"));
    m_sub->setVisible(true);
    m_clear->setVisible(true);
    m_badge->setText(QStringLiteral("已拖入"));
    m_badge->setVisible(true);
    m_busy = false;
    m_hover = false;
    refreshStyle();
}

void OcrImageDrop::setBusy(bool on)
{
    if (m_path.isEmpty())
        return;
    m_busy = on;
    m_badge->setText(on ? QStringLiteral("识别中…") : QStringLiteral("已识别"));
    m_badge->setVisible(true);
    m_sub->setText(on ? QStringLiteral("正在识别订单图片…")
                      : QStringLiteral("拖入或粘贴其它图片可替换"));
}

void OcrImageDrop::clearImage()
{
    if (m_path.isEmpty())
        return;
    m_path.clear();
    m_busy = false;
    m_hover = false;
    m_thumb->clear();
    m_thumb->setVisible(false);
    m_title->setText(QStringLiteral("图片拖入，Ctrl+V粘贴"));
    m_title->setToolTip(QString());
    m_sub->clear();
    m_sub->setVisible(false);
    m_badge->setVisible(false);
    m_clear->setVisible(false);
    refreshStyle();
    emit cleared();
}

void OcrImageDrop::dragEnterEvent(QDragEnterEvent *event)
{
    const QMimeData *mime = event->mimeData();
    if (!mime)
        return;
    bool ok = mime->hasImage();
    if (!ok && mime->hasUrls()) {
        for (const QUrl &u : mime->urls()) {
            if (OcrHelper::isImageFile(u.toLocalFile())) {
                ok = true;
                break;
            }
        }
    }
    if (!ok)
        return;
    event->acceptProposedAction();
    m_hover = true;
    refreshStyle();
}

void OcrImageDrop::dragLeaveEvent(QDragLeaveEvent *event)
{
    QFrame::dragLeaveEvent(event);
    m_hover = false;
    refreshStyle();
}

// 拖入和粘贴共用：优先本地图片文件，其次剪贴板/拖拽位图（无损存成临时 PNG），
// 最后兼容剪贴板里的图片路径文本。
QString OcrImageDrop::imagePathFromMime(const QMimeData *mime)
{
    if (!mime)
        return QString();
    if (mime->hasUrls()) {
        for (const QUrl &u : mime->urls()) {
            const QString p = u.toLocalFile();
            if (QFileInfo::exists(p) && OcrHelper::isImageFile(p))
                return p;
        }
    }
    if (mime->hasImage()) {
        const QImage img = qvariant_cast<QImage>(mime->imageData());
        if (!img.isNull()) {
            const QString tmp = QDir::tempPath()
                + QStringLiteral("/nd_main_ocr_%1.png")
                      .arg(QDateTime::currentDateTime().toString(
                          QStringLiteral("yyyyMMdd_HHmmss_zzz")));
            if (img.save(tmp, "PNG"))
                return tmp;
        }
    }
    if (mime->hasText()) {
        const QString p = mime->text().trimmed().remove(QLatin1Char('"'));
        if (QFileInfo::exists(p) && OcrHelper::isImageFile(p))
            return p;
    }
    return QString();
}

void OcrImageDrop::dropEvent(QDropEvent *event)
{
    m_hover = false;
    const QString path = imagePathFromMime(event->mimeData());
    if (!path.isEmpty()) {
        event->acceptProposedAction();
        setImage(path);
        emit imageDropped(path);
        return;
    }
    refreshStyle();
}

void OcrImageDrop::pasteFromClipboard()
{
    const QString path = imagePathFromMime(QGuiApplication::clipboard()->mimeData());
    if (path.isEmpty())
        return; // 剪贴板是文字时静默跳过，不打断用户在别处的粘贴
    setImage(path);
    emit imageDropped(path);
}

void OcrImageDrop::mousePressEvent(QMouseEvent *event)
{
    QFrame::mousePressEvent(event);
    if (event->button() == Qt::LeftButton && m_path.isEmpty())
        pasteFromClipboard(); // 点空框等同于「粘贴」
}
