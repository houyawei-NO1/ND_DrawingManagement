#ifndef CHANGELOGDIALOG_H
#define CHANGELOGDIALOG_H

#include "overlaymask.h"

class QLabel;
class QTableWidget;
class QWidget;
class QObject;
class QEvent;
class QKeyEvent;

// 更改记录：全屏 70% 黑遮罩 + 居中卡片，与工程内其它弹窗统一。
// 数据来自 change_log，支持逐条修改/删除；关闭不做全表刷新（表格列不依赖该表）。
class ChangeLogDialog : public OverlayMask
{
public:
    explicit ChangeLogDialog(QWidget *parent = nullptr);

    void loadProduct(int productId); // 载入某产品的更改记录
    void showPanel();                // 铺满屏幕显示
    void closePanel();               // 隐藏（点空白 / Esc /「关 闭」）

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    void reloadLogs();

    QWidget *m_card = nullptr;
    QLabel *m_title = nullptr;
    QTableWidget *m_table = nullptr;
    int m_pid = -1;
};

#endif // CHANGELOGDIALOG_H
