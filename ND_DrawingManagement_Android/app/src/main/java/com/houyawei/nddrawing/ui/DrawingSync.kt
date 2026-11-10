package com.houyawei.nddrawing.ui

import android.content.Context
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.platform.LocalContext
import com.houyawei.nddrawing.data.Drawing
import com.houyawei.nddrawing.data.Repository
import com.houyawei.nddrawing.data.SyncClient
import com.houyawei.nddrawing.data.localDrawingFile
import java.io.File

// 进入产品页时的「按型号补图」：只下载该产品 drawingsOf(id) 里本地缺失的图纸文件。
// 不写数据库：文件按 relPath 落到 Repository.baseDir（Download/南都图纸/<分类>/<名称 型号>/…），
// 与「设置 → 图纸管理」扫描/删除的目录完全一致，页面只是读取落盘文件用于预览。

// 服务器地址与「设置 → 数据同步」共用同一份配置
internal fun lanBaseUrl(ctx: Context): String? {
    val p = ctx.getSharedPreferences("sync", Context.MODE_PRIVATE)
    val host = (p.getString("host", "") ?: "").trim()
    val port = (p.getString("port", "8080") ?: "8080").trim().ifEmpty { "8080" }
    return if (host.isEmpty()) null else "http://$host:$port"
}

// 取图路径：优先「南都图纸」目录下按 relPath 落盘的文件（与下载落点一致），
// 数据库未写 local_path 或文件被删时也能正确找到刚补下来的图纸
internal fun localDrawingPath(d: Drawing): String {
    if (d.relPath.isNotBlank()) {
        val byRel = localDrawingFile(Repository.baseDir, d.relPath)
        if (byRel.isFile) return byRel.absolutePath
    }
    if (d.localPath.isNotEmpty() && File(d.localPath).exists()) return d.localPath
    return if (d.relPath.isNotBlank())
        localDrawingFile(Repository.baseDir, d.relPath).absolutePath
    else d.localPath
}

internal data class ProductDrawingSyncState(
    val busy: Boolean = false,
    val status: String = "",
    val revision: Int = 0
)

@Composable
internal fun rememberProductDrawingSync(drawings: List<Drawing>): ProductDrawingSyncState {
    val ctx = LocalContext.current
    var busy by remember { mutableStateOf(false) }
    var status by remember { mutableStateOf("") }
    var revision by remember { mutableStateOf(0) }
    val ids = drawings.map { it.id }

    LaunchedEffect(ids) {
        // 只挑本产品、且本地确实缺失的文件；relPath 为空的历史数据无法定位，跳过
        val missing = drawings.filter { d ->
            if (d.relPath.isBlank()) return@filter false
            val f = localDrawingFile(Repository.baseDir, d.relPath)
            !f.exists() || f.length() == 0L
        }
        if (missing.isEmpty()) return@LaunchedEffect

        val base = lanBaseUrl(ctx)
        if (base == null) {
            status = "未配置服务器地址，可在「设置 → 数据同步」连接局域网后自动补齐图纸"
            return@LaunchedEffect
        }
        val client = SyncClient(base)
        if (!client.probe()) {
            status = "未连接局域网服务器，仅显示本机已下载的图纸"
            return@LaunchedEffect
        }

        busy = true
        var ok = 0
        missing.forEachIndexed { i, d ->
            status = "正在从局域网补充图纸 ${i + 1}/${missing.size}"
            val r = client.downloadFile(d.relPath, Repository.baseDir, i + 1, missing.size)
            if (r.result != SyncClient.DownloadResult.Failed) ok++
            revision++ // 不写数据库，靠重组让详情页重新读取刚落盘的文件
        }
        busy = false
        status = if (ok > 0) "已从局域网补充 $ok 张图纸（可在「设置 → 图纸管理」删除）"
        else "图纸补充失败，请检查服务器地址与图纸目录"
    }

    return ProductDrawingSyncState(busy, status, revision)
}
