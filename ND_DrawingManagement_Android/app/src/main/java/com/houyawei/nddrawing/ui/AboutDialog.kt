package com.houyawei.nddrawing.ui

import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties
import com.houyawei.nddrawing.BuildConfig
import com.houyawei.nddrawing.R
import com.houyawei.nddrawing.util.AppUpdate
import kotlinx.coroutines.launch
import java.io.File

// APP 端技术栈：与 app/build.gradle.kts、AndroidManifest.xml 保持一致
private val APP_TECH = listOf(
    "系统：Android 14（compileSdk / targetSdk 34），最低 Android 7.0（minSdk 24）",
    "开发：Kotlin 1.9.22 · JDK 17 · AGP 8.3.0 · Gradle 8.4",
    "界面：Jetpack Compose Material3（BOM 2024.02.00）+ Navigation Compose 2.7.7",
    "二维码：CameraX 1.3.4 取景 + ZXing 3.5.3 解码（纯 Java，无原生库）",
    "NDK / ABI：未使用原生代码，仅打包 arm64-v8a",
    "数据：原生 SQLiteOpenHelper + HttpURLConnection 局域网同步",
    "图纸：按「分类 / 型号 / 文件名」存入 Download/南都图纸"
).joinToString("\n") { "· $it" }

/**
 * 关于弹窗：透明黑底遮罩 + 居中深色卡片。
 * 版本号为构建日期（Gradle 编译时自动生成），「关于」页新增「检查更新」：
 * 手机与电脑处于同一局域网时，从电脑端服务器下载新版本 APK 并安装。
 * 点击遮罩空白处关闭；点卡片内部不关闭。
 */
