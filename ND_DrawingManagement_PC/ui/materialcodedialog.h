#ifndef MATERIALCODEDIALOG_H
#define MATERIALCODEDIALOG_H

#include "overlaymask.h"
#include <QList>
#include <QStringList>

class QLabel;
class QListWidget;
class QProgressBar;
class QPushButton;
class QTableWidget;
class QCheckBox;
class QPlainTextEdit;
class QShortcut;
class QVBoxLayout;
class QDateEdit;

// 物料编码补充：与「关于窗口」同一套风格（半透明遮罩 + 居中卡片，点遮罩空白处关闭）。
// 把「物料编码 / 物料描述」表格截图拖进拖放区（可多张，约 8 张），OCR 后按型号回填物料编码。
// - 型号在库中不存在：跳过，不改动数据库；
// - 编码为空：直接补充；
// - 已有编码且不同：默认不改动，勾选「覆盖已有编码」后才更新；
// - 识别编码位数与库中编码不符：判定「编码识别错误」，一律不写入数据库。
class MaterialCodeDialog : public OverlayMask
{
    Q_OBJECT
public:
    enum Mode { MaterialCode, Quantity }; // 物料编码补充 / 数量录入
    explicit MaterialCodeDialog(QWidget *parent = nullptr, Mode mode = MaterialCode,
                                bool embedded = false);

    // 显式开关 Ctrl+V 粘贴（内联嵌入时由外层根据当前 Tab 切换）
    void setPasteEnabled(bool on);

signals:
    void closed(); // 面板关闭，调用方据此刷新列表

public slots:
    void showPanel(); // 铺满父窗口并置顶显示（仅覆盖式面板使用）

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragLeaveEvent(QDragLeaveEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    struct Row {
        QString code;        // 识别出的物料编码
        QString model;       // 识别出的型号
        QString productInfo; // 匹配到的产品（分类/名称），供人工核对
        int productId = -1;  // -1 表示库中未收录该型号
        bool incomplete = false; // 截图被截断 / OCR 尾部噪声，型号可能不完整
        QString status;      // 待补充 / 编码一致 / 编码识别错误 / 已有编码 / 未收录型号（跳过）
        int qty = 0;         // 数量模式：从截图解析出的数量
        int seq = 0;         // 截图里的表格序号，供人工核对（0=未识别到）
        bool byCode = false; // 型号没匹配上时，是否按物料编码反查到了产品
    };

    void addImages(const QStringList &paths);
    void refreshImageList();
    void onPickImages();
    void onRemoveSelected();
    void onClear();
    void onStartOcr();
    void onApply();
    void pasteFromClipboard(); // Ctrl+V 粘贴剪贴板里的截图
    void closePanel();
    void highlightDropArea(bool on);

    bool runOcr(const QString &path, QString *text);
    int parseAndAppend(const QString &text); // 返回未配对条数
    void mergeDuplicateRows(bool logToRaw); // 跨图片合并同一物料编码，数量累加
    void rebuildTable();

    QWidget *m_card = nullptr;
    QLabel *m_dropArea = nullptr;
    QListWidget *m_imgList = nullptr;
    QLabel *m_status = nullptr;
    QProgressBar *m_progress = nullptr;
    QTableWidget *m_table = nullptr;
    QPlainTextEdit *m_ocrRaw = nullptr; // 右侧：OCR 识别到的全部原始文本
    QCheckBox *m_chkOverwrite = nullptr;     // 数量模式=覆盖数量；编码模式=覆盖编码
    QCheckBox *m_chkOverwriteCode = nullptr; // 数量模式：覆盖已有编码
    QDateEdit *m_qtyDate = nullptr;          // 数量模式：本次录入日期（供用量统计）
    QPushButton *m_btnOcr = nullptr;
    QPushButton *m_btnApply = nullptr;
    QShortcut *m_pasteShortcut = nullptr;    // Ctrl+V，仅在面板显示时启用

    QStringList m_images;
    QList<Row> m_rows;

    Mode m_mode = MaterialCode;  // 当前模式
    bool m_embedded = false;     // true=内联嵌入（无遮罩、不点击关闭）
    bool m_saving = false;       // 正在写库：拦截嵌套事件循环里的重复提交
};

#endif // MATERIALCODEDIALOG_H
