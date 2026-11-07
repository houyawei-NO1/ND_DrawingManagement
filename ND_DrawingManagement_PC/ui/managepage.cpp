#include "managepage.h"
#include "db/databasemanager.h"
#include "image/heifconverter.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QComboBox>
#include <QLineEdit>
#include <QTableWidget>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QPushButton>
#include <QLabel>
#include "ndmessagebox.h"
#include "materialcodedialog.h"
#include "qtymanage.h"
#include <QInputDialog>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QApplication>
#include <QResizeEvent>
#include <functional>

ManagePage::ManagePage(QWidget *parent) : QWidget(parent)
{
    QVBoxLayout *root = new QVBoxLayout(this);
    root->setContentsMargins(14, 12, 14, 12);
    root->setSpacing(10);

    // 顶部：搜索框（默认搜所有分类）+ 搜索按钮 + 分类筛选 + 操作按钮
    QHBoxLayout *top = new QHBoxLayout();

    m_edtKeyword = new QLineEdit(this);
    m_edtKeyword->setPlaceholderText(QStringLiteral("型号 / 物料编码 / 名称…（默认搜索所有分类）"));
    m_edtKeyword->setFixedWidth(240);
    top->addWidget(m_edtKeyword);

    QPushButton *btnSearch = new QPushButton(QStringLiteral("搜 索"), this);
    btnSearch->setObjectName(QStringLiteral("btnSearch"));
    btnSearch->setCursor(Qt::PointingHandCursor);
    top->addWidget(btnSearch);

    // 分类筛选放在搜索键右边：默认「全部」，切换后即时重新筛选
    top->addWidget(new QLabel(QStringLiteral("分类："), this));
    m_cmbCategory = new QComboBox(this);
    m_cmbCategory->addItem(QStringLiteral("全部")); // 索引 0 = 不限分类
    m_cmbCategory->addItems(DatabaseManager::categories());
    top->addWidget(m_cmbCategory);

    top->addStretch();

    QPushButton *btnEdit = new QPushButton(QStringLiteral("修 改"), this);
    btnEdit->setObjectName(QStringLiteral("btnEntry"));
    btnEdit->setCursor(Qt::PointingHandCursor);
    QPushButton *btnLogs = new QPushButton(QStringLiteral("更改记录"), this);
    btnLogs->setObjectName(QStringLiteral("btnSearch"));
    btnLogs->setCursor(Qt::PointingHandCursor);
    QPushButton *btnDel = new QPushButton(QStringLiteral("删除选中"), this);
    btnDel->setObjectName(QStringLiteral("btnDanger"));
    btnDel->setCursor(Qt::PointingHandCursor);
    top->addWidget(btnEdit);
    top->addWidget(btnLogs);
    top->addWidget(btnDel);

    QPushButton *btnBatch = new QPushButton(QStringLiteral("批量导入"), this);
    btnBatch->setObjectName(QStringLiteral("btnEntry"));
    btnBatch->setCursor(Qt::PointingHandCursor);
    top->addWidget(btnBatch);

    QPushButton *btnCode = new QPushButton(QStringLiteral("物料编码补充"), this);
    btnCode->setObjectName(QStringLiteral("btnEntry"));
    btnCode->setCursor(Qt::PointingHandCursor);
    btnCode->setToolTip(QStringLiteral("拖入「物料编码/物料描述」表格截图，OCR 后按型号回填物料编码"));
    top->addWidget(btnCode);

    QPushButton *btnQty = new QPushButton(QStringLiteral("数量管理"), this);
    btnQty->setObjectName(QStringLiteral("btnEntry"));
    btnQty->setCursor(Qt::PointingHandCursor);
    btnQty->setToolTip(QStringLiteral("查看/修改选中产品的每日录入数量与最小/最大数量"));
    top->addWidget(btnQty);
    root->addLayout(top);

    // 表格：无勾选列，直接点中行即可对数据操作
    m_table = new QTableWidget(0, 7, this);
    m_table->setObjectName(QStringLiteral("productTable"));
    m_table->setHorizontalHeaderLabels({QStringLiteral("型号"), QStringLiteral("名称"),
                                        QStringLiteral("物料编码"), QStringLiteral("图纸数"),
                                        QStringLiteral("录入时间"), QStringLiteral("备注"), QStringLiteral("录入人")});
    // 列宽：名称与备注吃掉剩余宽度，其余按内容/固定宽度，
    // 物料编码（16 位）与录入时间（yyyy-MM-dd HH:mm）要能完整显示，不能被省略成 "..."
    QHeaderView *hh = m_table->horizontalHeader();
    hh->setMinimumSectionSize(60);
    hh->setSectionResizeMode(0, QHeaderView::ResizeToContents); // 型号
    hh->setSectionResizeMode(1, QHeaderView::Stretch);          // 名称
    hh->setSectionResizeMode(2, QHeaderView::ResizeToContents); // 物料编码
    hh->setSectionResizeMode(3, QHeaderView::Fixed);            // 图纸数（0/1）
    hh->setSectionResizeMode(4, QHeaderView::Fixed);            // 录入时间
    hh->setSectionResizeMode(5, QHeaderView::Stretch);          // 备注
    hh->setSectionResizeMode(6, QHeaderView::Fixed);            // 录入人
    m_table->setColumnWidth(3, 70);
    m_table->setColumnWidth(4, 150);
    m_table->setColumnWidth(6, 96);
    m_table->verticalHeader()->setVisible(false);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::ExtendedSelection); // Ctrl/Shift 可多选批量删除
    root->addWidget(m_table, 1);

    // 物料编码补充：遮罩卡片（与「关于窗口」同款），常驻页面，点按钮才显示
    m_codePanel = new MaterialCodeDialog(this);
    m_codePanel->resize(size());
    connect(m_codePanel, &MaterialCodeDialog::closed, this, &ManagePage::refresh);

    // 数量管理：全屏半透明黑框弹窗，常驻页面，点按钮才显示
    m_qtyDialog = new QtyManageDialog(this);
    m_qtyDialog->resize(size());
    connect(m_qtyDialog, &QtyManageDialog::closed, this, &ManagePage::refresh);

    connect(btnSearch, &QPushButton::clicked, this, &ManagePage::onSearch);
    connect(m_edtKeyword, &QLineEdit::returnPressed, this, &ManagePage::onSearch);
    connect(m_cmbCategory, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ManagePage::onSearch);
    connect(btnEdit, &QPushButton::clicked, this, &ManagePage::onEdit);
    connect(btnLogs, &QPushButton::clicked, this, &ManagePage::onChangeLogs);
    connect(btnDel, &QPushButton::clicked, this, &ManagePage::onDelete);
    connect(btnBatch, &QPushButton::clicked, this, &ManagePage::onBatchImport);
    connect(btnCode, &QPushButton::clicked, this, &ManagePage::onSupplementMaterialCode);
    connect(btnQty, &QPushButton::clicked, this, &ManagePage::onQuantityManage);
    // 双击行直接进入编辑（型号在第 0 列）
    connect(m_table, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
        QTableWidgetItem *it = m_table->item(row, 0);
        if (it) emit requestEditProduct(it->data(Qt::UserRole).toInt());
    });

    refresh();
}

