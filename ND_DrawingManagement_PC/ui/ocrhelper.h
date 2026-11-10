#ifndef OCRHELPER_H
#define OCRHELPER_H

// 主界面「拖入图片 → OCR → 多型号搜索」用到的公共小工具。
// 全部是 header-only 的纯函数，避免为几个小函数再引入一个 .cpp。
#include <QCoreApplication>
#include <QFileInfo>
#include <QIcon>
#include <QPainter>
#include <QPixmap>
#include <QPen>
#include <QColor>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QJsonObject>
#include <QJsonArray>
#include <QRegularExpression>
#include <QSet>
#include <QList>
#include <QString>
#include <QStringList>
#include <QtGlobal>
#include <algorithm>
#include <cstring>

namespace OcrHelper {

// 与工具栏搜索框「清除搜索内容」完全同款的叉号：平时灰色空心圆 + 灰叉，
// 悬停/按下变成红色实心圆。拖拽框和原文查找框共用它，保证视觉一致。
inline QIcon makeClearIcon()
{
    static const QColor gray(QStringLiteral("#8A90A6"));
    static const QColor red(QStringLiteral("#FF5252"));
    static const QColor darkRed(QStringLiteral("#8E1B1B"));
    const auto draw = [](bool hover) {
        const int s = 20;
        QPixmap pm(s, s);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        const QRectF circle(2.5, 2.5, 15, 15);
        if (hover) {
            p.setPen(Qt::NoPen);
            p.setBrush(red);
            p.drawEllipse(circle);
        } else {
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(gray, 1.6, Qt::SolidLine, Qt::RoundCap));
            p.drawEllipse(circle);
        }
        p.setPen(QPen(hover ? darkRed : gray, 1.8, Qt::SolidLine, Qt::RoundCap));
        const QRectF r(7, 7, 6, 6);
        p.drawLine(r.topLeft(), r.bottomRight());
        p.drawLine(r.topRight(), r.bottomLeft());
        p.end();
        return pm;
    };
    QIcon icon;
    const QIcon::State states[] = {QIcon::On, QIcon::Off};
    for (QIcon::State st : states) {
        icon.addPixmap(draw(false), QIcon::Normal, st);
        icon.addPixmap(draw(true), QIcon::Active, st);
        icon.addPixmap(draw(true), QIcon::Selected, st);
    }
    return icon;
}

inline QString enginePath()
{
    const QString exe = QCoreApplication::applicationDirPath()
        + QStringLiteral("/ocr/PaddleOCR-json/PaddleOCR-json.exe");
    return QFileInfo::exists(exe) ? exe : QString();
}

inline bool isImageFile(const QString &path)
{
    const QString s = QFileInfo(path).suffix().toLower();
    return s == QStringLiteral("jpg") || s == QStringLiteral("jpeg")
        || s == QStringLiteral("png") || s == QStringLiteral("bmp");
}

// 解析 PaddleOCR-json 输出：{code:100,data:[{box,text}]} → 按 y/x 排序后的多行文本。
// 与数量录入页的解析口径保持一致。
inline bool parseOcrJsonText(const QByteArray &out, QString *text)
{
    const int b = out.indexOf('{');
    const int e = out.lastIndexOf('}');
    if (b < 0 || e <= b)
        return false;
    QJsonParseError perr;
    const QJsonDocument doc = QJsonDocument::fromJson(out.mid(b, e - b + 1), &perr);
    if (perr.error != QJsonParseError::NoError)
        return false;
    const QJsonObject obj = doc.object();
    if (obj.value(QStringLiteral("code")).toInt() != 100)
        return false;
    struct Line { int y; int x; QString text; };
    QList<Line> lines;
    const QJsonArray data = obj.value(QStringLiteral("data")).toArray();
    for (const QJsonValue &v : data) {
        const QJsonObject o = v.toObject();
        const QJsonArray box = o.value(QStringLiteral("box")).toArray();
        const int y = box.isEmpty() ? 0 : int(box.first().toArray().at(1).toDouble());
        const int x = box.isEmpty() ? 0 : int(box.first().toArray().at(0).toDouble());
        lines.append({y, x, o.value(QStringLiteral("text")).toString()});
    }
    std::sort(lines.begin(), lines.end(), [](const Line &a, const Line &b2) {
        return a.y == b2.y ? a.x < b2.x : a.y < b2.y;
    });
    QStringList parts;
    for (const Line &l : lines)
        parts.append(l.text);
    *text = parts.join(QLatin1Char('\n'));
    return !text->trimmed().isEmpty();
}

