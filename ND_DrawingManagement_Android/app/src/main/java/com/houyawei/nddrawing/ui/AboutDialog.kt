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
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties
import com.houyawei.nddrawing.BuildConfig
import com.houyawei.nddrawing.R

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
 * 版本号改为构建日期（Gradle 编译时自动生成），「环境」改为「技术」并列出主要组件与版本。
 * 点击遮罩空白处关闭；点卡片内部不关闭。
 */
@Composable
fun AboutDialog(onDismiss: () -> Unit) {
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
                    Button(
                        onClick = onDismiss,
                        modifier = Modifier.width(120.dp)
                    ) { Text("确 定", fontSize = 14.sp) }
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