void ManagePage::refresh() { onSearch(); }

void ManagePage::onSearch()
{
    // 分类下拉第 0 项为「全部」→ 不限分类（传空串，searchProducts 即忽略分类）
    const QString category = (m_cmbCategory->currentIndex() == 0) ? QString()
                                                                  : m_cmbCategory->currentText();
    const auto list = DatabaseManager::instance().searchProducts(
        category, m_edtKeyword->text(), -1);
    m_table->setRowCount(list.size());
    for (int i = 0; i < list.size(); ++i) {
        const Product &p = list.at(i);
        auto mk = [&p](const QString &s) {
            QTableWidgetItem *it = new QTableWidgetItem(s);
            it->setData(Qt::UserRole, p.id);
            return it;
        };
        m_table->setItem(i, 0, mk(p.model));
        m_table->setItem(i, 1, mk(p.name));
        m_table->setItem(i, 2, mk(p.materialCode));
        m_table->setItem(i, 3, mk(QString::number(p.hasDrawing ? 1 : 0)));
        m_table->setItem(i, 4, mk(p.createdAt.toString(QStringLiteral("yyyy-MM-dd HH:mm"))));
        m_table->setItem(i, 5, mk(p.remark));
        m_table->setItem(i, 6, mk(p.creator));
        m_table->setRowHeight(i, 30);
    }
}

QList<int> ManagePage::selectedIds() const
{
    QList<int> ids;
    if (!m_table->selectionModel())
        return ids;
    const QModelIndexList rows = m_table->selectionModel()->selectedRows();
    for (const QModelIndex &idx : rows) {
        QTableWidgetItem *it = m_table->item(idx.row(), 0); // 第 0 列为型号
        if (it) ids << it->data(Qt::UserRole).toInt();
    }
    return ids;
}

void ManagePage::onEdit()
{
    const QList<int> ids = selectedIds();
    if (ids.size() != 1) {
        NdMessageBox::information(this, QStringLiteral("提示"), QStringLiteral("请先点选 1 条要修改的记录"));
        return;
    }
    emit requestEditProduct(ids.first());
}

