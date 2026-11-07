#ifndef ENTRYDIALOG_H
#define ENTRYDIALOG_H

#include <QWidget>
#include <QMap>
#include <QList>
#include <QPair>
#include <QDate>
#include <QImage>
#include <QStringList>
#include "model/product.h"

class QComboBox;
class QButtonGroup;
class QLineEdit;
class QDateEdit;
class QPushButton;
class QLabel;
class QVBoxLayout;
class QProgressBar;
class QPlainTextEdit;
class QGroupBox;
class QProcess;
class QTimer;
class QFormLayout;
class QShowEvent;
class QStackedWidget;
class QPdfDocument;
class QPdfView;
class QListWidget;
class QListWidgetItem;

// 图纸录入页：选文件 → 填信息（特有字段随分类动态生成）→ 保存
class EntryDialog : public QWidget
{
    Q_OBJECT
public:
    explicit EntryDialog(QWidget *parent = nullptr);
    ~EntryDialog() override;

    void resetForm();   // 清空表单，准备下一次录入
    void loadProduct(int id); // 载入已有产品进入编辑模式（管理页跳转）
    void setCurrentUser(const QString &name); // 设置当前登录用户（录入人自动填）

signals:
    void saved();       // 保存成功
    void cancelled();   // 用户取消

private slots:
    void onCategoryChanged(int index);
    void onPickFile();
    void onPaste();     // 从剪贴板粘贴图片/图纸文件
    void onOcr();       // 识别图纸：图片/PDF 转文字并自动填表
    void onSave();

protected:
    // 支持把图纸文件直接拖进窗口
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    void rebuildExtFields();
    void clearSupplement();                  // 换图纸时清空上一张的补充信息
    void setPickedFile(const QString &path); // 设置图纸文件并刷新预览
    void addPickedFiles(const QStringList &paths); // 追加多张图纸（首个负责预览+识别）
    void previewFile(const QString &path);         // 仅刷新预览，不触发识别
    void refreshAttachList();                      // 同步附件列表显示
    void onAttachClicked(QListWidgetItem *item);   // 点击附件：切换预览
    void onAttachContextMenu(const QPoint &pos);   // 附件右键：预览/移除
    void updatePreview();                    // 按当前预览区尺寸等比缩放显示图纸
    void searchOcrText(const QString &text);  // OCR 原文搜索：高亮并定位匹配
    QString ensureImageFile();               // 粘贴的图片在保存/识别时才落盘，返回文件路径
    void clearHeifTemps();                   // 清理 HEIC 转码产生的临时 JPG
    void pasteFromClipboard();               // 实际读取剪贴板内容（由进度条包裹调用）
    void autoOcrAfterLoad();                 // 图纸加载完成后自动识别（新增模式）
    void cancelRunningOcr();                 // 换图纸时静默中止上一次识别
    void startOcr(const QString &imagePath); // 异步启动离线识别引擎
    void onOcrFinished();                    // 识别进程结束：解析结果并填表
    QStringList applyOcrText(const QString &text, const QString &fileName = QString()); // 按规则从 OCR 文本提取字段并回填；fileName 用作辅助识别
    void beginBusy(QProgressBar *bar, QLabel *label, const QString &text); // 显示进度条
    void endBusy(QProgressBar *bar, QLabel *label);                        // 隐藏进度条
    int drawingType() const;                             // 当前图纸类型
    void setDrawingType(int t);

    QComboBox *m_cmbCategory = nullptr;
    QButtonGroup *m_dtypeGroup = nullptr; // 图纸类型分段按钮
    QLineEdit *m_edtModel = nullptr;
    QLineEdit *m_edtMaterial = nullptr;
    QLineEdit *m_edtName = nullptr;
    QLineEdit *m_edtRemark = nullptr;
    QLineEdit *m_edtChangeNote = nullptr;
    QDateEdit *m_edtDrawingDate = nullptr; // 图纸时间
    QLineEdit *m_edtCreated = nullptr;     // 录入时间（只读）
    QLineEdit *m_edtCreator = nullptr;
    QLineEdit *m_edtQty = nullptr;         // 数量
    QLineEdit *m_edtMinQty = nullptr;      // 最小数量
    QLineEdit *m_edtMaxQty = nullptr;      // 最大数量

    QLabel *m_fileLabel = nullptr;
    QLabel *m_filePreview = nullptr;         // 图纸缩略预览 / 拖放区（图片用）
    QStackedWidget *m_previewStack = nullptr;// 图片 QLabel 与 PDF QPdfView 切换
    QPdfDocument *m_pdfDoc = nullptr;        // PDF 预览文档
    QPdfView *m_pdfView = nullptr;           // PDF 预览视图
    QListWidget *m_attachList = nullptr;     // 已附加图纸列表（同型号可多张：pdf + 图片）
    QImage m_previewImage;                   // 原始图纸图像，窗口变化时重新缩放
    QPushButton *m_btnOcr = nullptr;         // 识别图纸

    QProgressBar *m_pasteBar = nullptr;      // 粘贴进度
    QLabel *m_pasteLabel = nullptr;
    QProgressBar *m_ocrBar = nullptr;        // 识别进度
    QLabel *m_ocrLabel = nullptr;
    QGroupBox *m_ocrGroup = nullptr;         // 右下角 OCR 原文
    QPlainTextEdit *m_ocrText = nullptr;
    QLineEdit *m_ocrSearch = nullptr;         // OCR 原文内快速定位输入框
    QProcess *m_ocrProc = nullptr;           // 运行中的识别进程
    QTimer *m_ocrTimeout = nullptr;

    QFormLayout *m_form = nullptr;    // 主表单布局（特有字段动态插入其中）
    int m_extFirstRow = -1;           // 特有字段插入的起始行
    QList<QPair<QString, QLineEdit *>> m_extEdits; // fieldKey -> 输入框

    QString m_pickedFile;    // 当前预览/识别的那张
    QStringList m_pickedFiles; // 全部待保存图纸（多附件，含 m_pickedFile）
    QString m_existingDrawingPath; // 编辑模式：产品已挂接的图纸路径（仅预览/识别，不算新附件）
    int m_existingDrawingId = -1;  // 编辑模式：当前展示/可回写的图纸记录 id
    QImage m_pastedImage;    // 粘贴的图片先放内存，避免大图编码卡顿
    QString m_pastedTempPath;
    QStringList m_heifTemps;   // HEIC 解码转存的临时 JPG（图纸落盘后清理）
    QString m_currentUser;
    int m_editingId = -1; // 编辑模式：正在编辑的产品 id，-1 为新增

    QStackedWidget *m_tabs = nullptr;   // 产品录入 / 数量录入 两个页
    QButtonGroup *m_tabGroup = nullptr; // 顶部 Tab 按钮组
};

#endif // ENTRYDIALOG_H
