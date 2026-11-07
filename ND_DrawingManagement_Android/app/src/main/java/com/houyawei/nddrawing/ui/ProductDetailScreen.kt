package com.houyawei.nddrawing.ui

import androidx.compose.foundation.Image
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.navigation.NavHostController
import com.houyawei.nddrawing.data.Drawing
import com.houyawei.nddrawing.data.Product
import com.houyawei.nddrawing.data.Repository

@Composable
fun ProductDetailScreen(id: Int, nav: NavHostController) {
    val product = remember { Repository.db.productById(id) }
    val drawings = remember { Repository.db.drawingsOf(id) }
    val ctx = LocalContext.current
    var showAbout by remember { mutableStateOf(false) }
    var showShare by remember { mutableStateOf(false) }

    if (product == null) {
        Box(Modifier.fillMaxSize().statusBarsPadding().navigationBarsPadding(), contentAlignment = Alignment.Center) {
            Text("未找到该产品")
        }
        return
    }

    // 特有字段按分类配置显示（中文标签/顺序）；硬件/软件版本号已归入其中，不再单独显示固定行
    val extFields = remember(product.id) { displayedExtFields(product) }

    // 整页文字内容（用于「文字」分享）
    val detailText = buildString {
        appendLine("型号：${product.model}")
        appendLine("名称：${product.name}")
        appendLine("物料编码：${product.materialCode}")
        appendLine("备注：${product.remark}")
        appendLine("创建人：${product.creator}")
        appendLine("分类：${product.category}")
        if (extFields.isNotEmpty()) {
            appendLine("扩展字段：")
            extFields.forEach { (k, v) -> appendLine("  $k：$v") }
        }
        if (drawings.isNotEmpty()) {
            appendLine("图纸（${drawings.size}）：")
            drawings.forEach { d ->
                val type = if (d.dtype == 1) "会签图" else "下发图"
                appendLine("  $type ${d.drawingDate} ${d.fileName}")
            }
        }
    }

    Column(Modifier.fillMaxSize().navigationBarsPadding()) {
        AppMenuBar(current = "list", nav = nav, onAbout = { showAbout = true })
        if (showAbout) AboutDialog(onDismiss = { showAbout = false })

        Column(
            Modifier
                .fillMaxSize()
                .verticalScroll(rememberScrollState())
                .statusBarsPadding()
                .padding(16.dp)
        ) {
            Text(product.model, style = MaterialTheme.typography.headlineSmall)
            Spacer(Modifier.height(4.dp))
            Text(product.name, style = MaterialTheme.typography.titleMedium)
            Spacer(Modifier.height(8.dp))
            InfoRow("物料编码", product.materialCode)
            InfoRow("备注", product.remark)
            InfoRow("创建人", product.creator)
            InfoRow("分类", product.category)

            if (extFields.isNotEmpty()) {
                Spacer(Modifier.height(12.dp))
                Text("扩展字段", style = MaterialTheme.typography.titleMedium)
                extFields.forEach { (label, value) -> InfoRow(label, value) }
            }

            Spacer(Modifier.height(16.dp))
            Text("图纸（${drawings.size}）", style = MaterialTheme.typography.titleMedium)
            Spacer(Modifier.height(8.dp))
            drawings.forEach { d -> DrawingItem(d) }

            Spacer(Modifier.height(16.dp))
            Button(
                onClick = { showShare = true },
                modifier = Modifier.fillMaxWidth()
            ) { Text("分享", fontSize = 14.sp) }
        }
    }

    if (showShare) {
        ShareChoiceDialog(
            onText = {
                showShare = false
                shareText(ctx, detailText)
            },
            onImage = {
                showShare = false
                // 图片分享：一次性分享该产品所有图纸图片；无图片图纸时兜底分享文字
                val imgs = drawings.filter { !it.format.equals("pdf", true) && it.localPath.isNotEmpty() }
                if (imgs.isNotEmpty()) shareImageFiles(ctx, imgs.map { it.localPath }) else shareText(ctx, detailText)
            },
            onDismiss = { showShare = false }
        )
    }
}

@Composable
private fun InfoRow(label: String, value: String) {
    Row(Modifier.fillMaxWidth().padding(vertical = 2.dp)) {
        Text("$label：", style = MaterialTheme.typography.bodyMedium)
        Text(value, style = MaterialTheme.typography.bodyMedium)
    }
}

@Composable
private fun DrawingItem(d: Drawing) {
    val ctx = LocalContext.current
    val label = (if (d.dtype == 1) "会签图" else "下发图") +
            if (d.drawingDate.isNotEmpty()) "  ${d.drawingDate}" else ""
    var showViewer by remember { mutableStateOf(false) }
    Card(Modifier.fillMaxWidth().padding(vertical = 4.dp)) {
        Column(Modifier.fillMaxWidth().padding(12.dp)) {
            Text(label, style = MaterialTheme.typography.titleSmall)
            Spacer(Modifier.height(4.dp))
            if (d.format.equals("pdf", true)) {
                OutlinedButton(onClick = { openPdf(ctx, d.localPath) }) {
                    Text("打开 PDF：${d.fileName}")
                }
            } else {
                val bmp = remember(d.localPath) { loadBitmap(d.localPath) }
                if (bmp != null) {
                    Image(
                        bmp, contentDescription = d.fileName,
                        modifier = Modifier.fillMaxWidth().clickable { showViewer = true },
                        contentScale = ContentScale.FillWidth
                    )
                    Spacer(Modifier.height(4.dp))
                    Text(
                        "点击图片可放大查看，或用手机相册打开",
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.primary
                    )
                } else {
                    Text(
                        "（图片未下载或文件缺失：${d.fileName}）",
                        style = MaterialTheme.typography.bodySmall
                    )
                }
            }
        }
    }
    if (showViewer) {
        ImageViewerDialog(path = d.localPath, onClose = { showViewer = false })
    }
}