// 数量管理：选中 1 条产品，弹出全屏半透明黑框，查看/修改每日数量与安全值
void ManagePage::onQuantityManage()
{
    const QList<int> ids = selectedIds();
    if (ids.size() != 1) {
        NdMessageBox::information(this, QStringLiteral("提示"), QStringLiteral("请先点选 1 条要管理数量的产品"));
        return;
    }
    m_qtyDialog->loadProduct(ids.first());
    m_qtyDialog->showPanel();
}

void ManagePage::onDelete()
{
    const QList<int> ids = selectedIds();
    if (ids.isEmpty()) {
        NdMessageBox::information(this, QStringLiteral("提示"), QStringLiteral("请先点选要删除的记录"));
        return;
    }
    if (!NdMessageBox::question(this, QStringLiteral("确认删除"),
                                QStringLiteral("将删除选中的 %1 条产品及其图纸、更改记录，且不可恢复！\n确定继续？").arg(ids.size())))
        return;
    QString err;
    if (!DatabaseManager::instance().deleteProducts(ids, &err))
        NdMessageBox::warning(this, QStringLiteral("删除失败"), err);
    refresh();
}

// 更改记录弹窗：查看 / 修改 / 删除
void ManagePage::onChangeLogs()
{
    const QList<int> ids = selectedIds();
    if (ids.size() != 1) {
        NdMessageBox::information(this, QStringLiteral("提示"), QStringLiteral("请点选 1 条记录查看更改记录"));
        return;
    }
    const int pid = ids.first();

    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("更改记录"));
    dlg.resize(640, 400);
    QVBoxLayout *lay = new QVBoxLayout(&dlg);

    QTableWidget *table = new QTableWidget(0, 4, &dlg);
    table->setHorizontalHeaderLabels({QStringLiteral("时间"), QStringLiteral("操作人"),
                                      QStringLiteral("内容"), QStringLiteral("操作")});
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Fixed);
    table->setColumnWidth(3, 140);
    table->verticalHeader()->setVisible(false);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    lay->addWidget(table, 1);

    std::function<void()> reload;
    reload = [&] {
        const auto logs = DatabaseManager::instance().changeLogsOf(pid);
        table->setRowCount(logs.size());
        for (int i = 0; i < logs.size(); ++i) {
            const ChangeLog log = logs.at(i); // 按值拷贝，避免 lambda 引用悬空
            table->setItem(i, 0, new QTableWidgetItem(log.createdAt.toString(QStringLiteral("yyyy-MM-dd HH:mm"))));
            table->setItem(i, 1, new QTableWidgetItem(log.oper));
            table->setItem(i, 2, new QTableWidgetItem(log.content));

            QWidget *ops = new QWidget(table);
            QHBoxLayout *ol = new QHBoxLayout(ops);
            ol->setContentsMargins(2, 0, 2, 0);
            ol->setSpacing(4);
            QPushButton *btnE = new QPushButton(QStringLiteral("修改"), ops);
            btnE->setObjectName(QStringLiteral("btnSearch"));
            btnE->setCursor(Qt::PointingHandCursor);
            QPushButton *btnD = new QPushButton(QStringLiteral("删除"), ops);
            btnD->setObjectName(QStringLiteral("btnDanger"));
            btnD->setCursor(Qt::PointingHandCursor);
            ol->addWidget(btnE);
            ol->addWidget(btnD);
            table->setCellWidget(i, 3, ops);
            table->setRowHeight(i, 30);

            connect(btnE, &QPushButton::clicked, &dlg, [&dlg, log, reload] {
                bool ok = false;
                const QString text = QInputDialog::getMultiLineText(
                    &dlg, QStringLiteral("修改更改记录"), QStringLiteral("内容："), log.content, &ok);
                if (!ok) return;
                QString err;
                if (!DatabaseManager::instance().updateChangeLog(log.id, text, &err))
                    NdMessageBox::warning(&dlg, QStringLiteral("失败"), err);
                reload();
            });
            connect(btnD, &QPushButton::clicked, &dlg, [&dlg, log, reload] {
                if (!NdMessageBox::question(&dlg, QStringLiteral("确认"),
                                            QStringLiteral("删除这条更改记录？")))
                    return;
                DatabaseManager::instance().deleteChangeLog(log.id);
                reload();
            });
        }
    };
    reload();

    QPushButton *btnClose = new QPushButton(QStringLiteral("关 闭"), &dlg);
    btnClose->setObjectName(QStringLiteral("btnEntry"));
    btnClose->setCursor(Qt::PointingHandCursor);
    connect(btnClose, &QPushButton::clicked, &dlg, &QDialog::accept);
    QHBoxLayout *bl = new QHBoxLayout();
    bl->addStretch();
    bl->addWidget(btnClose);
    lay->addLayout(bl);

    dlg.exec();
}

