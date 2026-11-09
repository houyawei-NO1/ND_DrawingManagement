#include "xlsreader.h"

#include <QByteArray>
#include <QFile>
#include <QMap>
#include <QSet>

#include <cmath>
#include <cstring>

namespace {

constexpr quint32 kFreeSect    = 0xFFFFFFFFu;
constexpr quint32 kEndOfChain  = 0xFFFFFFFEu;
constexpr quint32 kFatSect     = 0xFFFFFFFDu;

quint16 rd16(const char *p, int o)
{
    return quint16(quint8(p[o])) | (quint16(quint8(p[o + 1])) << 8);
}

quint32 rd32(const char *p, int o)
{
    return quint32(quint8(p[o])) | (quint32(quint8(p[o + 1])) << 8)
           | (quint32(quint8(p[o + 2])) << 16) | (quint32(quint8(p[o + 3])) << 24);
}

quint64 rd64(const char *p, int o)
{
    return quint64(rd32(p, o)) | (quint64(rd32(p, o + 4)) << 32);
}

double rkValue(quint32 rk)
{
    double v = 0.0;
    if (rk & 0x02u) {
        v = double(rk >> 2);
    } else {
        const quint64 bits = quint64(rk & 0xFFFFFFFCu) << 32;
        std::memcpy(&v, &bits, sizeof(v));
    }
    return (rk & 0x01u) ? v / 100.0 : v;
}

QString numberText(double v)
{
    if (std::isfinite(v) && std::fabs(v) < 1e15
        && std::fabs(v - std::floor(v)) < 1e-9)
        return QString::number(qint64(v));
    return QString::number(v, 'g', 15);
}

struct OleFile
{
    QByteArray data;
    int ssz = 512;
    int mssz = 64;
    quint32 miniCutoff = 4096;
    QList<quint32> fat;
    QList<quint32> miniFat;
    QByteArray miniStream;
    QByteArray dir;
    QString error;

    QByteArray sector(quint32 sid) const
    {
        const qint64 off = qint64(sid + 1) * ssz;
        if (off < 0 || off + ssz > data.size())
            return QByteArray();
        return QByteArray::fromRawData(data.constData() + off, ssz);
    }

    QList<quint32> chain(quint32 start, const QList<quint32> &table) const
    {
        QList<quint32> out;
        QSet<quint32> seen;
        quint32 sid = start;
        while (sid < kFatSect && sid < quint32(table.size()) && !seen.contains(sid)) {
            seen.insert(sid);
            out.append(sid);
            sid = table.at(int(sid));
        }
        return out;
    }

    QByteArray readChain(quint32 start, const QList<quint32> &table, qint64 size) const
    {
        QByteArray out;
        if (size > 0)
            out.reserve(qsizetype(qMin<qint64>(size, 64 * 1024 * 1024)));
        for (quint32 sid : chain(start, table)) {
            const QByteArray s = sector(sid);
            if (s.isEmpty())
                break;
            out.append(s);
            if (size > 0 && out.size() >= size)
                break;
        }
        if (size >= 0 && out.size() > size)
            out.truncate(qsizetype(size));
        return out;
    }

    bool parse(const QString &path)
    {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) {
            error = QStringLiteral("无法打开表格文件");
            return false;
        }
        data = f.readAll();
        if (data.size() < 512 || data.left(8) != QByteArray::fromHex("d0cf11e0a1b11ae1")) {
            error = QStringLiteral("不是 Excel 97-2003 的 .xls 文件（若是 .xlsx 请另存为 .xls）");
            return false;
        }
        const char *h = data.constData();
        ssz = 1 << rd16(h, 30);
        mssz = 1 << rd16(h, 32);
        const quint32 numFat = rd32(h, 44);
        const quint32 firstDir = rd32(h, 48);
        miniCutoff = rd32(h, 56);
        const quint32 firstMiniFat = rd32(h, 60);
        const quint32 numMiniFat = rd32(h, 64);
        quint32 firstDifat = rd32(h, 68);
        const quint32 numDifat = rd32(h, 72);
        if (ssz < 512 || mssz < 16) {
            error = QStringLiteral("OLE2 扇区大小异常");
            return false;
        }

        QList<quint32> difat;
        for (int i = 0; i < 109; ++i) {
            const quint32 v = rd32(h, 76 + i * 4);
            if (v < kFatSect)
                difat.append(v);
        }
        for (quint32 n = 0; n < numDifat && firstDifat < kFatSect; ++n) {
            const QByteArray s = sector(firstDifat);
            if (s.size() < ssz)
                break;
            const int cnt = ssz / 4 - 1;
            for (int i = 0; i < cnt; ++i) {
                const quint32 v = rd32(s.constData(), i * 4);
                if (v < kFatSect)
                    difat.append(v);
            }
            firstDifat = rd32(s.constData(), cnt * 4);
        }

        const int perSector = ssz / 4;
        for (int i = 0; i < int(numFat) && i < difat.size(); ++i) {
            const QByteArray s = sector(difat.at(i));
            if (s.size() < ssz)
                continue;
            for (int k = 0; k < perSector; ++k)
                fat.append(rd32(s.constData(), k * 4));
        }
        if (fat.isEmpty()) {
            error = QStringLiteral("OLE2 FAT 为空，文件可能已损坏");
            return false;
        }

