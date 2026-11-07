package com.houyawei.nddrawing.ui

import android.content.Context
import android.content.Intent
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.graphics.Matrix
import android.media.ExifInterface
import android.net.Uri
import androidx.compose.ui.graphics.ImageBitmap
import androidx.compose.ui.graphics.asImageBitmap
import androidx.core.content.FileProvider
import java.io.File

// 从 EXIF 解析需要顺时针旋转的角度（0 / 90 / 180 / 270），无标记或解析失败返回 0
fun exifRotationDegrees(path: String): Int {
    return try {
        val exif = ExifInterface(path)
        when (exif.getAttributeInt(
            ExifInterface.TAG_ORIENTATION, ExifInterface.ORIENTATION_NORMAL
        )) {
            ExifInterface.ORIENTATION_ROTATE_90 -> 90
            ExifInterface.ORIENTATION_ROTATE_180 -> 180
            ExifInterface.ORIENTATION_ROTATE_270 -> 270
            else -> 0
        }
    } catch (e: Exception) {
        0
    }
}

// 「用相册打开」方向正常、App 内却是歪的：这些 JPEG 带 EXIF 方向标记，
// 而 BitmapFactory.decodeFile 不会自动旋转（系统相册会按 EXIF 转正）。
// 这里按 EXIF 把位图旋转到正确方向后再显示。
fun applyExifRotation(src: Bitmap, path: String): ImageBitmap {
    val deg = exifRotationDegrees(path)
    if (deg == 0) return src.asImageBitmap()
    return try {
        val m = Matrix().apply { postRotate(deg.toFloat()) }
        val out = Bitmap.createBitmap(src, 0, 0, src.width, src.height, m, true)
        if (out !== src) src.recycle()
        out.asImageBitmap()
    } catch (e: Exception) {
        src.asImageBitmap()
    }
}

// 读取本地图片文件为 Compose ImageBitmap（简单降采样避免大图 OOM）
fun loadBitmap(path: String): ImageBitmap? {
    val f = File(path)
    if (!f.exists()) return null
    return try {
        val opts = BitmapFactory.Options().apply { inSampleSize = 2 }
        val src = BitmapFactory.decodeFile(path, opts) ?: return null
        applyExifRotation(src, path)
    } catch (e: Exception) {
        null
    }
}

// 用系统阅读器打开下载到内部存储的 PDF（通过 FileProvider 共享）
fun openPdf(context: Context, path: String) {
    val f = File(path)
    if (!f.exists()) return
    val uri = FileProvider.getUriForFile(context, context.packageName + ".fileprovider", f)
    val intent = Intent(Intent.ACTION_VIEW).apply {
        setDataAndType(uri, "application/pdf")
        addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
    }
    context.startActivity(Intent.createChooser(intent, "打开 PDF"))
}

// 查看大图用：先读取尺寸，按最大边限制降采样，避免超大扫描件 OOM，同时保证放大后仍清晰
fun loadBitmapForZoom(path: String, maxDim: Int = 3000): ImageBitmap? {
    val f = File(path)
    if (!f.exists()) return null
    return try {
        val bounds = BitmapFactory.Options().apply { inJustDecodeBounds = true }
        BitmapFactory.decodeFile(path, bounds)
        var sample = 1
        if (bounds.outWidth > 0 && bounds.outHeight > 0) {
            while (bounds.outWidth / sample > maxDim || bounds.outHeight / sample > maxDim) {
                sample *= 2
            }
        }
        val opts = BitmapFactory.Options().apply { inSampleSize = sample }
        val src = BitmapFactory.decodeFile(path, opts) ?: return null
        applyExifRotation(src, path) // 按 EXIF 转正，与相册打开一致
    } catch (e: Exception) {
        null
    }
}

// 用手机相册/图片查看器打开本地图片（通过 FileProvider 共享）
fun openImage(context: Context, path: String) {
    val f = File(path)
    if (!f.exists()) return
    val uri = FileProvider.getUriForFile(context, context.packageName + ".fileprovider", f)
    val mime = when (f.extension.lowercase()) {
        "png" -> "image/png"
        "webp" -> "image/webp"
        "bmp" -> "image/bmp"
        "gif" -> "image/gif"
        else -> "image/jpeg"
    }
    val intent = Intent(Intent.ACTION_VIEW).apply {
        setDataAndType(uri, mime)
        addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
    }
    try {
        context.startActivity(Intent.createChooser(intent, "用相册打开"))
    } catch (_: Exception) {
        // 没有可用的图片查看器时忽略
    }
}

// 分享纯文本（产品详情等）
fun shareText(context: Context, text: String, title: String = "分享") {
    if (text.isBlank()) return
    val intent = Intent(Intent.ACTION_SEND).apply {
        type = "text/plain"
        putExtra(Intent.EXTRA_TEXT, text)
    }
    context.startActivity(Intent.createChooser(intent, title))
}

// 分享本地图片/PDF 文件（通过 FileProvider 授予临时读取权限）
fun shareImageFile(context: Context, path: String, title: String = "分享图片") {
    val f = File(path)
    if (!f.exists()) return
    val uri = FileProvider.getUriForFile(context, context.packageName + ".fileprovider", f)
    val mime = when (f.extension.lowercase()) {
        "png" -> "image/png"
        "webp" -> "image/webp"
        "bmp" -> "image/bmp"
        "gif" -> "image/gif"
        "pdf" -> "application/pdf"
        else -> "image/jpeg"
    }
    val intent = Intent(Intent.ACTION_SEND).apply {
        type = mime
        putExtra(Intent.EXTRA_STREAM, uri)
        addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
    }
    context.startActivity(Intent.createChooser(intent, title))
}

// 一次性分享多张本地图片（通过 FileProvider 授予临时读取权限）
fun shareImageFiles(context: Context, paths: List<String>, title: String = "分享图片") {
    val uris = ArrayList<Uri>()
    for (p in paths) {
        val f = File(p)
        if (f.exists()) uris.add(FileProvider.getUriForFile(context, context.packageName + ".fileprovider", f))
    }
    if (uris.isEmpty()) return
    val intent = Intent(Intent.ACTION_SEND_MULTIPLE).apply {
        type = "image/*"
        putParcelableArrayListExtra(Intent.EXTRA_STREAM, uris)
        addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
    }
    context.startActivity(Intent.createChooser(intent, title))
}

// 分享内存中的 Bitmap（如整页截图为图片）
fun shareImageBitmap(context: Context, bitmap: android.graphics.Bitmap, title: String = "分享图片") {
    val dir = File(context.cacheDir, "share")
    dir.mkdirs()
    val f = File(dir, "nd_${System.currentTimeMillis()}.png")
    f.outputStream().use { bitmap.compress(android.graphics.Bitmap.CompressFormat.PNG, 100, it) }
    shareImageFile(context, f.absolutePath, title)
}
