#ifndef QTYMANAGEDIALOG_H
#define QTYMANAGEDIALOG_H

#include "overlaymask.h"
#include <QList>

class QLabel;
class QLineEdit;
class QTableWidget;

// 数量管理：全屏半透明黑遮罩，查看/修改某产品每日录入数量与最小/最大数量
class QtyManageDialog : public OverlayMask
{
    Q_OBJECT
public:
    explicit QtyManageDialog(QWidget *parent = nullptr);
    void loadProduct(int productId); // 载入指定产品并刷新
    void showPanel();                // 全屏遮罩并显示

signals:
    void closed();

private slots:
    void onSaveSafe();   // 保存最小/最大数量
    void reloadLogs();   // 刷新每日数量表
    void closePanel();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    int m_pid = -1;
    QLabel *m_title = nullptr;
    QLabel *m_curQtyLabel = nullptr;
    QLineEdit *m_minEdit = nullptr;
    QLineEdit *m_maxEdit = nullptr;
    QTableWidget *m_logTable = nullptr;
};

#endif // QTYMANAGEDIALOG_H
