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
class QShortcut;
class MaterialCodeDialog;

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
    void switchToProductEntry(); // 切到「产品录入」子菜单（补充信息 / 修改入口）

signals:
    void saved();       // 保存成功
    void cancelled();   // 用户取消
    void drawingsChanged(); // 编辑页删除了已挂接图纸，主界面/管理页需同步刷新

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
    void setEntryTab(int idx);   // 切换「产品录入 / 数量录入」并同步 Ctrl+V 归属
    void rebuildExtFields();
    void clearSupplement();                  // 换图纸时清空上一张的补充信息
    void setPickedFile(const QString &path); // 设置图纸文件并刷新预览
    void addPickedFiles(const QStringList &paths); // 追加多张图纸（首个负责预览+识别）
    // fromExisting=true 表示预览的是已挂接图纸，不重置 m_existingDrawingId
    void previewFile(const QString &path, bool fromExisting = false);
    void refreshAttachList();    // 同步缩略图条：已挂接图纸 + 本次新增附件，每张都能预览/删除
    void clearFilePreview();     // 清空大预览与文件名提示
    void showExistingDrawing(int drawingId);      // 预览某张已挂接图纸，并带出其日期/类型
    void onDeleteExistingDrawing(int drawingId);  // 删除某张已挂接图纸（记录 + 归档文件）
    void onDeletePickedFile(const QString &path); // 从本次新增附件中移除
    void onDeleteCurrentDrawing();                // 大预览区右上角删除键
    void updateCurrentDeleteButton();             // 按当前预览来源刷新删除键文字/可见性
    void onAttachClicked(QListWidgetItem *item);  // 点击缩略图条目：切换预览
    void onAttachContextMenu(const QPoint &pos);  // 缩略图右键：预览/删除
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
    QPushButton *m_btnDeleteCurrent = nullptr; // 大预览右上角：删除/移除当前图纸
    QLabel *m_thumbTitle = nullptr;          // 缩略图条标题（共几张、如何删除）
    QListWidget *m_attachList = nullptr;     // 缩略图条：已挂接图纸 + 本次新增附件（每张带删除键）
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
    QString m_existingDrawingPath; // 当前预览的已挂接图纸路径（仅预览/识别，不算新附件）
    int m_existingDrawingId = -1;  // 当前预览/可回写的已挂接图纸记录 id，-1 表示预览的是新附件
    QList<Drawing> m_existingDrawings; // 编辑模式：产品已挂接的全部图纸（缩略图条数据源）
    QImage m_pastedImage;    // 粘贴的图片先放内存，避免大图编码卡顿
    QString m_pastedTempPath;
    QStringList m_heifTemps;   // HEIC 解码转存的临时 JPG（图纸落盘后清理）
    QString m_currentUser;
    int m_editingId = -1; // 编辑模式：正在编辑的产品 id，-1 为新增

    QStackedWidget *m_tabs = nullptr;   // 产品录入 / 数量录入 两个页
    QButtonGroup *m_tabGroup = nullptr; // 顶部 Tab 按钮组
    QShortcut *m_pasteShortcut = nullptr;      // 产品录入页的 Ctrl+V
    MaterialCodeDialog *m_qtyPanel = nullptr;  // 数量录入页内嵌面板（切页时开关其 Ctrl+V）
};

#endif // ENTRYDIALOG_H
