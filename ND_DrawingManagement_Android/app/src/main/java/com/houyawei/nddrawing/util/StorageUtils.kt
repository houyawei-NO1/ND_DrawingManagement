package com.houyawei.nddrawing.util

import android.content.Context
import java.io.File
import java.util.Locale

/**
 * 图纸目录与存储占用统计。
 * 目录结构：Repository.baseDir/<分类>/<型号>/<图纸名>。
 */
object StorageUtils {

    /** 递归统计目录下所有文件的总字节数；exclude 子目录会被跳过（避免与图纸统计重复计算） */
    fun dirSize(dir: File, exclude: File? = null): Long {
        if (!dir.exists()) return 0L
        val skip = exclude?.absolutePath
        return try {
            dir.walkTopDown()
                .filter { f -> f.isFile && (skip == null || !f.absolutePath.startsWith(skip)) }
                .sumOf { it.length() }
        } catch (_: Exception) {
            0L
        }
    }

    fun categorySize(base: File, category: String): Long = dirSize(File(base, category))

    fun totalSize(base: File): Long = dirSize(base)

    /** APP 占用 = APK + 内部私有数据 + 外部私有目录（排除图纸根目录） */
    fun appUsage(ctx: Context, exclude: File? = null): Long {
        var total = 0L
        try {
            val apk = ctx.applicationInfo.sourceDir
            if (!apk.isNullOrEmpty()) total += File(apk).length()
        } catch (_: Exception) {
        }
        ctx.filesDir.parentFile?.let { total += dirSize(it, exclude) }
        ctx.getExternalFilesDir(null)?.let { total += dirSize(it, exclude) }
        return total
    }

    /** 删除某分类下的全部图纸文件（自底向上）；保留目录骨架与数据库记录，下次同步可重新下载 */
    fun deleteCategory(base: File, category: String): Boolean {
        val dir = File(base, category)
        if (!dir.exists()) return true
        return try {
            dir.walkBottomUp().forEach { f ->
                if (f != dir && f.exists()) f.delete()
            }
            true
        } catch (_: Exception) {
            false
        }
    }

    fun formatSize(bytes: Long): String {
        if (bytes <= 0L) return "0 KB"
        val kb = bytes / 1024.0
        return when {
            kb < 1024 -> "%.1f KB".format(Locale.US, kb)
            kb / 1024.0 < 1024 -> "%.2f MB".format(Locale.US, kb / 1024.0)
            else -> "%.2f GB".format(Locale.US, kb / 1024.0 / 1024.0)
        }
    }
}
