#ifndef CUTTASKPAGE_H
#define CUTTASKPAGE_H

#include <QWidget>
#include <QStringList>
#include "util/cuttypes.h"

class QPushButton;
class QComboBox;
class QTableWidget;
class QLabel;
class QTabWidget;
class QLineEdit;
class QDoubleSpinBox;
class QEvent;

// 线束 → 下线任务页：下线表导入、拆任务、派工、进度看板
// 服务器/客户端数据访问统一走 DatabaseManager（客户端自动转发 RemoteApiClient）。
class CutTaskPage : public QWidget
{
    Q_OBJECT
public:
    explicit CutTaskPage(QWidget *parent = nullptr);

    void onPageShown();
    void setLoggedIn(const QString &user);

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;

private slots:
    void onPickExcel();
    void onImport();
    void onRefresh();
    void onAutoDispatch();     // 智能分配 → 生成可调整的预览（不写库）
    void onCommitDispatch();   // 确认派工 → 把预览正式写成任务
    void onTaskCellChanged(int row, int column);  // 预览行的机台格可改
    void onTaskStatusChanged(int row, int column);  // 正式任务状态手动改为「已完成」
    // 批次管理
    void onDeleteBatch();
    void refreshBatchTable();
    // 派单规则
    void onRuleAdd();      // 新增规则（始终插入一条新规则）
    void onRuleSave();     // 保存修改（仅对表格中选中的规则生效）
    void onRuleToggle();   // 启用 / 禁用选中的规则
    void onRuleDelete();
    void onRuleRowClicked(int row, int column);
    void onRuleReset();

private:
    QWidget *buildImportTab();
    QWidget *buildTaskTab();
    QWidget *buildRuleTab();
    void refreshBatches();
    void refreshTasks();
    void refreshRules();
    void updateRuleInputs();   // 线径用范围输入，其余字段用匹配方式+匹配值
    void updateRuleButtons();  // 按「是否选中规则 / 是否登录」刷新按钮可用状态
    bool collectRuleForm(CutRule *rule);   // 读取表单，失败时弹提示并返回 false
    // 解析实际下线表 .xls：前几行是标题/参数，含「序号 回路编号 导线材质…」表头行，
    // 表头之后为数据行。warnings 返回原表数据被污染（如长度写成「取消」）的行的处理说明。
    bool parseExcel(const QString &path, QString *modelName, QString *batchNo,
                    QList<CutItem> *items, QString *err, QStringList *warnings) const;
    void showPreview(const QList<CutItem> &items);

    QTabWidget *m_tabs = nullptr;

    // 导入 Tab
    QLineEdit *m_edtExcelPath = nullptr;
    QPushButton *m_btnPick = nullptr;
    QPushButton *m_btnImport = nullptr;
    QLabel *m_lblImportInfo = nullptr;
    QTableWidget *m_previewTable = nullptr;
    // 已导入批次管理
    QTableWidget *m_batchTable = nullptr;
    QPushButton *m_btnBatchDelete = nullptr;
    QPushButton *m_btnBatchRefresh = nullptr;
    QLabel *m_lblBatchInfo = nullptr;

    // 任务/派工 Tab
    QComboBox *m_cmbBatch = nullptr;
    QComboBox *m_cmbMachine = nullptr;
    QComboBox *m_cmbStatus = nullptr;
    QPushButton *m_btnRefresh = nullptr;
    QPushButton *m_btnAutoDispatch = nullptr;  // 智能分配（生成预览）
    QPushButton *m_btnCommit = nullptr;        // 确认派工（把预览写成任务）
    QTableWidget *m_taskTable = nullptr;
    QLabel *m_lblTaskInfo = nullptr;
    // 智能分配预览：id=0 的虚拟任务行，机台列可改，确认后才写库
    QList<CutTask> m_previewTasks;
    int m_previewBatchId = 0;
    int m_previewQty = 1;

    // 派单规则 Tab
    QWidget *m_widgetRuleRange = nullptr;    // 线径范围行（仅线径字段显示）
    QWidget *m_widgetRuleMatch = nullptr;    // 匹配方式+匹配值行（非线径字段显示）
    QComboBox *m_cmbRuleField = nullptr;     // 线径 / 端子 / 防水栓 …
    QComboBox *m_cmbRuleMatch = nullptr;     // 精确 / 包含 / 前缀（线径不适用）
    QLineEdit *m_edtRuleValue = nullptr;     // 匹配值，如 PP2011101
    QDoubleSpinBox *m_spinRuleMin = nullptr; // 线径范围下限
    QDoubleSpinBox *m_spinRuleMax = nullptr; // 线径范围上限
    QComboBox *m_cmbRuleMachine = nullptr;   // 目标机台
    QPushButton *m_btnRuleAdd = nullptr;     // 新增规则
    QPushButton *m_btnRuleSave = nullptr;    // 保存修改
    QPushButton *m_btnRuleToggle = nullptr;  // 启用 / 禁用
    QPushButton *m_btnRuleDelete = nullptr;
    QTableWidget *m_ruleTable = nullptr;
    QLabel *m_lblRuleInfo = nullptr;
    int m_editingRuleId = 0;        // >0 表示正在编辑已有规则
    int m_editingRuleEnabled = 1;   // 正在编辑规则的启用状态，保存时原样带回
    bool m_ruleEditable = false;    // 已登录才允许增删改

    QString m_excelPath;
    QString m_currentUser;
    QString m_parsedModel;         // 从标题解析出的线束型号，用作批次名
    QString m_parsedBatchNo;       // 标签上的批次号
    QList<CutItem> m_parsedItems;  // 当前文件解析出的明细，导入时入库
};

#endif // CUTTASKPAGE_H
