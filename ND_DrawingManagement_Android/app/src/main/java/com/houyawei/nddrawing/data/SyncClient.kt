package com.houyawei.nddrawing.data

import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import org.json.JSONArray
import org.json.JSONObject
import java.io.BufferedInputStream
import java.io.File
import java.io.FileOutputStream
import java.net.HttpURLConnection
import java.net.URL
import java.nio.charset.StandardCharsets

// 通过 PC 端 LanHttpServer 的 /api/sync/* 接口拉取数据
class SyncClient(private val baseUrl: String) {

    suspend fun fetchSchema(): Int = withContext(Dispatchers.IO) {
        JSONObject(get("/api/sync/schema")).optInt("schema", 0)
    }

    // 同步握手：上报本机设备名，供 PC 设置页「离线同步」显示
    suspend fun postHello(device: String) = withContext(Dispatchers.IO) {
        try { get("/api/sync/hello?device=${enc(device)}") } catch (_: Exception) {}
    }

    suspend fun fetchProducts(since: String): List<Product> = withContext(Dispatchers.IO) {
        val path = if (since.isEmpty()) "/api/sync/products"
        else "/api/sync/products?since=${enc(since)}"
        val arr = JSONObject(get(path)).getJSONArray("items")
        parseProducts(arr)
    }

    suspend fun fetchDrawings(since: String): List<Drawing> = withContext(Dispatchers.IO) {
        val path = if (since.isEmpty()) "/api/sync/drawings"
        else "/api/sync/drawings?since=${enc(since)}"
        val arr = JSONObject(get(path)).getJSONArray("items")
        parseDrawings(arr)
    }

    // 下载结果：已存在且大小一致则 Skipped；实际下载为 Downloaded；多次重试仍失败为 Failed
    enum class DownloadResult { Downloaded, Skipped, Failed }

    // 单张图纸下载的返回结果（含重试次数、实际大小、期望大小、失败原因，供 UI 打日志）
    data class FileResult(
        val result: DownloadResult,
        val local: String,
        val attempts: Int,
        val bytes: Long,
        val expected: Long = 0,
        val error: String = ""
    )

    // 下载图纸到 baseDir 下，按服务端下发的 relPath（分类/型号/图纸名）还原目录结构，
    // 与电脑端 storageRoot/分类/型号/图纸名 完全一致。先 HEAD 询问大小，大小一致则跳过。
    // 下载失败（如连接被重置）会重试最多 3 次；全部失败返回 Failed，由调用方跳过、下次同步续传。
    // onProgress(0~100) 在每个 20% 里程碑（含 0 与 100）回调一次，供 UI 显示单文件进度。
    suspend fun downloadFile(
        relPath: String,
        baseDir: File,
        index: Int,
        total: Int,
        onProgress: (Int) -> Unit = {}
    ): FileResult = withContext(Dispatchers.IO) {
        val local = localDrawingFile(baseDir, relPath)
        val url = "$baseUrl/api/sync/file?path=${enc(relPath)}&i=$index&total=$total"
        // 数据库小，每次都同步；图纸大，做存在+大小检查避免重复下载
        val expected = headSize(url)
        val hasLocal = local.exists() && local.length() > 0
        if (hasLocal && (expected <= 0 || local.length() == expected)) {
            return@withContext FileResult(DownloadResult.Skipped, local.absolutePath, 1, local.length(), expected)
        }
        if (local.exists()) local.delete() // 大小不一致，重新下载
        var lastErr: Throwable? = null
        repeat(3) { attempt ->
            try {
                local.parentFile?.mkdirs()
                val conn = URL(url).openConnection() as HttpURLConnection
                conn.connectTimeout = 15000
                conn.readTimeout = 60000
                conn.setRequestProperty("Connection", "close")
                val code = conn.responseCode
                if (code != 200) throw RuntimeException("下载失败 HTTP $code : $relPath")
                val buffer = ByteArray(32 * 1024)
                var read: Int
                var totalRead = 0L
                var lastStep = 0
                onProgress(0)
                conn.inputStream.use { input ->
                    BufferedInputStream(input).use { bis ->
                        FileOutputStream(local).use { fos ->
                            while (bis.read(buffer).also { read = it } != -1) {
                                fos.write(buffer, 0, read)
                                totalRead += read
                                if (expected > 0) {
                                    val pct = (totalRead * 100 / expected).toInt()
                                    val step = (pct / 20) * 20
                                    if (step > lastStep && step < 100) {
                                        lastStep = step
                                        onProgress(step)
                                    }
                                }
                            }
                        }
                    }
                }
                onProgress(100)
                // 若已知期望大小，校验完整性
                if (expected > 0 && local.length() != expected) {
                    local.delete()
                    throw RuntimeException("文件大小校验失败：$relPath")
                }
                return@withContext FileResult(DownloadResult.Downloaded, local.absolutePath, attempt + 1, local.length(), expected)
            } catch (e: Throwable) {
                lastErr = e
                local.delete() // 删除可能不完整的文件，避免下次误判为已下载
                try { Thread.sleep(1000) } catch (_: InterruptedException) {}
            }
        }
        FileResult(DownloadResult.Failed, "", 3, 0, expected, lastErr?.message ?: "未知错误")
    }

    // 短超时探测服务器是否可达：手机不在同一局域网 / PC 未开服务时，
    // 详情页要立刻放弃补图，避免逐张连接超时把页面拖住
    suspend fun probe(): Boolean = withContext(Dispatchers.IO) {
        try {
            val c = URL("$baseUrl/api/sync/schema").openConnection() as HttpURLConnection
            c.connectTimeout = 2000
            c.readTimeout = 2000
            c.setRequestProperty("Connection", "close")
            val ok = c.responseCode == 200
            c.disconnect()
            ok
        } catch (_: Exception) {
            false
        }
    }