@Composable
fun AboutDialog(onDismiss: () -> Unit) {
    val ctx = LocalContext.current
    val scope = rememberCoroutineScope()
    var status by remember { mutableStateOf("") }
    var busy by remember { mutableStateOf(false) }
    var percent by remember { mutableStateOf(-1) }
    var apkFile by remember { mutableStateOf<File?>(null) }

    fun startCheck() {
        if (busy) return
        busy = true
        status = "正在检查更新…"
        percent = -1
        apkFile = null
        scope.launch {
            when (val r = AppUpdate.check(ctx)) {
                is AppUpdate.CheckResult.NoServer ->
                    status = "未配置服务器地址。请先在「设置 → 数据同步」填写电脑端 IP，并连接同一局域网。"
                is AppUpdate.CheckResult.NoPackage ->
                    status = "服务器上还没有手机安装包，请先在电脑端「设置 → 版本管理」上传 APK。"
                is AppUpdate.CheckResult.Error ->
                    status = "检查失败：${r.message}\n请确认手机与电脑处于同一局域网。"
                is AppUpdate.CheckResult.Ready -> {
                    if (!AppUpdate.hasNewer(r.info.version)) {
                        status = "当前已是最新版本（${BuildConfig.BUILD_DATE}）。"
                    } else {
                        status = "发现新版本 ${r.info.version}，正在下载…"
                        percent = 0
                        try {
                            val f = AppUpdate.download(ctx, r.info) { p -> percent = p }
                            apkFile = f
                            percent = 100
                            status = "下载完成（${r.info.version}），点「立即安装」按系统提示完成安装。"
                        } catch (e: Exception) {
                            status = "下载失败：${e.message}"
                        }
                    }
                }
            }
            busy = false
        }
    }

    Dialog(
        onDismissRequest = onDismiss,
        properties = DialogProperties(usePlatformDefaultWidth = false)
    ) {
        Box(
            Modifier
                .fillMaxSize()
                .background(Color.Black.copy(alpha = 0.55f))
                .clickable(onClick = onDismiss),
            contentAlignment = Alignment.Center
        ) {
            Card(
                Modifier
                    .widthIn(min = 300.dp, max = 360.dp)
                    .heightIn(max = 560.dp)
                    .clickable { /* 消费点击，避免点到卡片关闭弹窗 */ },
                shape = RoundedCornerShape(12.dp),
                colors = CardDefaults.cardColors(containerColor = Color(0xFF223040))
            ) {
                Column(
                    Modifier
                        .padding(20.dp)
                        .verticalScroll(rememberScrollState()),
                    horizontalAlignment = Alignment.CenterHorizontally
                ) {
                    Image(
                        painter = painterResource(R.drawable.logo),
                        contentDescription = "logo",
                        modifier = Modifier.size(52.dp)
                    )
                    Spacer(Modifier.height(8.dp))
                    Text(
                        "南都图纸管理系统",
                        color = Color.White,
                        fontWeight = FontWeight.Bold,
                        fontSize = 18.sp
                    )
                    Spacer(Modifier.height(12.dp))
                    Text(
                        "河南南都汽车电子有限公司\n" +
                                "河南省南阳市卧龙区天佑产业园24号楼2楼/3楼/4楼\n\n" +
                                "开发：侯亚威　18337356560\n" +
                                "构建日期：${BuildConfig.BUILD_DATE}",
                        color = Color(0xFFC8D2DC),
                        fontSize = 13.sp,
                        lineHeight = 20.sp,
                        textAlign = TextAlign.Center
                    )
                    Spacer(Modifier.height(12.dp))
                    HorizontalDivider(color = Color(0xFF4A5468))
                    Spacer(Modifier.height(10.dp))
                    Text(
                        "技术",
                        color = Color(0xFF43CF7C),
                        fontWeight = FontWeight.Bold,
                        fontSize = 15.sp
                    )
                    Spacer(Modifier.height(8.dp))
                    Text(
                        APP_TECH,
                        color = Color(0xFFC8D2DC),
                        fontSize = 12.sp,
                        lineHeight = 18.sp,
                        textAlign = TextAlign.Start,
                        modifier = Modifier.fillMaxWidth()
                    )
                    Spacer(Modifier.height(16.dp))
                    HorizontalDivider(color = Color(0xFF4A5468))
                    Spacer(Modifier.height(10.dp))
                    Text(
                        "版本更新",
                        color = Color(0xFF43CF7C),
                        fontWeight = FontWeight.Bold,
                        fontSize = 15.sp
                    )
                    Spacer(Modifier.height(6.dp))
                    Text(
                        "仅在与电脑处于同一局域网时可用：检查服务器上的新版 APK，下载后直接安装。",
                        color = Color(0xFFC8D2DC),
                        fontSize = 12.sp,
                        lineHeight = 18.sp,
                        textAlign = TextAlign.Center
                    )
                    Spacer(Modifier.height(8.dp))
                    if (status.isNotEmpty()) {
                        Text(
                            status,
                            color = Color(0xFFE6EDF5),
                            fontSize = 12.sp,
                            lineHeight = 18.sp,
                            textAlign = TextAlign.Center
                        )
                        Spacer(Modifier.height(6.dp))
                    }
                    if (percent in 0..99) {
                        Text(
                            "下载进度：$percent%",
                            color = Color(0xFF43CF7C),
                            fontSize = 13.sp
                        )
                        Spacer(Modifier.height(6.dp))
                    }
                    apkFile?.let { f ->
                        Button(
                            onClick = { AppUpdate.install(ctx, f) },
                            modifier = Modifier.fillMaxWidth()
                        ) { Text("立即安装", fontSize = 14.sp) }
                        Spacer(Modifier.height(8.dp))
                    }
                    Row(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                        Button(
                            onClick = { startCheck() },
                            enabled = !busy,
                            modifier = Modifier.weight(1f)
                        ) { Text("检查更新", fontSize = 14.sp) }
                        Button(
                            onClick = onDismiss,
                            modifier = Modifier.weight(1f)
                        ) { Text("确 定", fontSize = 14.sp) }
                    }
                }
            }
        }
    }
}

/**
 * 分享方式选择：透明黑底弹窗，提供「文字」「图片」两种方式。
 */
@Composable
fun ShareChoiceDialog(
    onText: () -> Unit,
    onImage: () -> Unit,
    onDismiss: () -> Unit
) {
    Dialog(
        onDismissRequest = onDismiss,
        properties = DialogProperties(usePlatformDefaultWidth = false)
    ) {
        Box(
            Modifier
                .fillMaxSize()
                .background(Color.Black.copy(alpha = 0.55f))
                .clickable(onClick = onDismiss),
            contentAlignment = Alignment.Center
        ) {
            Card(
                Modifier
                    .widthIn(max = 320.dp)
                    .clickable { },
                shape = RoundedCornerShape(12.dp),
                colors = CardDefaults.cardColors(containerColor = Color(0xFF223040))
            ) {
                Column(
                    Modifier.padding(24.dp),
                    horizontalAlignment = Alignment.CenterHorizontally
                ) {
                    Text("选择分享方式", color = Color.White, fontWeight = FontWeight.Bold, fontSize = 18.sp)
                    Spacer(Modifier.height(16.dp))
                    Button(onClick = onText, Modifier.fillMaxWidth()) { Text("文字", fontSize = 14.sp) }
                    Spacer(Modifier.height(10.dp))
                    Button(onClick = onImage, Modifier.fillMaxWidth()) { Text("图片", fontSize = 14.sp) }
                }
            }
        }
    }
}
