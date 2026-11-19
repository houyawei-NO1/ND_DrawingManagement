#ifndef MOLDQUERYPAGE_H
#define MOLDQUERYPAGE_H

#include <QWidget>
#include <QStringList>
#include "util/moldtypes.h"

class QLineEdit;
class QPushButton;
class QComboBox;
class QTableWidget;
class QLabel;
class QTextBrowser;
struct MoldItem;

// 线束 → 模具端子查询页：风格仿图纸查询（搜索栏 + 表格 + 右侧详情）。
// 只读，数据来自 mold_item；供车间/技术员按端子型号、模具号、线径快速反查。
class MoldQueryPage : public QWidget
{
    Q_OBJECT
public:
    explicit MoldQueryPage(QWidget *parent = nullptr);
    void onPageShown();

private slots:
    void onSearch();
    void onTableClicked(int row, int column);

private:
    QWidget *buildSearchBar();
    void refreshTable();
    void showDetail(const MoldItem &m);

    QLineEdit *m_edtKeyword = nullptr;
    QComboBox *m_cmbMachineType = nullptr;
    QTableWidget *m_table = nullptr;
    QLabel *m_lblInfo = nullptr;
    QTextBrowser *m_detail = nullptr;
    QList<MoldItem> m_items;
};

#endif // MOLDQUERYPAGE_H
