#ifndef MOLDTERMINALPAGE_H
#define MOLDTERMINALPAGE_H

#include <QWidget>
#include <QStringList>
#include "util/moldtypes.h"

class QLineEdit;
class QPushButton;
class QComboBox;
class QTableWidget;
class QLabel;

// 线束 → 模具端子页：模具台账与端子适配关系录入、查询
// 数据允许不全：模具号可空（老模具/刀片），用 data_status 标记「待完善」。
// 服务器/客户端数据访问统一走 DatabaseManager（客户端自动转发 RemoteApiClient）。
class MoldTerminalPage : public QWidget
{
    Q_OBJECT
public:
    explicit MoldTerminalPage(QWidget *parent = nullptr);

    // 切到本页时刷新列表
    void onPageShown();

    // 登录态变化由 MainWindow 推入：未登录只读，登录后可增删改
    void setLoggedIn(const QString &user);

private slots:
    void onSearch();
    void onAdd();
    void onEdit();
    void onDelete();
    void onTableDoubleClicked(int row, int column);

private:
    QWidget *buildSearchBar();
    QWidget *buildTableArea();
    void refreshTable();
    void fillTable(const QList<QStringList> &rows);
    bool openEditor(MoldItem *item); // 新增/编辑对话框，返回是否保存
    int currentRowId() const;

    QLineEdit *m_edtKeyword = nullptr;
    QComboBox *m_cmbStatus = nullptr;
    QPushButton *m_btnSearch = nullptr;
    QPushButton *m_btnAdd = nullptr;
    QPushButton *m_btnEdit = nullptr;
    QPushButton *m_btnDelete = nullptr;
    QTableWidget *m_table = nullptr;
    QLabel *m_lblInfo = nullptr;

    QString m_currentUser;
};

#endif // MOLDTERMINALPAGE_H
