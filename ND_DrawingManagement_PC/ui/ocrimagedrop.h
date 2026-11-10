#ifndef OCRIMAGEDROP_H
#define OCRIMAGEDROP_H

#include <QFrame>

class QLabel;
class QToolButton;
class QMimeData;
class QMouseEvent;

// 工具栏里的「图片拖入，Ctrl+V粘贴」框：沿用产品录入页的虚线拖放区风格。
// 接收本地图片文件、剪贴板图片；有图时显示缩略图、文件名与搜索框同款的清除叉号。
class OcrImageDrop : public QFrame
{
    Q_OBJECT
public:
    explicit OcrImageDrop(QWidget *parent = nullptr);

    QString imagePath() const { return m_path; }
    void setBusy(bool on);        // 识别中… / 已识别
    void clearImage();            // 清除图片并发出 cleared()
    void pasteFromClipboard();    // Ctrl+V / 点击框：从剪贴板取图片并自动识别

signals:
    void imageDropped(const QString &path);
    void cleared();

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragLeaveEvent(QDragLeaveEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;

private:
    void setImage(const QString &path);
    void refreshStyle();
    static QString imagePathFromMime(const QMimeData *mime);

    QLabel *m_thumb = nullptr;
    QLabel *m_title = nullptr;
    QLabel *m_sub = nullptr;
    QLabel *m_badge = nullptr;
    QToolButton *m_clear = nullptr;
    QString m_path;
    bool m_busy = false;
    bool m_hover = false;
};

#endif // OCRIMAGEDROP_H
