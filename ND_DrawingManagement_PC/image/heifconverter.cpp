#include "heifconverter.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QLibrary>
#include <QStringList>
#include <QUuid>
#include <cstring>

namespace {

// libheif 的 C API 统一返回：
//   struct heif_error { enum heif_error_code code; enum heif_suberror_code subcode;
//                       const char* message; };
// 两个 enum 都是 int，Windows x64 下是 16 字节 POD。MSVC 与 MinGW 都遵循同一套
// Windows x64 调用约定，因此用 QLibrary 动态加载 C 接口不需要 .lib，也不挑编译器。
struct HeifError {
    int code;
    int subcode;
    const char *message;
};
static_assert(sizeof(HeifError) == sizeof(int) * 2 + sizeof(const char *),
              "unexpected libheif heif_error layout");

using FnAlloc         = void *(*)();
using FnFreeCtx       = void (*)(void *);
using FnInit          = HeifError (*)(const void *);
using FnRead          = HeifError (*)(void *, const char *, const void *);
using FnPrimary       = HeifError (*)(void *, void **);
using FnSize          = int (*)(const void *);
using FnDecode        = HeifError (*)(const void *, void **, int, int, const void *);
using FnPlane         = const unsigned char *(*)(const void *, int, int *);
using FnReleaseHandle = void (*)(const void *);
using FnReleaseImage  = void (*)(const void *);

// 取自 libheif 公共头文件 heif.h（这些枚举值长期保持稳定）
constexpr int kColorspaceRGB          = 1;  // heif_colorspace_RGB
constexpr int kChromaInterleavedRGB  = 10; // heif_chroma_interleaved_RGB
constexpr int kChannelInterleaved    = 10; // heif_channel_interleaved

struct Api {
    QLibrary lib;
    FnAlloc alloc = nullptr;
    FnFreeCtx freeCtx = nullptr;
    FnInit init = nullptr;             // heif_init 为可选符号（旧版没有）
    FnRead read = nullptr;
    FnPrimary primary = nullptr;
    FnSize width = nullptr;
    FnSize height = nullptr;
    FnDecode decode = nullptr;
    FnPlane plane = nullptr;
    FnReleaseHandle releaseHandle = nullptr;
    FnReleaseImage releaseImage = nullptr;
    bool ok = false;
};

void loadApi(Api &a)
{
    const QStringList candidates = {
        QCoreApplication::applicationDirPath() + QStringLiteral("/heif.dll"),
        QStringLiteral("heif"),
        QStringLiteral("libheif")
    };
    for (const QString &name : candidates) {
        a.lib.setFileName(name);
        if (!a.lib.load())
            continue;
        a.alloc         = reinterpret_cast<FnAlloc>(a.lib.resolve("heif_context_alloc"));
        a.freeCtx       = reinterpret_cast<FnFreeCtx>(a.lib.resolve("heif_context_free"));
        a.init          = reinterpret_cast<FnInit>(a.lib.resolve("heif_init"));
        a.read          = reinterpret_cast<FnRead>(a.lib.resolve("heif_context_read_from_file"));
        a.primary       = reinterpret_cast<FnPrimary>(a.lib.resolve("heif_context_get_primary_image_handle"));
        a.width         = reinterpret_cast<FnSize>(a.lib.resolve("heif_image_handle_get_width"));
        a.height        = reinterpret_cast<FnSize>(a.lib.resolve("heif_image_handle_get_height"));
        a.decode        = reinterpret_cast<FnDecode>(a.lib.resolve("heif_decode_image"));
        a.plane         = reinterpret_cast<FnPlane>(a.lib.resolve("heif_image_get_plane_readonly"));
        a.releaseHandle = reinterpret_cast<FnReleaseHandle>(a.lib.resolve("heif_image_handle_release"));
        a.releaseImage  = reinterpret_cast<FnReleaseImage>(a.lib.resolve("heif_image_release"));
        if (a.alloc && a.freeCtx && a.read && a.primary && a.width && a.height
            && a.decode && a.plane && a.releaseHandle && a.releaseImage) {
            if (a.init)
                a.init(nullptr); // 引用计数初始化，整个进程只做一次
            a.ok = true;
            return;
        }
        a.lib.unload();
    }
}

const Api &api()
{
    static Api *a = [] {
        Api *p = new Api;
        loadApi(*p);
        return p;
    }();
    return *a;
}

QString errorText(const HeifError &e)
{
    return e.message ? QString::fromUtf8(e.message) : QStringLiteral("libheif 解码失败");
}

} // namespace