    // 拉取产品分类（与 PC DatabaseManager::categories 一致）
    suspend fun fetchCategories(): List<String> = withContext(Dispatchers.IO) {
        val obj = JSONObject(get("/api/sync/categories"))
        val arr = obj.optJSONArray("categories") ?: org.json.JSONArray()
        val out = mutableListOf<String>()
        for (i in 0 until arr.length()) out.add(arr.optString(i))
        out
    }

    // 拉取各分类特有字段配置（field_key → 中文标签、显示顺序），与 PC/网页端一致。
    // 老版本 PC 端没有这个接口，失败时返回空列表，由详情页用内置标签兜底，不影响主同步流程。
    suspend fun fetchFieldConfigs(): List<FieldConfig> = withContext(Dispatchers.IO) {
        try {
            val arr = JSONObject(get("/api/sync/fields")).optJSONArray("fields") ?: JSONArray()
            val out = mutableListOf<FieldConfig>()
            for (i in 0 until arr.length()) {
                val o = arr.optJSONObject(i) ?: continue
                out.add(
                    FieldConfig(
                        category = o.optString("category"),
                        key = o.optString("key"),
                        label = o.optString("label"),
                        sort = o.optInt("sort", 0)
                    )
                )
            }
            out
        } catch (e: Exception) {
            emptyList()
        }
    }

    // HEAD 请求获取文件大小（供断点判断）；失败返回 -1
    private fun headSize(urlStr: String): Long {
        return try {
            val c = URL(urlStr).openConnection() as HttpURLConnection
            c.requestMethod = "HEAD"
            c.connectTimeout = 15000
            c.readTimeout = 15000
            c.setRequestProperty("Connection", "close")
            c.connect()
            val len = c.contentLengthLong
            c.disconnect()
            len
        } catch (e: Exception) {
            -1L
        }
    }

    private fun get(path: String): String {
        val conn = URL(baseUrl + path).openConnection() as HttpURLConnection
        conn.connectTimeout = 15000
        conn.readTimeout = 30000
        conn.setRequestProperty("Connection", "close") // 强制每次新连接，绕开 QHttpServer 持久连接缺陷
        try {
            val code = conn.responseCode
            if (code != 200) throw RuntimeException("HTTP $code : $path")
            return conn.inputStream.bufferedReader(StandardCharsets.UTF_8).use { it.readText() }
        } finally {
            conn.disconnect()
        }
    }

    private fun enc(s: String): String = try {
        java.net.URLEncoder.encode(s, "UTF-8")
    } catch (e: Exception) { s }

}

// 与 PC 端 apiSyncProducts 返回的 JSON 字段一一对应，局域网同步与 USB 导入共用
internal fun parseProducts(arr: JSONArray): List<Product> {
    val out = mutableListOf<Product>()
    for (i in 0 until arr.length()) {
        val o = arr.getJSONObject(i)
        val ext = mutableMapOf<String, String>()
        o.optJSONObject("ext")?.let { ej ->
            val keys = ej.keys()
            while (keys.hasNext()) {
                val k = keys.next()
                ext[k] = ej.optString(k)
            }
        }
        // 硬件/软件版本号现在由 PC 端作为控制器“可配置特有字段”存入 ext（field_key=hw_version/sw_version），
        // 这里优先取 ext，缺省时回退到通用列（兼容历史数据）
        val hw = (ext["hw_version"]?.takeIf { it.isNotBlank() }) ?: o.optString("hw_version")
        val sw = (ext["sw_version"]?.takeIf { it.isNotBlank() }) ?: o.optString("sw_version")
        out.add(
            Product(
                id = o.optInt("id", 0),
                category = o.optString("category"),
                materialCode = o.optString("material_code"),
                name = o.optString("name"),
                model = o.optString("model"),
                hwVersion = hw,
                swVersion = sw,
                remark = o.optString("remark"),
                creator = o.optString("creator"),
                createdAt = o.optString("created_at"),
                updatedAt = o.optString("updated_at"),
                ext = ext
            )
        )
    }
    return out
}

// 与 PC 端 apiSyncDrawings 返回的 JSON 字段一一对应
internal fun parseDrawings(arr: JSONArray): List<Drawing> {
    val out = mutableListOf<Drawing>()
    for (i in 0 until arr.length()) {
        val o = arr.getJSONObject(i)
        out.add(
            Drawing(
                id = o.optInt("id", 0),
                productId = o.optInt("product_id", 0),
                dtype = o.optInt("dtype", 0),
                format = o.optString("format"),
                drawingDate = o.optString("drawing_date"),
                createdAt = o.optString("created_at"),
                relPath = o.optString("rel_path"),
                fileName = o.optString("file_name")
            )
        )
    }
    return out
}

// 服务端相对路径（分类/名称 型号/图纸名）→ 本机「南都图纸」目录下的文件。
// 下载与详情页预览共用同一套映射，保证落到图纸管理页能扫描/删除的位置。
fun localDrawingFile(baseDir: File, relPath: String): File {
    val segs = relPath.replace('\\', '/').split('/').filter { it.isNotEmpty() }
    return File(baseDir, segs.map { safeDrawingSegment(it) }.joinToString("/"))
}

// 与电脑端 safeFileName 一致的净化规则：剔除 / \ : * ? " < > |
private fun safeDrawingSegment(s: String): String {
    val r = s.map { c -> if (c in "/\\:*?\"<>|") '_' else c }.joinToString("")
    val t = r.trim()
    return if (t.isEmpty()) "未命名" else t
}