// 型号比较前归一化：去掉括号/分隔符/空格，统一大写，只留字母数字。
inline QString normalizeModelKey(const QString &s)
{
    QString out;
    out.reserve(s.size());
    for (const QChar ch : s)
        if (ch.isLetterOrNumber())
            out.append(ch.toUpper());
    return out;
}

// ---- OCR 型号纠错：拿数据库里的真实型号当词典，修正字母形近误识别 ----------
// 采购单 OCR 里 LXM1604 常被读成 L3M1604 / LM1604，DT2304-7D 被读成 7I。
// 这些误识别在归一化串上只差 1 个字符，用「形近字符零代价替换」的距离即可命中。
inline bool confusable(QChar ca, QChar cb)
{
    if (ca == cb)
        return true;
    const char a = ca.toLatin1(), b = cb.toLatin1();
    if (a == '\0' || b == '\0') // 归一化串里残留的非 ASCII 字符不参与形近判断
        return false;
    static const char *const groups[] = {
        "O0Q", "I1L", "S5", "B8", "Z2", "G6", "DI", "UV", "MN", "XK"
    };
    for (const char *g : groups)
        if (std::strchr(g, a) && std::strchr(g, b))
            return true;
    return false;
}

inline QString digitsOnly(const QString &s)
{
    QString out;
    out.reserve(s.size());
    for (const QChar ch : s)
        if (ch.isDigit())
            out.append(ch);
    return out;
}

// 滚动数组编辑距离；fuzzy=true 时形近字符替换不计代价，长度差异过大直接判不相关。
inline int editDistance(const QString &a, const QString &b, bool fuzzy)
{
    constexpr int kInf = 1000000;
    const int n = a.size(), m = b.size();
    if (n > 64 || m > 64 || qAbs(n - m) > 3)
        return kInf;
    QList<int> prev(m + 1, 0), cur(m + 1, 0);
    for (int j = 0; j <= m; ++j)
        prev[j] = j;
    for (int i = 1; i <= n; ++i) {
        cur[0] = i;
        for (int j = 1; j <= m; ++j) {
            const bool same = fuzzy ? confusable(a.at(i - 1), b.at(j - 1))
                                    : (a.at(i - 1) == b.at(j - 1));
            cur[j] = qMin(qMin(cur[j - 1] + 1, prev[j] + 1),
                          prev[j - 1] + (same ? 0 : 1));
        }
        prev.swap(cur);
    }
    return prev.at(m);
}

// 两个归一化型号的差异分数：0~2 可接受，-1 表示不是同一型号。
// 数字骨架单独校验：字母读错可以忍，连续数字读错就不该强行改成别的型号。
inline int modelDistance(const QString &a, const QString &b)
{
    if (a.size() < 6 || b.size() < 6 || qAbs(a.size() - b.size()) > 2)
        return -1;
    const int d = editDistance(a, b, true);
    if (d > 2 || editDistance(digitsOnly(a), digitsOnly(b), false) > 1)
        return -1;
    return d;
}

// 用库中型号逐个纠正 OCR 型号。只在「唯一最优且差异 ≤2」时替换，
// 避免把订单里相近的 …016 / …018 互相改错。返回纠正后的型号（按归一化 key 去重）。
inline QStringList correctModels(const QStringList &rawModels, const QStringList &dict)
{
    struct Entry { QString key; QString display; };
    QList<Entry> entries;
    QSet<QString> seen;
    for (const QString &d : dict) {
        const QString k = normalizeModelKey(d);
        if (k.size() < 6 || seen.contains(k))
            continue;
        seen.insert(k);
        entries.append({k, d.trimmed()});
    }
    if (entries.isEmpty())
        return rawModels;

    constexpr int kInf = 1000000;
    QStringList out;
    QSet<QString> outKeys;
    for (const QString &raw : rawModels) {
        const QString a = normalizeModelKey(raw);
        int best = kInf, second = kInf;
        QString bestDisplay;
        for (const Entry &e : entries) {
            const int d = modelDistance(a, e.key);
            if (d < 0)
                continue;
            if (d < best) {
                second = best;
                best = d;
                bestDisplay = e.display;
            } else if (d < second) {
                second = d;
            }
        }
        const QString chosen = (best <= 2 && best < second) ? bestDisplay : raw;
        const QString key = normalizeModelKey(chosen);
        if (outKeys.contains(key))
            continue;
        outKeys.insert(key);
        out << chosen;
    }
    return out;
}

