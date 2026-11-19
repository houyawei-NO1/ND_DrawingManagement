#ifndef CUTTYPES_H
#define CUTTYPES_H

#include <QString>
#include <QJsonObject>
#include <QRegularExpression>
#include <QtGlobal>

// 下线表明细：字段对应实际生产 Excel「下线表」一行。
// 序号=seq（A列），回路编号=circuit（B列），其余依次。
struct CutItem
{
    int id = 0;
    int batchId = 0;
    int seq = 0;              // 序号（A列）
    QString circuit;          // 回路编号
    QString material;         // 导线材质
    QString section;          // 横截面积
    QString color;            // 颜色
    QString wireLength;       // 长度
    QString stripLeft;        // 左剥皮
    QString stripRight;       // 右剥皮
    QString terminalLeft;     // 左端子
    QString terminalRight;    // 右端子
    QString workLeft;         // 左压前作业
    QString workRight;        // 右压前作业

    QJsonObject toJson() const
    {
        QJsonObject o;
        o[QStringLiteral("id")] = id;
        o[QStringLiteral("batchId")] = batchId;
        o[QStringLiteral("seq")] = seq;
        o[QStringLiteral("circuit")] = circuit;
        o[QStringLiteral("material")] = material;
        o[QStringLiteral("section")] = section;
        o[QStringLiteral("color")] = color;
        o[QStringLiteral("wireLength")] = wireLength;
        o[QStringLiteral("stripLeft")] = stripLeft;
        o[QStringLiteral("stripRight")] = stripRight;
        o[QStringLiteral("terminalLeft")] = terminalLeft;
        o[QStringLiteral("terminalRight")] = terminalRight;
        o[QStringLiteral("workLeft")] = workLeft;
        o[QStringLiteral("workRight")] = workRight;
        return o;
    }
    static CutItem fromJson(const QJsonObject &o)
    {
        CutItem it;
        it.id = o.value(QStringLiteral("id")).toInt();
        it.batchId = o.value(QStringLiteral("batchId")).toInt();
        it.seq = o.value(QStringLiteral("seq")).toInt();
        it.circuit = o.value(QStringLiteral("circuit")).toString();
        it.material = o.value(QStringLiteral("material")).toString();
        it.section = o.value(QStringLiteral("section")).toString();
        it.color = o.value(QStringLiteral("color")).toString();
        it.wireLength = o.value(QStringLiteral("wireLength")).toString();
        it.stripLeft = o.value(QStringLiteral("stripLeft")).toString();
        it.stripRight = o.value(QStringLiteral("stripRight")).toString();
        it.terminalLeft = o.value(QStringLiteral("terminalLeft")).toString();
        it.terminalRight = o.value(QStringLiteral("terminalRight")).toString();
        it.workLeft = o.value(QStringLiteral("workLeft")).toString();
        it.workRight = o.value(QStringLiteral("workRight")).toString();
        return it;
    }
};

// 派工后的一条任务：一条明细 → 一台机器。状态由 AutoIt 客户端回传推进。
// status: 0待派工 1已派工待做 2进行中 3已完成 4已作废
struct CutTask
{
    int id = 0;
    int batchId = 0;
    int itemId = 0;
    int machineId = 0;
    int quantity = 1;         // 任务数量
    int doneQty = 0;          // 已完成数量
    int status = 0;
    QString operName;         // 完成的操作员
    QString createdAt;
    QString finishedAt;
    // 明细冗余字段（仅用于看板展示，来自 cut_item；随 JSON 回传给客户端显示）
    int seq = 0;
    QString circuit;
    QString color;
    QString wireLength;
    QString section;
    QString workLeft;         // 左压前作业（防水栓），来自明细
    QString workRight;        // 右压前作业（防水栓），来自明细
    QString batchCreatedAt;   // 所属批次的导入时间，用于显示「导入时间+序号」任务号
    // 设备端标签打印所需字段
    QString material;         // 材质
    QString stripLeft;        // 左剥皮
    QString stripRight;       // 右剥皮
    QString terminalLeft;     // 左端子
    QString terminalRight;    // 右端子
    QString batchName;        // 批次名/型号
    QString batchNo;          // 标签批次号
    QString taskNo;           // 任务号：批次导入时间 + 批次内序号