        dir = readChain(firstDir, fat, -1);
        quint32 rootStart = kEndOfChain;
        qint64 rootSize = 0;
        quint32 wbStart = kEndOfChain;
        qint64 wbSize = 0;
        bool foundWorkbook = false;

        for (int i = 0; i + 128 <= dir.size(); i += 128) {
            const char *e = dir.constData() + i;
            const quint16 nlen = rd16(e, 64);
            const quint8 type = quint8(e[66]);
            const quint32 start = rd32(e, 116);
            const quint64 size = rd64(e, 120);
            if (type == 5) {
                rootStart = start;
                rootSize = qint64(size);
                continue;
            }
            if (type != 2 || nlen < 4 || nlen > 64)
                continue;
            const QString name = QString::fromUtf16(
                reinterpret_cast<const char16_t *>(e), nlen / 2 - 1).trimmed();
            if (name.compare(QStringLiteral("Workbook"), Qt::CaseInsensitive) == 0
                || name.compare(QStringLiteral("Book"), Qt::CaseInsensitive) == 0) {
                wbStart = start;
                wbSize = qint64(size);
                foundWorkbook = true;
            }
        }
        if (!foundWorkbook) {
            error = QStringLiteral("未找到 Workbook 流，文件可能不是 Excel 工作簿");
            return false;
        }

        if (wbSize < miniCutoff && rootStart < kFatSect && rootSize > 0) {
            miniStream = readChain(rootStart, fat, rootSize);
            const QByteArray mf = readChain(firstMiniFat, fat, qint64(numMiniFat) * ssz);
            for (int i = 0; i + 4 <= mf.size(); i += 4)
                miniFat.append(rd32(mf.constData(), i));
        }
        return true;
    }

    QByteArray workbook(quint32 start, qint64 size) const
    {
        if (size < miniCutoff && !miniStream.isEmpty() && !miniFat.isEmpty()) {
            QByteArray out;
            out.reserve(qsizetype(size));
            for (quint32 sid : chain(start, miniFat)) {
                const qint64 off = qint64(sid) * mssz;
                if (off + mssz > miniStream.size())
                    break;
                out.append(miniStream.constData() + off, mssz);
                if (out.size() >= size)
                    break;
            }
            out.truncate(qsizetype(size));
            return out;
        }
        return readChain(start, fat, size);
    }
};

struct SstCursor
{
    const QList<QByteArray> *segs = nullptr;
    int si = 0;
    int p = 8; // SST 记录头：cstTotal(4) + cstUnique(4)

    bool jump(bool skipFlagByte)
    {
        ++si;
        p = skipFlagByte ? 1 : 0;
        return si < segs->size() && p <= segs->at(si).size();
    }

    bool ensure()
    {
        while (si < segs->size() && p >= segs->at(si).size()) {
            if (!jump(false))
                return false;
        }
        return si < segs->size();
    }

    QByteArray readPlain(int n)
    {
        QByteArray out;
        while (n > 0) {
            if (!ensure())
                break;
            const QByteArray &s = segs->at(si);
            const int take = qMin(n, s.size() - p);
            if (take <= 0)
                break;
            out.append(s.constData() + p, take);
            p += take;
            n -= take;
        }
        return out;
    }

    // 字符数据跨 CONTINUE 时，新段首字节是新一截字符的压缩标志
    QString readChars(int cch, bool wide)
    {
        QString out;
        out.reserve(cch);
        int left = cch;
        while (left > 0) {
            if (!ensure())
                break;
            const QByteArray &s = segs->at(si);
            const int unit = wide ? 2 : 1;
            const int avail = (s.size() - p) / unit;
            if (avail <= 0) {
                if (si + 1 >= segs->size() || segs->at(si + 1).isEmpty())
                    break;
                wide = (quint8(segs->at(si + 1).constData()[0]) & 1u) != 0;
                if (!jump(true))
                    break;
                continue;
            }
            const int take = qMin(left, avail);
            if (wide)
                out += QString::fromUtf16(
                    reinterpret_cast<const char16_t *>(s.constData() + p), take);
            else
                out += QString::fromLocal8Bit(s.constData() + p, take);
            p += take * unit;
            left -= take;
            if (left > 0 && si + 1 < segs->size() && !segs->at(si + 1).isEmpty()) {
                wide = (quint8(segs->at(si + 1).constData()[0]) & 1u) != 0;
                if (!jump(true))
                    break;
            }
        }
        return out;
    }
};

} // namespace

