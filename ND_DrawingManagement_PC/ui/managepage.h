#ifndef MANAGEPAGE_H
#define MANAGEPAGE_H

#include <QWidget>
#include <QList>

class QComboBox;
class QLineEdit;
class QTableWidget;
class QResizeEvent;
class MaterialCodeDialog;
class QtyManageDialog;

// 管理页：修改产品 / 调整更改记录 / 删除、批量删除
class ManagePage : public QWidget
{
    Q_OBJECT
public:
    explicit ManagePage(QWidget *parent = nullptr);

    void refresh(); // 重新加载表格

protected:
    void resizeEvent(QResizeEvent *event) override; // 遮罩层跟随页面大小

signals:
    void requestEditProduct(int productId); // 跳转录入页编辑

private slots:
    void onSearch();
    void onEdit();
    void onChangeLogs();
    void onDelete();
    void onBatchImport(); // 选文件夹，按型号批量挂接图纸
    void onSupplementMaterialCode(); // OCR 截图按型号补充物料编码
    void onQuantityManage(); // 数量管理：查看/修改每日数量与安全值

private:
    QList<int> selectedIds() const; // 当前选中行对应的产品 id

    QComboBox *m_cmbCategory = nullptr;
    QLineEdit *m_edtKeyword = nullptr;
    QTableWidget *m_table = nullptr;
    MaterialCodeDialog *m_codePanel = nullptr; // 物料编码补充（遮罩卡片，与关于窗口同款）
    QtyManageDialog *m_qtyDialog = nullptr;     // 数量管理（全屏半透明黑框）
};

#endif // MANAGEPAGE_H
