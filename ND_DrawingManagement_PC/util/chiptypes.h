#ifndef CHIPTYPES_H
#define CHIPTYPES_H

#include <QString>
#include <QStringList>
#include <QJsonObject>

// 一条「硬件版本 → 重要芯片」记录。字段与用户表格对齐：
//   物资小类                  → hardware  (硬件版本)
//   零部件中使用进口芯片名称  → category  (类型)
//   芯片型号                  → model     (型号)
//   使用数量                  → qty       (数量)
//   芯片主要功能              → function  (功能)
//   芯片生产国家              → country   (国家)
//   芯片品牌                  → brand     (品牌)
//   remark 为预留备注，初始数据全部为空。
struct ChipItem
{
    int id = 0;
    QString hardware;
    QString category;
    QString model;
    int qty = 0;
    QString function;
    QString country;
    QString brand;
    QString remark;

    QStringList toRow() const
    {
        return QStringList{ hardware, category, model, QString::number(qty),
                            function, country, brand, remark };
    }

    QJsonObject toJson() const
    {
        QJsonObject o;
        o[QStringLiteral("id")] = id;
        o[QStringLiteral("hardware")] = hardware;
        o[QStringLiteral("category")] = category;
        o[QStringLiteral("model")] = model;
        o[QStringLiteral("qty")] = qty;
        o[QStringLiteral("function")] = function;
        o[QStringLiteral("country")] = country;
        o[QStringLiteral("brand")] = brand;
        o[QStringLiteral("remark")] = remark;
        return o;
    }

    static ChipItem fromJson(const QJsonObject &o)
    {
        ChipItem c;
        c.id = o.value(QStringLiteral("id")).toInt();
        c.hardware = o.value(QStringLiteral("hardware")).toString();
        c.category = o.value(QStringLiteral("category")).toString();
        c.model = o.value(QStringLiteral("model")).toString();
        c.qty = o.value(QStringLiteral("qty")).toInt();
        c.function = o.value(QStringLiteral("function")).toString();
        c.country = o.value(QStringLiteral("country")).toString();
        c.brand = o.value(QStringLiteral("brand")).toString();
        c.remark = o.value(QStringLiteral("remark")).toString();
        return c;
    }
};

#endif // CHIPTYPES_H