// OCR 搜索匹配口径：勾选「型号一致」时只认完全相同；默认按包含匹配，
// 这样 OCR 截断或库中带 -1 之类后缀的型号也都能搜到。
inline bool modelHit(const QString &dbKey, const QSet<QString> &ocrKeys, bool exactOnly)
{
    if (ocrKeys.contains(dbKey))
        return true;
    if (exactOnly || dbKey.size() < 6)
        return false;
    for (const QString &k : ocrKeys) {
        if (k.size() >= 6 && (dbKey.contains(k) || k.contains(dbKey)))
            return true;
    }
    return false;
}

// 旧版式兜底：型号在「物料描述」行尾，可能带 (U)/小写/括号等噪声。
// 采购单两类图片不走这里，避免把仓管编码、版本号之类的行尾数字误当型号。
inline QStringList extractLegacyModels(const QString &text)
{
    static const QRegularExpression tailAscii(
        QStringLiteral("([A-Za-z0-9][A-Za-z0-9.\\-_/()]*)\\s*$"));
    static const QRegularExpression modelAfterParen(
        QStringLiteral("\\)([A-Za-z0-9][A-Za-z0-9.\\-_/()]*)$"));
    static const QRegularExpression parenTail(QStringLiteral("\\([^()]*\\)\\s*$"));
    static const QRegularExpression dangling(QStringLiteral("[.\\-_/]+$"));
    static const QRegularExpression loneOpenParen(QStringLiteral("\\($"));
    static const QRegularExpression codeOnly(QStringLiteral("^\\d{10,14}$"));
    static const QRegularExpression seqOnly(QStringLiteral("^\\d{1,4}$"));
    static const QRegularExpression decimal(QStringLiteral("^\\d+\\.\\d+$"));

    QStringList out;
    QSet<QString> seen;
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (const QString &raw : lines) {
        const QString line = raw.trimmed();
        if (line.isEmpty())
            continue;
        const auto m = tailAscii.match(line);
        if (!m.hasMatch())
            continue;
        QString model = m.captured(1);
        const auto pm = modelAfterParen.match(line);
        if (pm.hasMatch() && pm.captured(1).size() >= 5)
            model = pm.captured(1);
        model.remove(loneOpenParen);
        model.remove(dangling);
        if (model.size() < 4)
            continue;
        QString bare = model;
        bare.remove(parenTail);
        bare = bare.trimmed();
        const QString check = bare.isEmpty() ? model : bare;
        if (check.size() < 4 || codeOnly.match(check).hasMatch()
            || seqOnly.match(check).hasMatch() || decimal.match(check).hasMatch())
            continue;
        const QString key = normalizeModelKey(model);
        if (key.size() < 4 || seen.contains(key))
            continue;
        seen.insert(key);
        out << model;
    }
    return out;
}

// 从订单截图 OCR 原文里提取所有型号。
// 两类采购单的型号都嵌在「品名」列中间，例如
//   「半成品LX2404-7VC-48-012 角柱屏过渡线 S 一拖」「半成品LXM604-C3-48-016 右侧操纵台线束 一拖」
// 型号只由大写字母、数字、横杠组成（至少一段横杠分隔），后面紧跟中文描述。
// 因此全文扫描 token，而不是取行尾：中文、品号、数量、日期、电话、仓管编码都不会命中。
inline QStringList extractModels(const QString &text)
{
    static const QRegularExpression modelToken(
        QStringLiteral("([A-Za-z0-9]+(?:-[A-Za-z0-9]+)+)"));
    static const QRegularExpression hasLetter(QStringLiteral("[A-Z]"));

    QStringList out;
    QSet<QString> seen;
    auto it = modelToken.globalMatch(text);
    while (it.hasNext()) {
        // OCR 偶尔会把大写读成小写，统一回大写（型号规范里没有小写）
        QString model = it.next().captured(1).toUpper();
        if (model.size() < 6 || !hasLetter.match(model).hasMatch())
            continue; // 排除 2026-11-05、0377-61170651 这类纯数字日期/电话
        // 型号首段固定是「字母前缀 + 数字」；OCR 把横杠读丢时会切成 C3-48-016
        // 这种碎片，首段过短就丢弃，避免弹窗里出现假型号。
        if (model.indexOf(QLatin1Char('-')) < 4)
            continue;
        const QString key = normalizeModelKey(model);
        if (key.size() < 6 || seen.contains(key))
            continue;
        seen.insert(key);
        out << model;
    }
    if (!out.isEmpty())
        return out;
    // 新版式一个型号都没扫到时，兼容「型号在行尾」的旧表格截图
    return extractLegacyModels(text);
}

} // namespace OcrHelper

#endif // OCRHELPER_H
