#ifndef MOLDTYPES_H
#define MOLDTYPES_H

#include <QString>
#include <QStringList>
#include <QJsonObject>
#include <QRegularExpression>

// 一条端子机模具台账记录，字段与实际 Excel「2026.4.22端子机模具.刀头」对齐。
// 一个模具可能对应多个端子：terminals 存适用端子（多个用「、」或换行连接）。
// 老模具/刀片没有模具号：moldCode 允许为空，用 status 标记「待完善」。
struct MoldItem
{
    int id = 0;
    QString moldCode;       // 模具名称/模具号（可空）
    QString type;           // 类型：模具 / 刀片 / 刀头 / 其他
    QString machineType;    // 机器类型：日式 / 欧式
    QString terminals;      // 适用端子型号/线径（多个用分隔符）
    QString pressWire;      // 模具刀头可以压接线径
    QString terminalInfo;   // 端子信息
    QString sealInfo;       // 密封堵/在几号机使用
    QString machineNo;      // 机器编号（预留，目前无数据）
    QString sealImage;      // 密封堵图片路径（预留，后续补传）
    QString terminalImage;  // 端子图片路径（预留，后续补传）
    QString harness;        // 使用线束名称
    QString orientation;    // A竖排 / B横排
    QString department;     // 责任部门
    QString area;           // 放置区域
    QString keeper;         // 保管人
    QString vendor;         // 模具厂家/购买日期
    QString remark;         // 备注
    int status = 0;         // 0待完善 1完整 2已核对
    QString creator;
    QString createdAt;
    QString updatedAt;

    QStringList terminalList() const
    {
        QStringList out;
        for (const QString &s : terminals.split(QRegularExpression(QStringLiteral("[、,;；\\r\\n]+")),
                                                Qt::SkipEmptyParts)) {
            const QString t = s.trimmed();
            if (!t.isEmpty())
                out << t;
        }
        return out;
    }

    QString statusText() const
    {
        switch (status) {
        case 1: return QStringLiteral("完整");
        case 2: return QStringLiteral("已核对");
        default: return QStringLiteral("待完善");
        }
    }

    QStringList toRow() const
    {
        return QStringList{ QString::number(id), moldCode, type, terminals,
                            pressWire, machineNo, sealInfo, harness, statusText() };
    }

    QJsonObject toJson() const
    {
        QJsonObject o;
        o[QStringLiteral("id")] = id;
        o[QStringLiteral("moldCode")] = moldCode;
        o[QStringLiteral("type")] = type;
        o[QStringLiteral("machineType")] = machineType;
        o[QStringLiteral("pressWire")] = pressWire;
        o[QStringLiteral("terminals")] = terminals;
        o[QStringLiteral("terminalInfo")] = terminalInfo;
        o[QStringLiteral("sealInfo")] = sealInfo;
        o[QStringLiteral("machineNo")] = machineNo;
        o[QStringLiteral("sealImage")] = sealImage;
        o[QStringLiteral("terminalImage")] = terminalImage;
        o[QStringLiteral("harness")] = harness;
        o[QStringLiteral("orientation")] = orientation;
        o[QStringLiteral("department")] = department;
        o[QStringLiteral("area")] = area;
        o[QStringLiteral("keeper")] = keeper;
        o[QStringLiteral("vendor")] = vendor;
        o[QStringLiteral("remark")] = remark;
        o[QStringLiteral("status")] = status;
        return o;
    }

    static MoldItem fromJson(const QJsonObject &o)
    {
        MoldItem m;
        m.id = o.value(QStringLiteral("id")).toInt();
        m.moldCode = o.value(QStringLiteral("moldCode")).toString();
        m.type = o.value(QStringLiteral("type")).toString();
        m.machineType = o.value(QStringLiteral("machineType")).toString();
        m.pressWire = o.value(QStringLiteral("pressWire")).toString();
        m.terminals = o.value(QStringLiteral("terminals")).toString();
        m.terminalInfo = o.value(QStringLiteral("terminalInfo")).toString();
        m.sealInfo = o.value(QStringLiteral("sealInfo")).toString();
        m.machineNo = o.value(QStringLiteral("machineNo")).toString();
        m.sealImage = o.value(QStringLiteral("sealImage")).toString();
        m.terminalImage = o.value(QStringLiteral("terminalImage")).toString();
        m.harness = o.value(QStringLiteral("harness")).toString();
        m.orientation = o.value(QStringLiteral("orientation")).toString();
        m.department = o.value(QStringLiteral("department")).toString();
        m.area = o.value(QStringLiteral("area")).toString();
        m.keeper = o.value(QStringLiteral("keeper")).toString();
        m.vendor = o.value(QStringLiteral("vendor")).toString();
        m.remark = o.value(QStringLiteral("remark")).toString();
        m.status = o.value(QStringLiteral("status")).toInt();
        return m;
    }
};

#endif // MOLDTYPES_H