// 批量导入：选一个文件夹（如 F:\侯亚威电脑共享\5.控制器），
// 对每个型号去该文件夹下「名字含型号」的子目录中，找以型号命名的图纸文件并挂接
void ManagePage::onBatchImport()
{
    const QString folder = QFileDialog::getExistingDirectory(
        this, QStringLiteral("选择包含型号图纸的文件夹（如 F:\\侯亚威电脑共享\\5.控制器）"),
        QString());
    if (folder.isEmpty())
        return;

    // 取全部产品（按分类遍历，避免 searchProducts 空分类查不到）
    QList<Product> products;
    for (const QString &cat : DatabaseManager::categories())
        products << DatabaseManager::instance().searchProducts(cat, QString(), -1);
    QSet<int> seen;
    QList<Product> unique;
    for (const Product &p : products) {
        if (!seen.contains(p.id)) {
            seen.insert(p.id);
            unique << p;
        }
    }

    const QFileInfoList subDirs = QDir(folder).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
    const QStringList exts = {QStringLiteral("jpg"), QStringLiteral("jpeg"),
                              QStringLiteral("png"), QStringLiteral("bmp"), QStringLiteral("pdf"),
                              QStringLiteral("heic"), QStringLiteral("heif")};

    int added = 0, skipped = 0, notFound = 0, heifFailed = 0;
    QString err;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    for (const Product &p : unique) {
        const QString model = p.model.trimmed();
        if (model.isEmpty()) { ++skipped; continue; }
        // 找到名字含型号的子文件夹
        QString dirPath;
        for (const QFileInfo &d : subDirs)
            if (d.fileName().contains(model)) { dirPath = d.filePath(); break; }
        if (dirPath.isEmpty()) { ++notFound; continue; }
        // 在该子文件夹内找文件名含型号的图纸文件；同一目录下的多张（如下发图 + 会签图）都挂接
        bool found = false;
        for (const QFileInfo &f : QDir(dirPath).entryInfoList(QDir::Files)) {
            const QString suffix = f.suffix().toLower();
            if (!exts.contains(suffix)) continue;
            // 文件名含型号即可（允许带 TCU_、安全控制盒 等前缀），不要求以型号开头
            if (!f.completeBaseName().contains(model, Qt::CaseInsensitive)) continue;
            const int dtype = f.fileName().contains(QStringLiteral("会签"))
                                  ? TypeCountersign : TypeIssued;
            const QDate date = f.lastModified().date();
            found = true;

            QString src = f.filePath(), tmpJpg;
            if (HeifConverter::isHeifFile(src)) {
                QString convErr;
                tmpJpg = HeifConverter::toJpeg(src, &convErr);
                if (tmpJpg.isEmpty()) { ++skipped; ++heifFailed; continue; }
                src = tmpJpg;
            }
            if (DatabaseManager::instance().addDrawing(p.id, src, dtype, date, &err))
                ++added;
            else
                ++skipped;
            if (!tmpJpg.isEmpty())
                QFile::remove(tmpJpg); // addDrawing 已把 JPG 复制进图纸库，临时文件可删
        }
        if (!found) ++notFound;
    }
    QApplication::restoreOverrideCursor();

    const QString heifTip = heifFailed > 0
        ? QStringLiteral("\nHEIC 转码失败 %1 张：请确认程序目录下的 heif.dll / libde265-0.dll 完整，或先转成 JPG/PNG")
              .arg(heifFailed)
        : QString();
    NdMessageBox::information(
        this, QStringLiteral("批量导入完成"),
        QStringLiteral("扫描文件夹：%1\n共处理 %2 个产品：\n新增图纸 %3 条，已存在/失败 %4 条，未找到图纸 %5 条%6")
            .arg(folder).arg(unique.size()).arg(added).arg(skipped).arg(notFound).arg(heifTip));
}

// 物料编码补充：拖入表格截图 → OCR 提取「物料编码 + 型号」→ 按型号回填
void ManagePage::onSupplementMaterialCode()
{
    if (m_codePanel)
        m_codePanel->showPanel(); // 关闭时发 closed() 信号 → refresh()
}

// 遮罩层跟随页面大小，保证始终铺满
void ManagePage::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    if (m_codePanel)
        m_codePanel->resize(event->size());
    if (m_qtyDialog)
        m_qtyDialog->resize(event->size());
}