    static QString statusText(int s)
    {
        switch (s) {
        case 1: return QStringLiteral("待生产");
        case 2: return QStringLiteral("进行中");
        case 3: return QStringLiteral("已完成");
        case 4: return QStringLiteral("已作废");
        default: return QStringLiteral("待派工");
        }
    }
    QJsonObject toJson() const
    {
        QJsonObject o;
        o[QStringLiteral("id")] = id;
        o[QStringLiteral("batchId")] = batchId;
        o[QStringLiteral("itemId")] = itemId;
        o[QStringLiteral("machineId")] = machineId;
        o[QStringLiteral("quantity")] = quantity;
        o[QStringLiteral("doneQty")] = doneQty;
        o[QStringLiteral("status")] = status;
        o[QStringLiteral("operator")] = operName;
        o[QStringLiteral("createdAt")] = createdAt;
        o[QStringLiteral("finishedAt")] = finishedAt;
        o[QStringLiteral("seq")] = seq;
        o[QStringLiteral("circuit")] = circuit;
        o[QStringLiteral("color")] = color;
        o[QStringLiteral("wireLength")] = wireLength;
        o[QStringLiteral("section")] = section;
        o[QStringLiteral("workLeft")] = workLeft;
        o[QStringLiteral("workRight")] = workRight;
        o[QStringLiteral("batchCreatedAt")] = batchCreatedAt;
        o[QStringLiteral("material")] = material;
        o[QStringLiteral("stripLeft")] = stripLeft;
        o[QStringLiteral("stripRight")] = stripRight;
        o[QStringLiteral("terminalLeft")] = terminalLeft;
        o[QStringLiteral("terminalRight")] = terminalRight;
        o[QStringLiteral("batchName")] = batchName;
        o[QStringLiteral("batchNo")] = batchNo;
        o[QStringLiteral("taskNo")] = taskNo;
        return o;
    }
    static CutTask fromJson(const QJsonObject &o)
    {
        CutTask t;
        t.id = o.value(QStringLiteral("id")).toInt();
        t.batchId = o.value(QStringLiteral("batchId")).toInt();
        t.itemId = o.value(QStringLiteral("itemId")).toInt();
        t.machineId = o.value(QStringLiteral("machineId")).toInt();
        t.quantity = o.value(QStringLiteral("quantity")).toInt();
        t.doneQty = o.value(QStringLiteral("doneQty")).toInt();
        t.status = o.value(QStringLiteral("status")).toInt();
        t.operName = o.value(QStringLiteral("operator")).toString();
        t.createdAt = o.value(QStringLiteral("createdAt")).toString();
        t.finishedAt = o.value(QStringLiteral("finishedAt")).toString();
        t.seq = o.value(QStringLiteral("seq")).toInt();
        t.circuit = o.value(QStringLiteral("circuit")).toString();
        t.color = o.value(QStringLiteral("color")).toString();
        t.wireLength = o.value(QStringLiteral("wireLength")).toString();
        t.section = o.value(QStringLiteral("section")).toString();
        t.workLeft = o.value(QStringLiteral("workLeft")).toString();
        t.workRight = o.value(QStringLiteral("workRight")).toString();
        t.batchCreatedAt = o.value(QStringLiteral("batchCreatedAt")).toString();
        t.material = o.value(QStringLiteral("material")).toString();
        t.stripLeft = o.value(QStringLiteral("stripLeft")).toString();
        t.stripRight = o.value(QStringLiteral("stripRight")).toString();
        t.terminalLeft = o.value(QStringLiteral("terminalLeft")).toString();
        t.terminalRight = o.value(QStringLiteral("terminalRight")).toString();
        t.batchName = o.value(QStringLiteral("batchName")).toString();
        t.batchNo = o.value(QStringLiteral("batchNo")).toString();
        t.taskNo = o.value(QStringLiteral("taskNo")).toString();
        return t;
    }
};

// 智能分配预览中的一条「明细 → 机台」建议；确认派工前可在界面上改机台
struct CutAssign
{
    int itemId = 0;
    int machineId = 0;
    QJsonObject toJson() const
    {
        QJsonObject o;
        o[QStringLiteral("itemId")] = itemId;
        o[QStringLiteral("machineId")] = machineId;
        return o;
    }
    static CutAssign fromJson(const QJsonObject &o)
    {
        CutAssign a;
        a.itemId = o.value(QStringLiteral("itemId")).toInt();
        a.machineId = o.value(QStringLiteral("machineId")).toInt();
        return a;
    }
};

// 下线机：5 台机器，capabilities 存能力配置（密封堵/线色/线径/端子），JSON 文本。
struct CutMachine
{
    int id = 0;
    QString code;            // 机台编号，如 M1 / 1号机
    QString name;
    QString capabilities;    // JSON 文本，能力配置
    int active = 1;

    QJsonObject toJson() const
    {
        QJsonObject o;
        o[QStringLiteral("id")] = id;
        o[QStringLiteral("code")] = code;
        o[QStringLiteral("name")] = name;
        o[QStringLiteral("capabilities")] = capabilities;
        o[QStringLiteral("active")] = active;
        return o;
    }
    static CutMachine fromJson(const QJsonObject &o)
    {
        CutMachine m;
        m.id = o.value(QStringLiteral("id")).toInt();
        m.code = o.value(QStringLiteral("code")).toString();
        m.name = o.value(QStringLiteral("name")).toString();
        m.capabilities = o.value(QStringLiteral("capabilities")).toString();
        m.active = o.value(QStringLiteral("active")).toInt(1);
        return m;
    }
};