namespace XlsReader {

bool read(const QString &path, QList<QStringList> &rows, QString *err)
{
    rows.clear();
    OleFile ole;
    if (!ole.parse(path)) {
        if (err) *err = ole.error;
        return false;
    }

    // 目录解析时已记录 Workbook 信息，这里重新扫一遍取出来
    quint32 wbStart = kEndOfChain;
    qint64 wbSize = 0;
    for (int i = 0; i + 128 <= ole.dir.size(); i += 128) {
        const char *e = ole.dir.constData() + i;
        const quint16 nlen = rd16(e, 64);
        if (quint8(e[66]) != 2 || nlen < 4)
            continue;
        const QString name = QString::fromUtf16(
            reinterpret_cast<const char16_t *>(e), nlen / 2 - 1).trimmed();
        if (name.compare(QStringLiteral("Workbook"), Qt::CaseInsensitive) == 0
            || name.compare(QStringLiteral("Book"), Qt::CaseInsensitive) == 0) {
            wbStart = rd32(e, 116);
            wbSize = qint64(rd64(e, 120));
            break;
        }
    }
    const QByteArray wb = ole.workbook(wbStart, wbSize);
    if (wb.size() < 8) {
        if (err) *err = QStringLiteral("Workbook 流读取失败");
        return false;
    }

    struct Rec { quint16 id; QByteArray data; };
    QList<Rec> recs;
    for (int pos = 0; pos + 4 <= wb.size();) {
        const quint16 id = rd16(wb.constData(), pos);
        const quint16 len = rd16(wb.constData(), pos + 2);
        if (pos + 4 + len > wb.size())
            break;
        recs.append({id, QByteArray::fromRawData(wb.constData() + pos + 4, len)});
        pos += 4 + len;
    }

    // 共享字符串表：SST 记录 + 紧随其后的 CONTINUE 记录
    QList<QByteArray> segs;
    bool collecting = false;
    for (const Rec &r : recs) {
        if (r.id == 0x00FC) {
            segs.append(r.data);
            collecting = true;
        } else if (collecting && r.id == 0x003C) {
            segs.append(r.data);
        } else if (collecting) {
            break;
        }
    }
    if (segs.isEmpty() || segs.first().size() < 8) {
        if (err) *err = QStringLiteral("未找到共享字符串表（SST）");
        return false;
    }

    SstCursor cur;
    cur.segs = &segs;
    const quint32 total = rd32(segs.first().constData(), 4);
    QStringList strings;
    strings.reserve(qMin<quint32>(total, 200000));
    for (quint32 i = 0; i < total; ++i) {
        const QByteArray head = cur.readPlain(3);
        if (head.size() < 3)
            break;
        const quint16 cch = rd16(head.constData(), 0);
        const quint8 flags = quint8(head[2]);
        const bool wide = (flags & 1u) != 0;
        const int extra = (flags & 8u ? 2 : 0) + (flags & 4u ? 4 : 0);
        const QByteArray ex = cur.readPlain(extra);
        int cRun = 0, cbExt = 0, off = 0;
        if (flags & 8u) { cRun = rd16(ex.constData(), 0); off = 2; }
        if (flags & 4u) cbExt = int(rd32(ex.constData(), off));
        const QString s = cur.readChars(int(cch), wide);
        cur.readPlain(cRun * 4 + cbExt);
        strings.append(s);
    }

    QMap<int, QMap<int, QString>> cells;
    for (const Rec &r : recs) {
        const char *d = r.data.constData();
        const int n = r.data.size();
        switch (r.id) {
        case 0x00FD: // LABELSST
            if (n >= 10) {
                const quint32 idx = rd32(d, 6);
                if (idx < quint32(strings.size()))
                    cells[rd16(d, 0)][rd16(d, 2)] = strings.at(int(idx));
            }
            break;
        case 0x0203: // NUMBER
            if (n >= 14) {
                double v = 0.0;
                std::memcpy(&v, d + 6, sizeof(v));
                cells[rd16(d, 0)][rd16(d, 2)] = numberText(v);
            }
            break;
        case 0x027E: // RK
            if (n >= 10)
                cells[rd16(d, 0)][rd16(d, 2)] = numberText(rkValue(rd32(d, 6)));
            break;
        case 0x00BD: // MULRK
            if (n >= 12) {
                const int cnt = (n - 6) / 6;
                for (int k = 0; k < cnt; ++k)
                    cells[rd16(d, 0)][int(rd16(d, 2)) + k] =
                        numberText(rkValue(rd32(d, 6 + k * 6)));
            }
            break;
        case 0x0204: // LABEL（BIFF5 内联字符串）
            if (n >= 8) {
                const int cch = rd16(d, 6);
                cells[rd16(d, 0)][rd16(d, 2)] =
                    QString::fromLocal8Bit(d + 8, qMin(cch, n - 8));
            }
            break;
        default:
            break;
        }
    }

    int maxCol = 0;
    for (auto it = cells.cbegin(); it != cells.cend(); ++it)
        if (!it.value().isEmpty())
            maxCol = qMax(maxCol, it.value().lastKey());

    for (auto it = cells.cbegin(); it != cells.cend(); ++it) {
        QStringList line;
        line.reserve(maxCol + 1);
        for (int c = 0; c <= maxCol; ++c)
            line.append(it.value().value(c, QString()));
        rows.append(line);
    }
    if (rows.isEmpty() && err)
        *err = QStringLiteral("表格里没有读到任何单元格数据");
    return !rows.isEmpty();
}

} // namespace XlsReader
