package com.houyawei.nddrawing.util

import android.content.Context
import android.content.Intent
import android.net.Uri
import android.os.Build
import androidx.core.content.FileProvider
import com.houyawei.nddrawing.BuildConfig
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import org.json.JSONObject
import java.io.File
import java.io.FileOutputStream
import java.net.HttpURLConnection
import java.net.URL
import java.nio.charset.StandardCharsets

/**
 * 手机 APP 检查更新：从「设置 → 数据同步」里配置的电脑端 IP 拉取 PC 服务器上的 APK。
 * 服务器地址就是局域网地址，因此只有手机与电脑在同一局域网时才可用。
 */
data class AppUpdateInfo(
    val version: String,
    val file: String,
    val size: Long
)

object AppUpdate {
    private const val INFO_PATH = "/api/update/android/info"
    private const val PACKAGE_PATH = "/api/update/android/package"

    sealed class CheckResult {
        object NoServer : CheckResult()
        object NoPackage : CheckResult()
        data class Error(val message: String) : CheckResult()
        data class Ready(val info: AppUpdateInfo) : CheckResult()
    }

    private fun baseUrl(ctx: Context): String? {
        val p = ctx.getSharedPreferences("sync", Context.MODE_PRIVATE)
        val host = (p.getString("host", "") ?: "").trim()
        val port = (p.getString("port", "8080") ?: "8080").trim().ifEmpty { "8080" }
        return if (host.isEmpty()) null else "http://$host:$port"
    }

    suspend fun check(ctx: Context): CheckResult = withContext(Dispatchers.IO) {
        val base = baseUrl(ctx) ?: return@withContext CheckResult.NoServer
        var conn: HttpURLConnection? = null
        try {
            conn = (URL(base + INFO_PATH).openConnection() as HttpURLConnection).apply {
                connectTimeout = 5000
                readTimeout = 8000
                setRequestProperty("Connection", "close")
            }
            if (conn.responseCode != 200)
                return@withContext CheckResult.Error("HTTP ${conn.responseCode}")
            val body = conn.inputStream.bufferedReader(StandardCharsets.UTF_8).use { it.readText() }
            val o = JSONObject(body)
            if (!o.optBoolean("available", false))
                return@withContext CheckResult.NoPackage
            CheckResult.Ready(
                AppUpdateInfo(
                    version = o.optString("version"),
                    file = o.optString("file"),
                    size = o.optLong("size")
                )
            )
        } catch (e: Exception) {
            CheckResult.Error(e.message ?: "无法连接服务器")
        } finally {
            conn?.disconnect()
        }
    }

    /** 下载 APK 到 cacheDir/update（已被 FileProvider 的 cache-path 覆盖），回调下载百分比 */
    suspend fun download(ctx: Context, info: AppUpdateInfo, onProgress: (Int) -> Unit): File =
        withContext(Dispatchers.IO) {
            val base = baseUrl(ctx) ?: throw IllegalStateException("未配置服务器地址")
            val dir = File(ctx.cacheDir, "update").apply { mkdirs() }
            val outFile = File(dir, info.file.ifEmpty { "ND_DrawingManagement.apk" })
            if (outFile.exists()) outFile.delete()
            var conn: HttpURLConnection? = null
            try {
                conn = (URL(base + PACKAGE_PATH).openConnection() as HttpURLConnection).apply {
                    connectTimeout = 8000
                    readTimeout = 60000
                    setRequestProperty("Connection", "close")
                }
                if (conn.responseCode != 200)
                    throw IllegalStateException("HTTP ${conn.responseCode}")
                val total = conn.contentLength.toLong()
                conn.inputStream.use { input ->
                    FileOutputStream(outFile).use { out ->
                        val buf = ByteArray(64 * 1024)
                        var done = 0L
                        while (true) {
                            val n = input.read(buf)
                            if (n <= 0) break
                            out.write(buf, 0, n)
                            done += n
                            if (total > 0) {
                                val percent = (done * 100 / total).toInt()
                                withContext(Dispatchers.Main) { onProgress(percent) }
                            }
                        }
                    }
                }
                if (outFile.length() <= 0) throw IllegalStateException("下载内容为空")
                outFile
            } finally {
                conn?.disconnect()
            }
        }

    /** 调用系统安装器；Android 8+ 需要 REQUEST_INSTALL_PACKAGES 权限 */
    fun install(ctx: Context, file: File) {
        val uri: Uri = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.N) {
            FileProvider.getUriForFile(ctx, ctx.packageName + ".fileprovider", file)
        } else {
            Uri.fromFile(file)
        }
        val intent = Intent(Intent.ACTION_VIEW).apply {
            setDataAndType(uri, "application/vnd.android.package-archive")
            addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
            addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
        }
        ctx.startActivity(intent)
    }

    /** 版本号都是 yyyy-MM-dd，可直接按字典序比较 */
    fun hasNewer(serverVersion: String): Boolean =
        serverVersion.isNotBlank() && serverVersion > BuildConfig.BUILD_DATE
}