// 派单规则：约束「某段线径 / 某类端子 / 某类防水栓 只能发到某台机器」。
// field：section 线径(截面) / terminal 端子(左右) / seal 防水栓(压前作业) /
//        color 颜色 / material 材质
// 线径用范围表达（matchValue 形如 "0.5-1.5"，matchType 固定 range）；
// 其余字段用 exact 精确 / contains 包含 / prefix 前缀（均忽略大小写）。
struct CutRule
{
    int id = 0;
    QString field;
    QString matchType;
    QString matchValue;       // 线径为 "0.5-1.5"；其它为 PP2011101 这类匹配值
    int machineId = 0;        // 目标机台 cut_machine.id
    int enabled = 1;

    // 线径规则一律按数值范围比对
    bool isRange() const { return field == QLatin1String("section"); }

    static QString fieldText(const QString &f)
    {
        if (f == QLatin1String("section")) return QStringLiteral("线径(截面)");
        if (f == QLatin1String("terminal")) return QStringLiteral("端子");
        if (f == QLatin1String("seal")) return QStringLiteral("防水栓(压前作业)");
        if (f == QLatin1String("color")) return QStringLiteral("颜色");
        if (f == QLatin1String("material")) return QStringLiteral("材质");
        return f;
    }
    static QString matchText(const QString &t)
    {
        if (t == QLatin1String("range")) return QStringLiteral("范围");
        if (t == QLatin1String("exact")) return QStringLiteral("精确等于");
        if (t == QLatin1String("prefix")) return QStringLiteral("开头是");
        return QStringLiteral("包含");
    }

    // 表格/提示显示用文案
    QString valueText() const
    {
        if (!isRange()) return matchValue;
        QString t = matchValue;
        return t.replace(QLatin1Char('-'), QStringLiteral(" ~ ")).trimmed();
    }

    QJsonObject toJson() const
    {
        QJsonObject o;
        o[QStringLiteral("id")] = id;
        o[QStringLiteral("field")] = field;
        o[QStringLiteral("matchType")] = matchType;
        o[QStringLiteral("matchValue")] = matchValue;
        o[QStringLiteral("machineId")] = machineId;
        o[QStringLiteral("enabled")] = enabled;
        return o;
    }
    static CutRule fromJson(const QJsonObject &o)
    {
        CutRule r;
        r.id = o.value(QStringLiteral("id")).toInt();
        r.field = o.value(QStringLiteral("field")).toString();
        r.matchType = o.value(QStringLiteral("matchType")).toString();
        r.matchValue = o.value(QStringLiteral("matchValue")).toString();
        r.machineId = o.value(QStringLiteral("machineId")).toInt();
        r.enabled = o.value(QStringLiteral("enabled")).toInt(1);
        return r;
    }
};

// 从文本里取最后一个数字，兼容 "0.75" / "2X0.75" / "0.75mm²"；取不到返回 -1
inline double cutSectionNumber(const QString &text)
{
    static const QRegularExpression re(QStringLiteral("([0-9]+(?:[\\.,][0-9]+)?)"));
    double v = -1.0;
    QRegularExpressionMatchIterator it = re.globalMatch(text);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        bool ok = false;
        const double d = QString(m.captured(1)).replace(QLatin1Char(','), QLatin1Char('.')).toDouble(&ok);
        if (ok) v = d;
    }
    return v;
}

// 解析线径范围文本："0.5-1.5" / "0.5~1.5" / "0.5 — 1.5" 均可
inline bool cutRuleParseRange(const QString &text, double *lo, double *hi)
{
    QString t = text;
    t.replace(QChar(0xFF5E), QLatin1Char('~'));   // ～
    t.replace(QChar(0x2014), QLatin1Char('-'));   // —
    t.replace(QChar(0x2013), QLatin1Char('-'));   // –
    t.replace(QChar(0xFF0D), QLatin1Char('-'));   // －
    static const QRegularExpression re(QStringLiteral(
        "^\\s*([0-9]+(?:[\\.,][0-9]+)?)\\s*[-~]\\s*([0-9]+(?:[\\.,][0-9]+)?)\\s*$"));
    const QRegularExpressionMatch m = re.match(t);
    if (!m.hasMatch()) return false;
    bool ok1 = false, ok2 = false;
    double a = QString(m.captured(1)).replace(QLatin1Char(','), QLatin1Char('.')).toDouble(&ok1);
    double b = QString(m.captured(2)).replace(QLatin1Char(','), QLatin1Char('.')).toDouble(&ok2);
    if (!ok1 || !ok2) return false;
    if (a > b) qSwap(a, b);
    if (lo) *lo = a;
    if (hi) *hi = b;
    return true;
}

