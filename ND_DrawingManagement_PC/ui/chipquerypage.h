#ifndef CHIPQUERYPAGE_H
#define CHIPQUERYPAGE_H

#include <QWidget>
#include <QList>
#include <QStringList>
#include "util/chiptypes.h"

class QLineEdit;
class QComboBox;
class QTableWidget;
class QLabel;
class QPushButton;
class QFrame;

// 工具 → 芯片查询：表格数据一次性导入 chip_item 表，之后在系统内维护。
// 未登录只读；登录后由 MainWindow 推入账号，开放新增 / 修改 / 删除。
// 两个查询方向：按硬件版本列出全部芯片；按芯片型号反查所有使用该芯片的硬件。
class ChipQueryPage : public QWidget
{
    Q_OBJECT
public:
    explicit ChipQueryPage(QWidget *parent = nullptr);

    // 空串 = 未登录；非空 = 已登录（账号名仅用于展示）
    void setLoggedIn(const QString &user);
    // 切到工具页时由 ToolsPage 调用：加载字典并刷新结果，避免启动时发起请求
    void refresh();

protected:
    void resizeEvent(QResizeEvent *event) override;

private slots:
    void onSearch();
    void onSetMode(bool byHardware);
    void onAdd();
    void onEdit();
    void onDelete();
    void onSave();
    void onCancel();
    void onQuickPicked(int index);
    void onBrandPicked(int index);
    void onCellDoubleClicked(int row, int column);

private:
    void buildUi();
    void fillTable();
    void updateSummary();
    void refreshDict();
    void sortItems();                  // 按硬件查询时按 主控/驱动/电源/功率/信号链 排
    void openEditor(const ChipItem &item, bool isNew);
    void positionEditor();
    void updateManageButtons();
    ChipItem collectForm() const;
    bool loggedIn() const { return !m_user.isEmpty(); }

    QString m_user;
    bool m_byHardware = true;          // true 按硬件查 / false 按芯片反查
    QList<ChipItem> m_items;           // 当前查询结果
    QList<ChipItem> m_dict;            // 已有芯片资料（按型号去重，快速添加用）
    QStringList m_hardwares;           // 已有硬件版本列表
    int m_total = 0;

    QPushButton *m_btnModeHw = nullptr;
    QPushButton *m_btnModeModel = nullptr;
    QLineEdit *m_edtKeyword = nullptr;
    QLabel *m_lblLogin = nullptr;
    QLabel *m_lblResult = nullptr;
    QLabel *m_lblSummary = nullptr;
    QTableWidget *m_table = nullptr;
    QPushButton *m_btnAdd = nullptr;
    QPushButton *m_btnEdit = nullptr;
    QPushButton *m_btnDelete = nullptr;

    // 右侧新增 / 编辑浮层
    QFrame *m_editor = nullptr;
    QLabel *m_lblEditorTitle = nullptr;
    QComboBox *m_cmbQuick = nullptr;
    QComboBox *m_cmbHardware = nullptr;
    QComboBox *m_cmbCategory = nullptr;   // 可选预设，也可直接输入新类型
    QLineEdit *m_edtModel = nullptr;
    QLineEdit *m_edtQty = nullptr;
    QLineEdit *m_edtFunction = nullptr;
    QLineEdit *m_edtCountry = nullptr;
    QComboBox *m_cmbBrand = nullptr;      // 选中已有品牌时自动带出国家
    QLineEdit *m_edtRemark = nullptr;
    int m_editId = 0;                 // 0 = 新增
    bool m_editorOpen = false;
    bool m_ready = false;             // buildUi 完成前不发起查询，避免启动时阻塞
};

#endif // CHIPQUERYPAGE_H
