package com.houyawei.nddrawing.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import android.content.Context
import androidx.navigation.NavHostController
import com.houyawei.nddrawing.data.DEFAULT_CATEGORIES
import com.houyawei.nddrawing.data.Drawing
import com.houyawei.nddrawing.data.Repository
import com.houyawei.nddrawing.data.SyncClient
import com.houyawei.nddrawing.util.StoragePermission
import com.houyawei.nddrawing.util.UsbSync
import kotlinx.coroutines.launch
import java.text.SimpleDateFormat
import java.util.*

@Composable
fun SyncScreen(nav: NavHostController) {
    val ctx = LocalContext.current
    val prefs = ctx.getSharedPreferences("sync", Context.MODE_PRIVATE)
    var host by remember { mutableStateOf(prefs.getString("host", "") ?: "") }
    var port by remember { mutableStateOf(prefs.getString("port", "8080") ?: "8080") }
    var history by remember { mutableStateOf(readHosts(prefs)) }
    var status by remember { mutableStateOf("") }
    var syncing by remember { mutableStateOf(false) }
    var progress by remember { mutableStateOf(-1f) }
    var phase by remember { mutableStateOf("") }
    var skipped by remember { mutableStateOf(0) }
    var downloaded by remember { mutableStateOf(0) }
    var failed by remember { mutableStateOf(0) }
    // 同步流程阶段：input（填写 IP/选择导入方式）/ downloading（下载中）
    var syncPhase by remember { mutableStateOf("input") }
    // 按分类勾选要下载的图纸文件：未勾选的分类只同步数据库（图纸清单），不下载文件。
    // 选择持久化，避免每次进页面都要重选；默认全不选，防止一次性下载服务器全部图纸。
    val categories = remember {
        val stored = Repository.db.getCategories()
        if (stored.isEmpty()) DEFAULT_CATEGORIES else stored
    }
    var selectedCats by remember {
        mutableStateOf<Set<String>>(
            prefs.getStringSet("drawing_categories", emptySet())?.toSet() ?: emptySet()
        )
    }
    fun persistCats(next: Set<String>) {
        selectedCats = next
        prefs.edit().putStringSet("drawing_categories", next).apply()
    }
    // 图纸相对路径首段就是分类，如「控制器/LP3004-CA-48-0530/xxx.pdf」
    fun categoryOf(d: Drawing): String =
        d.relPath.replace('\\', '/').split('/').firstOrNull { it.isNotEmpty() } ?: ""
    val scope = rememberCoroutineScope()
    val logs = remember { mutableStateListOf<String>() }
    val mainHandler = android.os.Handler(android.os.Looper.getMainLooper())
    fun logLine(s: String) {
        val t = SimpleDateFormat("HH:mm:ss", Locale.US).format(Date())
        val line = "[$t] $s"
        // 进度回调发生在 IO 线程，统一丢回主线程更新 Compose 状态
        mainHandler.post { logs.add(line) }
    }

    // 执行下载：所有下载逻辑共用（手动选中 / USB 导入全量）。在调用协程内挂起执行。
    suspend fun performDownload(list: List<Drawing>) {
        try {
            syncing = true
            status = ""
            skipped = 0
            downloaded = 0
            failed = 0
            progress = 0f
            syncPhase = "downloading"
            phase = "正在下载图纸 0/${list.size}……"
            logLine("开始下载图纸，共 ${list.size} 张")
            val client = SyncClient("http://$host:$port")
            list.forEachIndexed { i, d ->
                val name = d.relPath.substringAfterLast("/")
                val typeLabel = if (name.endsWith(".pdf", ignoreCase = true)) "PDF" else "图片"
                val segs = d.relPath.replace('\\', '/').split('/').filter { it.isNotEmpty() }
                val model = if (segs.size >= 2) segs[segs.size - 2] else name.substringBeforeLast('.')
                phase = "正在下载图纸 ${i + 1}/${list.size}：$model $typeLabel"
                progress = (i + 1).toFloat() / list.size
                logLine("↓ [${i + 1}/${list.size}] $name（$typeLabel）")
                val f = client.downloadFile(d.relPath, Repository.baseDir, i + 1, list.size) { pct ->
                    logLine("  … $name  进度 $pct%")
                }
                when (f.result) {
                    SyncClient.DownloadResult.Skipped -> {
                        skipped++
                        logLine("  ⏭ 跳过 $name（$typeLabel，已存在 ${formatSize(f.bytes)}）")
                    }
                    SyncClient.DownloadResult.Downloaded -> {
                        downloaded++
                        val reconnect = if (f.attempts > 1) "（重连 ${f.attempts - 1} 次后成功）" else ""
                        val sizeText = if (f.expected > 0) "${formatSize(f.bytes)}/${formatSize(f.expected)}" else formatSize(f.bytes)
                        logLine("  ✓ 完成 $name（$typeLabel $sizeText）$reconnect")
                    }
                    SyncClient.DownloadResult.Failed -> {
                        failed++
                        logLine("  ✗ 失败 $name：${f.error}（下次同步可续传）")
                    }
                }
                Repository.db.upsertDrawing(d.copy(localPath = f.local))
            }
            progress = -1f
            if (failed > 0) {
                syncPhase = "input"
                status = "下载完成：新增/更新 $downloaded，跳过 $skipped，失败 $failed。再次点击「无线导入」可续传（已下载的会自动跳过）。"
                logLine("下载结束：成功 $downloaded，跳过 $skipped，失败 $failed。失败项可再次导入续传。")
            } else {
                status = "同步完成：图纸 ${list.size} 张（新增/更新 $downloaded，已存在跳过 $skipped）"
                logLine("同步完成：全部成功（下载 $downloaded，跳过 $skipped）")
                nav.navigate("list") { popUpTo("settings/sync") { inclusive = true } }
            }
        } catch (e: Exception) {
            status = "同步失败：${e.message}"
            logLine("同步失败：${e.message}")
            progress = -1f
            syncPhase = "input"
        } finally {
            syncing = false
        }
    }

    // 无线导入：从电脑拉取数据库；勾选「图纸」时连全部图纸一起下载，未勾选则只同步数据库内容
    fun connectAndFetch() {
        if (host.isBlank()) { status = "请输入电脑端 IP"; return }
        scope.launch {
            try {
                syncing = true
                status = ""
                progress = -1f
                phase = "连接中……"
                logs.clear()
                logLine("开始无线导入 → http://$host:$port")
                val client = SyncClient("http://$host:$port")
                val schema = client.fetchSchema()
                val localSchema = prefs.getInt("schema", -1)
                logLine("协议版本 schema=$schema（本地 $localSchema）")
                if (localSchema != schema) {
                    Repository.db.clearAll()
                    logLine("schema 变化，已清空本地库并重建")
                }
                phase = "正在拉取产品列表……"
                val products = client.fetchProducts("")
                products.forEach { Repository.db.upsertProduct(it) }
                logLine("产品列表已同步：${products.size} 条，已写入本地库")
                phase = "正在拉取分类……"
                val cats = client.fetchCategories()
                Repository.db.saveCategories(cats)
                logLine("分类已同步：${cats.size} 项 → ${cats.joinToString("、")}")
                phase = "正在拉取字段配置……"
                val fields = client.fetchFieldConfigs()
                Repository.db.saveFieldConfigs(fields)
                logLine(
                    if (fields.isEmpty()) "字段配置未获取到（可能为旧版 PC 端），使用内置标签"
                    else "字段配置已同步：${fields.size} 项"
                )
                phase = "正在拉取图纸清单……"
                val ds = client.fetchDrawings("")
                // 全量清单：删除本地多出来（服务端已不存在）的图纸记录，避免幽灵条目
                Repository.db.pruneDrawingsExcept(ds.map { it.id }.toSet())
                logLine("图纸清单已拉取：${ds.size} 张")
                val now = SimpleDateFormat("yyyy-MM-dd'T'HH:mm:ss", Locale.US).format(Date())
                val entry = "$host:$port"
                val newHistory = (listOf(entry) + history.filter { it != entry }).take(10)
                history = newHistory
                prefs.edit()
                    .putInt("schema", schema)
                    .putString("last_sync", now)
                    .putString("host", host)
                    .putString("port", port)
                    .putString("hosts", writeHosts(newHistory))
                    .apply()
                // 数据库清单始终全量同步；只有勾选分类的图纸才下载文件，
                // 未勾选分类仅写入清单记录，避免服务器图纸太多把手机塞满。
                val toDownload = ds.filter { categoryOf(it) in selectedCats }
                val dbOnly = ds.filter { categoryOf(it) !in selectedCats }
                dbOnly.forEach { Repository.db.upsertDrawingKeepLocal(it) }
                logLine("图纸清单 ${ds.size} 张：勾选分类需下载 ${toDownload.size} 张，其余 ${dbOnly.size} 张仅写数据库")
                if (toDownload.isEmpty()) {
                    logLine("未勾选任何需下载的分类：仅同步数据库，不下载图纸文件")
                    status = "数据库同步完成：产品 ${products.size} 条，图纸清单 ${ds.size} 条"
                    nav.navigate("list") { popUpTo("settings/sync") { inclusive = true } }
                } else {
                    logLine("已勾选分类：${selectedCats.joinToString("、")}，开始下载 ${toDownload.size} 张")
                    performDownload(toDownload)
                }
            } catch (e: Exception) {
                status = "无线导入失败：${e.message}"
                logLine("无线导入失败：${e.message}")
            } finally {
                syncing = false
            }
        }
    }

    // USB 导入：通过 USB 网络共享自动查找电脑并全量下载（无需手动复制/输入 IP）
    fun doUsbImport() {
        if (syncing) return
        scope.launch {
            syncing = true
            status = ""
            logs.clear()
            progress = -1f
            phase = "正在通过 USB 查找电脑…"
            try {
                val portNum = port.toIntOrNull() ?: 8080
                val h = UsbSync.findHost(ctx, host, portNum)
                if (h == null) {
                    status = "未找到电脑。请先用 USB 数据线连上电脑，并在手机端开启「USB 网络共享」（设置→连接→USB 网络共享，手机当热点），" +
                            "或在下方「无线导入」里填写电脑地址后导入。"
                    logLine("未找到电脑：USB 子网内无 8080 可达主机，且已保存 IP 不可达")
                    syncing = false
                    return@launch
                }
                logLine("找到电脑：$h，开始连接…")
                host = h // 记住地址，便于下次
                prefs.edit().putString("host", h).putString("port", port).apply()
                val client = SyncClient("http://$h:$port")
                client.postHello(UsbSync.deviceName(ctx)) // 上报设备名，供 PC 设置页显示
                val schema = client.fetchSchema()
                val localSchema = prefs.getInt("schema", -1)
                logLine("协议版本 schema=$schema（本地 $localSchema）")
                if (localSchema != schema) {
                    Repository.db.clearAll()
                    logLine("schema 变化，已清空本地库并重建")
                }
                phase = "正在拉取产品列表…"
                val products = client.fetchProducts("")
                products.forEach { Repository.db.upsertProduct(it) }
                logLine("产品列表已同步：${products.size} 条")
                phase = "正在拉取分类与字段配置…"
                val cats = client.fetchCategories()
                Repository.db.saveCategories(cats)
                val fields = client.fetchFieldConfigs()
                Repository.db.saveFieldConfigs(fields)
                logLine("字段配置已同步：${fields.size} 项")
                val ds = client.fetchDrawings("")
                // 全量清单：删除本地多出来（服务端已不存在）的图纸记录
                Repository.db.pruneDrawingsExcept(ds.map { it.id }.toSet())
                val toDownload = ds.filter { categoryOf(it) in selectedCats }
                ds.filter { categoryOf(it) !in selectedCats }.forEach { Repository.db.upsertDrawingKeepLocal(it) }
                logLine("图纸清单已拉取：${ds.size} 张，按勾选分类下载 ${toDownload.size} 张，其余仅写数据库")
                if (toDownload.isEmpty()) {
                    status = "USB 导入完成：仅同步数据库，图纸清单 ${ds.size} 条（未勾选下载分类）"
                    nav.navigate("list") { popUpTo("settings/sync") { inclusive = true } }
                } else {
                    phase = "USB 已连接，正在下载勾选分类的图纸…"
                    performDownload(toDownload) // 下载完成后自动跳转主页
                }
            } catch (e: Exception) {
                status = "USB网络共享导入失败：${e.message}"
                logLine("USB网络共享导入失败：${e.message}")
                syncing = false
            }
        }
    }

    Column(modifier = Modifier.fillMaxSize().navigationBarsPadding()) {
        // 顶部 PC 风格菜单栏
        var showAbout by remember { mutableStateOf(false) }
        AppMenuBar(current = "settings", nav = nav, onAbout = { showAbout = true })
        if (showAbout) AboutDialog(onDismiss = { showAbout = false })
        SettingsSubPageHeader(title = "数据同步", onBack = { nav.popBackStack() })
        Column(
            modifier = Modifier.fillMaxSize().padding(16.dp).verticalScroll(rememberScrollState()),
            verticalArrangement = Arrangement.Top
        ) {
            Spacer(Modifier.height(4.dp))
        if (Repository.usesPublicDownload) {
            Text(
                "保存位置：${Repository.baseDir.absolutePath}",
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
        } else {
            Text(
                "保存位置：${Repository.baseDir.absolutePath}（应用私有）",
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.error
            )
            TextButton(onClick = { StoragePermission.open(ctx) }) {
                Text("授予存储权限，改存到 Download/南都图纸")
            }
        }
        // ===== 无线导入 =====
        Spacer(Modifier.height(16.dp))
        Text("无线导入", style = MaterialTheme.typography.titleSmall)

        if (syncPhase != "downloading") {
            Spacer(Modifier.height(8.dp))
            OutlinedTextField(
                host, { host = it }, label = { Text("电脑端 IP") },
                singleLine = true, modifier = Modifier.fillMaxWidth()
            )
            Spacer(Modifier.height(8.dp))
            OutlinedTextField(
                port, { port = it }, label = { Text("端口") },
                singleLine = true, modifier = Modifier.fillMaxWidth()
            )
            if (history.isNotEmpty()) {
                Spacer(Modifier.height(12.dp))
                Text("历史连接", style = MaterialTheme.typography.labelMedium)
                Spacer(Modifier.height(6.dp))
                LazyRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    items(history) { entry ->
                        val parts = entry.split(":", limit = 2)
                        val ip = if (parts.size == 2) parts[0] else entry
                        val pt = if (parts.size == 2) parts[1] else "8080"
                        FilterChip(
                            selected = false,
                            onClick = { host = ip; port = pt },
                            label = { Text(entry) }
                        )
                    }
                }
            }
            Spacer(Modifier.height(12.dp))
            Text("同步图纸文件（按分类勾选）", style = MaterialTheme.typography.titleSmall)
            Row(verticalAlignment = Alignment.CenterVertically) {
                TextButton(onClick = { persistCats(categories.toSet()) }, enabled = !syncing) {
                    Text("全选", fontSize = 12.sp)
                }
                TextButton(onClick = { persistCats(emptySet()) }, enabled = !syncing) {
                    Text("全不选", fontSize = 12.sp)
                }
                Spacer(Modifier.weight(1f))
                Text(
                    "已选 ${selectedCats.size}/${categories.size}",
                    fontSize = 12.sp,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
            }
            categories.chunked(2).forEach { pair ->
                Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
                    pair.forEach { cat ->
                        Row(Modifier.weight(1f), verticalAlignment = Alignment.CenterVertically) {
                            Checkbox(
                                checked = cat in selectedCats,
                                onCheckedChange = { on ->
                                    persistCats(if (on) selectedCats + cat else selectedCats - cat)
                                },
                                enabled = !syncing
                            )
                            Text(cat, fontSize = 13.sp)
                        }
                    }
                    if (pair.size == 1) Spacer(Modifier.weight(1f))
                }
            }
            Spacer(Modifier.height(4.dp))
            Text(
                "只有勾选的分类会下载图纸文件；未勾选的分类只同步数据库（产品、分类、图纸清单），不下载文件。无线 / 有线导入均按此设置。",
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
            Spacer(Modifier.height(8.dp))
            Button(
                onClick = { connectAndFetch() },
                enabled = !syncing,
                modifier = Modifier.fillMaxWidth()
            ) { Text(if (syncing) "导入中……" else "无线导入") }
            Spacer(Modifier.height(4.dp))
            Text(
                "使用无线导入时关闭手机移动网络增加传输效率。",
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
        } else {
            Spacer(Modifier.height(8.dp))
            LinearProgressIndicator(progress = progress, modifier = Modifier.fillMaxWidth())
            Spacer(Modifier.height(6.dp))
            Text(phase, style = MaterialTheme.typography.bodySmall)
        }

        if (status.isNotEmpty()) {
            Spacer(Modifier.height(12.dp))
            Text(status, style = MaterialTheme.typography.bodyMedium)
        }

        // ===== 有线导入（放在主页按钮上方） =====
        Spacer(Modifier.height(20.dp))
        Button(
            onClick = { doUsbImport() },
            enabled = !syncing,
            modifier = Modifier.fillMaxWidth()
        ) { Text("有线导入") }
        Spacer(Modifier.height(4.dp))
        Text(
            "用 USB 数据线连接电脑，并在手机「设置→连接→USB 网络共享」中开启（手机当热点），" +
                    "App 会自动在本机 USB 子网里找到电脑并导入全部图纸。",
            style = MaterialTheme.typography.bodySmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant
        )

        // ===== 主页 =====
        Spacer(Modifier.height(20.dp))
        OutlinedButton(onClick = { nav.navigate("list") }, modifier = Modifier.fillMaxWidth()) {
            Text("主页")
        }

        // ===== 传输日志 =====
        Spacer(Modifier.height(16.dp))
        Text("传输日志", style = MaterialTheme.typography.labelMedium)
        Spacer(Modifier.height(6.dp))
        val logScroll = rememberScrollState()
        LaunchedEffect(logs.size) {
            logScroll.scrollTo(logScroll.maxValue)
        }
        Box(
            modifier = Modifier
                .fillMaxWidth()
                .height(200.dp)
                .verticalScroll(logScroll)
                .background(MaterialTheme.colorScheme.surfaceVariant, MaterialTheme.shapes.small)
                .padding(10.dp)
        ) {
            Text(
                if (logs.isEmpty()) "（导入后，这里显示传输详情、跳过与重连情况）"
                else logs.joinToString("\n"),
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
        }
        }
    }
}

// 字节数格式化为 KB / MB
private fun formatSize(bytes: Long): String {
    if (bytes <= 0) return ""
    val kb = bytes / 1024.0
    return if (kb < 1024) "%.1f KB".format(Locale.US, kb)
    else "%.2f MB".format(Locale.US, kb / 1024.0)
}

// 历史连接以 JSON 数组持久化到 SharedPreferences，最多保留 10 条、最近优先
private fun readHosts(prefs: android.content.SharedPreferences): List<String> {
    return try {
        val arr = org.json.JSONArray(prefs.getString("hosts", "[]"))
        val out = mutableListOf<String>()
        for (i in 0 until arr.length()) out.add(arr.getString(i))
        out
    } catch (e: Exception) {
        emptyList()
    }
}

private fun writeHosts(list: List<String>): String {
    val arr = org.json.JSONArray()
    list.forEach { arr.put(it) }
    return arr.toString()
}
