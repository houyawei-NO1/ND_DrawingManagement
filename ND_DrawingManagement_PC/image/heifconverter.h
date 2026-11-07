#ifndef HEIFCONVERTER_H
#define HEIFCONVERTER_H

#include <QString>

// HEIC / HEIF 图纸支持。
//
// 图纸库、网页端预览、手机端同步、离线 OCR 都只认 jpg/png/bmp/pdf，
// 所以 HEIC 不直接入库：录入时用 libheif 解码成 QImage，转存成 JPG 后再走原有链路。
//
// libheif 以「运行时动态加载 heif.dll」的方式使用：
//   - 不需要 .lib / 头文件，不改动 CMake 的链接依赖，MinGW 与 MSVC 构建都能编译；
//   - 运行库缺失时功能自动降级，提示用户放置 DLL 或自行转换格式。
// 部署：third_party/libheif 下的 heif.dll 与 libde265-0.dll（HEVC 解码器）
// 由 CMake 在编译后自动复制到 exe 同目录，安装包脚本也会一并打包。
namespace HeifConverter {

// 是否为需要转码的 HEIC / HEIF 文件
bool isHeifFile(const QString &path);

// heif.dll 及其所需的 C API 是否可用
bool available();

// 运行库缺失时给用户的部署提示
QString libraryHint();

// 解码 HEIC/HEIF 并转存为临时 JPG（质量 95）。
// 成功返回临时 JPG 的绝对路径（调用方负责在图纸落盘后删除）；
// 失败返回空字符串，err 给出可直接展示给用户的原因。
QString toJpeg(const QString &path, QString *err = nullptr);

} // namespace HeifConverter

#endif // HEIFCONVERTER_H
