#ifndef MODELCODEC_H
#define MODELCODEC_H

#include <QString>
#include <QVector>

// 中小拖型号编码转换规则（对照表见「工具 → 编码转换」）。
// 只处理第一个 '-' 之前的「机型号 + 功率代码」：
//   机型号 → 两位替代号；功率代码里每位数字 → 0~9 对应 A~J；
//   其余字符（字母 Z、机型号后的 A/C/L/S/G 等）原样保留，'-' 之后整体不变。
namespace ModelCode {

struct MachineCode {
    QString code;    // 机型号
    QString number;  // 替代号
};

// 按对照表顺序：前 12 项替代号 61~72，后 12 项 73~84
inline const QVector<MachineCode> &machineCodes()
{
    static const QVector<MachineCode> codes = {
        {QStringLiteral("MC"),  QStringLiteral("61")},
        {QStringLiteral("ME"),  QStringLiteral("62")},
        {QStringLiteral("MF"),  QStringLiteral("63")},
        {QStringLiteral("MK"),  QStringLiteral("64")},
        {QStringLiteral("MY"),  QStringLiteral("65")},
        {QStringLiteral("MA"),  QStringLiteral("66")},
        {QStringLiteral("MH"),  QStringLiteral("67")},
        {QStringLiteral("MW"),  QStringLiteral("68")},
        {QStringLiteral("SG"),  QStringLiteral("69")},
        {QStringLiteral("SE"),  QStringLiteral("70")},
        {QStringLiteral("SN"),  QStringLiteral("71")},
        {QStringLiteral("SK"),  QStringLiteral("72")},
        {QStringLiteral("SY"),  QStringLiteral("73")},
        {QStringLiteral("SH"),  QStringLiteral("74")},
        {QStringLiteral("CZ"),  QStringLiteral("75")},
        {QStringLiteral("ZT"),  QStringLiteral("76")},
        {QStringLiteral("E"),   QStringLiteral("77")},
        {QStringLiteral("M"),   QStringLiteral("78")},
        {QStringLiteral("ES"),  QStringLiteral("79")},
        {QStringLiteral("MS"),  QStringLiteral("80")},
        {QStringLiteral("MG"),  QStringLiteral("81")},
        {QStringLiteral("ST"),  QStringLiteral("82")},
        {QStringLiteral("ESK"), QStringLiteral("83")},
        {QStringLiteral("MZ"),  QStringLiteral("84")},
    };
    return codes;
}

// 功率代码数字 0~9 对应 A~J
inline QString powerLetter(int digit)
{
    static const QString letters = QStringLiteral("ABCDEFGHIJ");
    return (digit >= 0 && digit < 10) ? letters.mid(digit, 1) : QString();
}

// 失败时返回空字符串，并通过 err 给出原因（本规则仅适用于中小拖型号）
inline QString convert(const QString &model, QString *err = nullptr)
{
    if (err) err->clear();
    const QString src = model.trimmed();
    if (src.isEmpty()) {
        if (err) *err = QStringLiteral("请先输入原型号");
        return QString();
    }

    const int dash = src.indexOf(QLatin1Char('-'));
    const QString head = dash < 0 ? src : src.left(dash);
    const QString tail = dash < 0 ? QString() : src.mid(dash);

    // 机型号按最长匹配，避免 "MF1004" 被单字母 "M" 截走
    const QString upper = head.toUpper();
    int matched = 0;
    QString number;
    for (const MachineCode &m : machineCodes()) {
        if (m.code.size() > matched && upper.startsWith(m.code)) {
            matched = m.code.size();
            number = m.number;
        }
    }
    if (matched == 0) {
        if (err) *err = QStringLiteral("未匹配到机型号：本规则仅适用于中小拖型号");
        return QString();
    }

    QString out = number;
    int digits = 0;
    for (int i = matched; i < head.size(); ++i) {
        const ushort u = head.at(i).unicode();
        if (u >= ushort('0') && u <= ushort('9')) {
            out += powerLetter(u - ushort('0'));
            ++digits;
        } else {
            // 注：字母 Z，以及机型号后的 A/C/L/S/G 等字母不参与转换
            out += head.at(i);
        }
    }
    if (digits == 0) {
        if (err) *err = QStringLiteral("未找到功率代码数字：本规则仅适用于中小拖型号");
        return QString();
    }
    return out + tail;
}

} // namespace ModelCode
#endif // MODELCODEC_H