// 规则待匹配文本：按 field 取出明细中参与比对的内容。
// 防水栓信息来自「左压前作业 / 右压前作业」两列，不掺入端子。
inline QString cutRuleHaystack(const CutItem &it, const QString &field)
{
    if (field == QLatin1String("section"))  return it.section;
    if (field == QLatin1String("terminal")) return it.terminalLeft + QLatin1Char(' ') + it.terminalRight;
    if (field == QLatin1String("seal"))     return it.workLeft + QLatin1Char(' ') + it.workRight;
    if (field == QLatin1String("color"))    return it.color;
    if (field == QLatin1String("material")) return it.material;
    return it.section + QLatin1Char(' ')
           + it.terminalLeft + QLatin1Char(' ') + it.terminalRight;
}

// 屏蔽类线束：端子 / 防水栓（压前作业） / 材质任一列含「屏蔽」，
// 统一归「其他」机台由人工/手动下线，五台自动机都不接。
inline bool cutItemHasShield(const CutItem &it)
{
    const QString kw = QStringLiteral("屏蔽");
    return cutRuleHaystack(it, QLatin1String("terminal")).contains(kw)
        || cutRuleHaystack(it, QLatin1String("seal")).contains(kw)
        || cutRuleHaystack(it, QLatin1String("material")).contains(kw);
}

// 材料归组键：线径（截面）和线色都相同才算同一种材料，可放在同一台机台上生产
inline QString cutMaterialKey(const CutItem &it)
{
    return it.section.trimmed().toUpper() + QLatin1Char('|') + it.color.trimmed().toUpper();
}

// 规则具体度：数值越小越具体。命中多条时先看专用件（端子/防水栓），再看线径范围。
inline int cutRuleSpecificity(const CutRule &r)
{
    if (r.isRange()) return 3;
    if (r.matchType == QLatin1String("exact")) return 0;
    if (r.matchType == QLatin1String("prefix")) return 1;
    return 2;
}

// 一条派单规则是否命中某条明细
inline bool cutRuleMatches(const CutRule &r, const CutItem &it)
{
    if (r.isRange()) {
        const double v = cutSectionNumber(it.section);
        if (v < 0.0) return false;
        double lo = 0.0, hi = 0.0;
        if (cutRuleParseRange(r.matchValue, &lo, &hi))
            return v >= lo - 1e-9 && v <= hi + 1e-9;
        // 兼容早期单值写法：0.75 视为只有该值
        const double single = cutSectionNumber(r.matchValue);
        return single >= 0.0 && qAbs(v - single) < 1e-6;
    }
    const QString needle = r.matchValue.trimmed();
    if (needle.isEmpty()) return false;
    const QString hay = cutRuleHaystack(it, r.field);
    if (r.matchType == QLatin1String("exact"))
        return hay.trimmed().compare(needle, Qt::CaseInsensitive) == 0;
    if (r.matchType == QLatin1String("prefix"))
        return hay.trimmed().startsWith(needle, Qt::CaseInsensitive);
    return hay.contains(needle, Qt::CaseInsensitive);
}

// 导入批次
struct CutBatch
{
    int id = 0;
    QString name;            // 批次名（型号）
    QString batchNo;         // 标签上的批次号
    QString fileName;        // 来源文件
    QString quantity;        // 数量（Excel 顶部）
    int totalRows = 0;
    QString creator;
    QString createdAt;

    QJsonObject toJson() const
    {
        QJsonObject o;
        o[QStringLiteral("id")] = id;
        o[QStringLiteral("name")] = name;
        o[QStringLiteral("batchNo")] = batchNo;
        o[QStringLiteral("fileName")] = fileName;
        o[QStringLiteral("quantity")] = quantity;
        o[QStringLiteral("totalRows")] = totalRows;
        o[QStringLiteral("creator")] = creator;
        o[QStringLiteral("createdAt")] = createdAt;
        return o;
    }
    static CutBatch fromJson(const QJsonObject &o)
    {
        CutBatch b;
        b.id = o.value(QStringLiteral("id")).toInt();
        b.name = o.value(QStringLiteral("name")).toString();
        b.batchNo = o.value(QStringLiteral("batchNo")).toString();
        b.fileName = o.value(QStringLiteral("fileName")).toString();
        b.quantity = o.value(QStringLiteral("quantity")).toString();
        b.totalRows = o.value(QStringLiteral("totalRows")).toInt();
        b.creator = o.value(QStringLiteral("creator")).toString();
        b.createdAt = o.value(QStringLiteral("createdAt")).toString();
        return b;
    }
};

#endif // CUTTYPES_H