namespace HeifConverter {

bool isHeifFile(const QString &path)
{
    const QString s = QFileInfo(path).suffix().toLower();
    return s == QStringLiteral("heic") || s == QStringLiteral("heif");
}

bool available()
{
    return api().ok;
}

QString libraryHint()
{
    return QStringLiteral(
        "HEIC 解码运行库缺失。请确认程序目录下有 heif.dll 与 libde265-0.dll"
        "（安装包会随附这两个文件）；若文件损坏，可先把 HEIC 图纸另存为 JPG / PNG 再录入。");
}

QString toJpeg(const QString &path, QString *err)
{
    auto fail = [err](const QString &msg) {
        if (err) *err = msg;
        return QString();
    };

    const Api &a = api();
    if (!a.ok)
        return fail(libraryHint());

    void *ctx = a.alloc();
    if (!ctx)
        return fail(QStringLiteral("libheif 上下文创建失败"));

    const QByteArray file = QFileInfo(path).absoluteFilePath().toUtf8();
    HeifError e = a.read(ctx, file.constData(), nullptr);
    if (e.code != 0) {
        const QString msg = QStringLiteral("无法读取 HEIC 文件：%1").arg(errorText(e));
        a.freeCtx(ctx);
        return fail(msg);
    }

    void *handle = nullptr;
    e = a.primary(ctx, &handle);
    if (e.code != 0 || !handle) {
        const QString msg = QStringLiteral("HEIC 主图像不存在：%1").arg(errorText(e));
        a.freeCtx(ctx);
        return fail(msg);
    }

    const int w = a.width(handle), h = a.height(handle);
    if (w <= 0 || h <= 0 || w > 30000 || h > 30000) {
        a.releaseHandle(handle);
        a.freeCtx(ctx);
        return fail(QStringLiteral("HEIC 图像尺寸异常"));
    }

    void *img = nullptr;
    e = a.decode(handle, &img, kColorspaceRGB, kChromaInterleavedRGB, nullptr);
    if (e.code != 0 || !img) {
        const QString msg = QStringLiteral("HEIC 解码失败：%1").arg(errorText(e));
        a.releaseHandle(handle);
        a.freeCtx(ctx);
        return fail(msg);
    }

    int stride = 0;
    const unsigned char *plane = a.plane(img, kChannelInterleaved, &stride);
    if (!plane || stride < w * 3) {
        a.releaseImage(img);
        a.releaseHandle(handle);
        a.freeCtx(ctx);
        return fail(QStringLiteral("HEIC 像素数据读取失败"));
    }

    // interleaved_RGB 每行是 R,G,B 顺序，与 QImage::Format_RGB888 一致；
    // stride 可能带行尾 padding，必须逐行拷贝。
    QImage out(w, h, QImage::Format_RGB888);
    if (out.isNull()) {
        a.releaseImage(img);
        a.releaseHandle(handle);
        a.freeCtx(ctx);
        return fail(QStringLiteral("内存不足，无法生成 JPG 图纸"));
    }
    for (int y = 0; y < h; ++y)
        std::memcpy(out.scanLine(y), plane + size_t(y) * size_t(stride), size_t(w) * 3);

    a.releaseImage(img);
    a.releaseHandle(handle);
    a.freeCtx(ctx);

    // 临时文件名带上原文件名，附件列表里能看出来源；GUID 避免同名覆盖
    QString base;
    for (const QChar c : QFileInfo(path).completeBaseName())
        base.append(c.isLetterOrNumber() || c == QLatin1Char('_') || c == QLatin1Char('-')
                        ? c : QLatin1Char('_'));
    if (base.isEmpty())
        base = QStringLiteral("drawing");
    const QString dst = QDir::tempPath() + QStringLiteral("/nd_heif_%1_%2.jpg")
                            .arg(base.left(60),
                                 QUuid::createUuid().toString(QUuid::WithoutBraces).left(8));
    if (!out.save(dst, "JPEG", 95))
        return fail(QStringLiteral("HEIC 转存 JPG 失败（临时目录不可写）"));
    return dst;
}

} // namespace HeifConverter
