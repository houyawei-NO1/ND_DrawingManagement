package com.houyawei.nddrawing.ui

import android.content.Context
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.BasicTextField
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties
import androidx.navigation.NavHostController
import com.houyawei.nddrawing.R
import com.houyawei.nddrawing.data.Product
import com.houyawei.nddrawing.data.QtyLog
import com.houyawei.nddrawing.util.DEFAULT_SERVER_HOST
import com.houyawei.nddrawing.util.DEFAULT_SERVER_PORT
import com.houyawei.nddrawing.data.Repository
import com.houyawei.nddrawing.data.SyncClient
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.text.SimpleDateFormat
import java.util.Calendar
import java.util.Locale
import kotlin.math.roundToInt

// ===== 与 PC / 网页端一致的配色 =====
private val StockGreen = Color(0xFF43CF7C)
private val StockBlue = Color(0xFF5AA9FF)
private val StockOrange = Color(0xFFE5C07B)
private val StockPurple = Color(0xFFC79BFF)
private val StockRed = Color(0xFFFF9AA3)
private val StockCard = Color(0xFF22293C)
private val StockDim = Color(0xFF9AA6BC)

private data class StockRow(
    val p: Product,
    val qty: Int,
    val dayUse: Int,
    val weekUse: Int,
    val monthUse: Int,
    val yearUse: Int,
    val rank: Int,
    val hasLogs: Boolean
)

private data class Usage(val date: String, val used: Int, val inc: Int)

// 与 PC 端 apiStock / 网页端 usageOf 完全一致：
// 按日期升序，用量 = 上一条结存 − 当日结存（数量上升按 0，记入库）。
// 首条记录口径：按记录日期 2026-11-01 起，第一次录入的结存视为「从 0 入库」；
// 此前的第一次录入属于建立库存（期初建账），既不算用量也不算入库。
private const val INBOUND_FROM = "2026-11-01"

private fun usageOf(logs: List<QtyLog>): List<Usage> {
    val sorted = logs.filter { it.recordDate.isNotEmpty() }.sortedBy { it.recordDate }
    val out = ArrayList<Usage>(maxOf(0, sorted.size - 1) + 1)
    if (sorted.isNotEmpty()) {
        val first = sorted.first()
        if (first.qty > 0 && first.recordDate >= INBOUND_FROM)
            out.add(Usage(first.recordDate, 0, first.qty))
    }
    for (i in 1 until sorted.size) {
        val d = sorted[i - 1].qty - sorted[i].qty
        out.add(Usage(sorted[i].recordDate, maxOf(0, d), maxOf(0, -d)))
    }
    return out
}

private fun todayStr(): String =
    SimpleDateFormat("yyyy-MM-dd", Locale.US).format(Calendar.getInstance().time)

private fun daysAgoStr(n: Int): String {
    val c = Calendar.getInstance()
    c.add(Calendar.DAY_OF_YEAR, -n)
    return SimpleDateFormat("yyyy-MM-dd", Locale.US).format(c.time)
}

private fun sumUse(u: List<Usage>, pred: (String) -> Boolean): Int =
    u.sumOf { if (pred(it.date)) it.used else 0 }

private fun buildRows(products: List<Product>, allLogs: List<QtyLog>): List<StockRow> {
    val byProduct = allLogs.groupBy { it.productId }
    val today = todayStr()
    val week = daysAgoStr(6)
    val month = today.take(7)
    val year = today.take(4)
    return products.map { p ->
        val logs = byProduct[p.id].orEmpty()
        val u = usageOf(logs)
        val qty = if (logs.isEmpty()) p.qty
                  else logs.maxByOrNull { it.recordDate }?.qty ?: p.qty
        val rank = when {
            p.minQty == 0 && p.maxQty == 0 -> if (qty > 0) 3 else 4
            qty < p.minQty -> 0
            qty > p.maxQty -> 1
            else -> 2
        }
        StockRow(
            p = p, qty = qty,
            dayUse = sumUse(u) { it == today },
            weekUse = sumUse(u) { it >= week },
            monthUse = sumUse(u) { it.take(7) == month },
            yearUse = sumUse(u) { it.take(4) == year },
            rank = rank,
            hasLogs = logs.isNotEmpty()
        )
    }
}

private val SORT_KEYS: List<Pair<String, (StockRow) -> Int>> = listOf(
    "最新数量" to { it.qty },
    "今日用量" to { it.dayUse },
    "周用量" to { it.weekUse },
    "月用量" to { it.monthUse },
    "年用量" to { it.yearUse }
)

private fun rowColor(rank: Int): Color = when (rank) {
    0 -> Color(0x2EE06C75)
    1 -> Color(0x2EE5C07B)
    2 -> Color(0x2443CF7C)
    else -> Color(0x0DFFFFFF)
}

// ===================== 库存列表（主界面之后的第二个页面） =====================
@Composable
fun StockScreen(nav: NavHostController) {
    val ctx = LocalContext.current
    val scope = rememberCoroutineScope()
    var kw by remember { mutableStateOf("") }
    var products by remember { mutableStateOf(emptyList<Product>()) }
    var logs by remember { mutableStateOf(emptyList<QtyLog>()) }
    var sortIndex by remember { mutableIntStateOf(-1) } // -1 = 规则排序
    var sortAsc by remember { mutableStateOf(true) }
    var syncing by remember { mutableStateOf(false) }
    var syncMsg by remember { mutableStateOf("") }
    var showAbout by remember { mutableStateOf(false) }
    // 点击没有数量记录的产品时弹出提示，而不是进入空白详情页
    var noDataModel by remember { mutableStateOf<String?>(null) }

    suspend fun reload() {
        val p = withContext(Dispatchers.IO) { Repository.db.allProducts("", "") }
        val l = withContext(Dispatchers.IO) { Repository.db.allQtyLogs() }
        products = p
        logs = l
    }

    // 无线同步：只拉产品库存三值与每日数量日志，不请求图纸清单、不下载任何图纸文件。
    // silent = 进入页面时自动执行，连不上电脑也不打扰，直接展示上一次的本地数据。
    suspend fun syncStock(silent: Boolean) {
        if (syncing) return
        syncing = true
        try {
            val prefs = ctx.getSharedPreferences("sync", Context.MODE_PRIVATE)
            val host = prefs.getString("host", DEFAULT_SERVER_HOST) ?: DEFAULT_SERVER_HOST
            val port = prefs.getString("port", DEFAULT_SERVER_PORT) ?: DEFAULT_SERVER_PORT
            if (host.isBlank()) {
                if (!silent) syncMsg = "请先在「设置 → 数据同步」填写服务器IP"
                return
            }
            syncMsg = if (silent) "正在自动同步…" else "正在同步…"
            withContext(Dispatchers.IO) {
                val client = SyncClient("http://$host:$port")
                if (!client.probe())
                    throw RuntimeException("连不上电脑，请确认 PC 端已启动且在同一局域网")
                val ps = client.fetchProducts("")
                ps.forEach { Repository.db.upsertProduct(it) }
                val ql = client.fetchQtyLogs("")
                if (ql.isEmpty())
                    throw RuntimeException("未获取到每日数量日志（请确认 PC 端为最新版本），本地已有记录保持不变")
                Repository.db.replaceQtyLogs(ql)
            }
            reload()
            syncMsg = "同步完成：产品 ${products.size} 条，数量日志 ${logs.size} 条"
        } catch (e: Exception) {
            syncMsg = if (silent) "自动同步未成功，当前显示本地数据"
                      else "同步失败：${e.message}"
        } finally {
            syncing = false
        }
    }

    // 每次进入库存页：先渲染本地数据，再自动做一次无线同步（不含图纸）
    LaunchedEffect(Unit) {
        reload()
        syncStock(silent = true)
    }

    val rows = remember(products, logs) { buildRows(products, logs) }
    val shown = remember(rows, kw, sortIndex, sortAsc) {
        val k = kw.trim()
        val base = if (k.isEmpty()) rows else rows.filter {
            it.p.model.contains(k, true) || it.p.name.contains(k, true) ||
                it.p.materialCode.contains(k, true)
        }
        if (sortIndex < 0) base.sortedBy { it.rank }
        else {
            val f = SORT_KEYS[sortIndex].second
            val s = base.sortedBy(f)
            if (sortAsc) s else s.asReversed()
        }
    }
    val alarm = rows.count { it.rank == 0 || it.rank == 1 }

    Column(Modifier.fillMaxSize().navigationBarsPadding()) {
        AppMenuBar(current = "stock", nav = nav, onAbout = { showAbout = true })
        if (showAbout) AboutDialog(onDismiss = { showAbout = false })
        noDataModel?.let { name ->
            StockNoDataDialog(model = name) { noDataModel = null }
        }

        // 搜索 + 一键同步（只拉产品与数量日志，不下载图纸；断网时仍可看本地数据）
        Row(
            Modifier.fillMaxWidth().padding(horizontal = 12.dp, vertical = 5.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            Box(
                Modifier.weight(1f).height(36.dp)
                    .background(MaterialTheme.colorScheme.surfaceVariant, MaterialTheme.shapes.small)
                    .padding(horizontal = 12.dp),
                contentAlignment = Alignment.CenterStart
            ) {
                BasicTextField(
                    value = kw, onValueChange = { kw = it }, singleLine = true,
                    textStyle = MaterialTheme.typography.bodyMedium,
                    decorationBox = { inner ->
                        Box(contentAlignment = Alignment.CenterStart) {
                            if (kw.isEmpty()) Text("型号 / 编码 / 名称", fontSize = 12.sp, color = StockDim)
                            inner()
                        }
                    }
                )
            }
            Spacer(Modifier.width(8.dp))
            Button(
                onClick = syncClick@ {
                    if (syncing) return@syncClick
                    scope.launch { syncStock(silent = false) }
                },
                enabled = !syncing,
                contentPadding = PaddingValues(horizontal = 12.dp, vertical = 0.dp),
                modifier = Modifier.height(36.dp)
            ) { Text(if (syncing) "同步中" else "同步", fontSize = 12.sp) }
        }

        // 状态行
        Row(Modifier.fillMaxWidth().padding(horizontal = 12.dp), verticalAlignment = Alignment.CenterVertically) {
            Text(
                "共 ${rows.size} 项 · 报警 $alarm 项 · " +
                    (if (sortIndex < 0) "规则排序"
                     else "按${SORT_KEYS[sortIndex].first}${if (sortAsc) "从小到大" else "从大到小"}"),
                fontSize = 11.sp, color = StockDim, modifier = Modifier.weight(1f)
            )
            if (syncMsg.isNotEmpty()) {
                Text(syncMsg, fontSize = 10.sp, color = StockBlue, maxLines = 1,
                     overflow = TextOverflow.Ellipsis, modifier = Modifier.weight(1f))
            }
        }

        // 排序表头：默认规则排序；点数值列首击小→大、再击大→小
        Row(
            Modifier.fillMaxWidth().horizontalScroll(rememberScrollState())
                .padding(horizontal = 8.dp, vertical = 4.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            Text(
                "规则排序", fontSize = 12.sp,
                color = if (sortIndex < 0) StockGreen else StockDim,
                modifier = Modifier
                    .background(
                        if (sortIndex < 0) Color(0x2243CF7C) else Color.Transparent,
                        RoundedCornerShape(6.dp)
                    )
                    .clickable { sortIndex = -1; sortAsc = true }
                    .padding(horizontal = 8.dp, vertical = 5.dp)
            )
            SORT_KEYS.forEachIndexed { i, (label, _) ->
                val on = sortIndex == i
                Text(
                    label + if (on) (if (sortAsc) " ↑" else " ↓") else " ↕",
                    fontSize = 12.sp,
                    color = if (on) StockGreen else StockDim,
                    modifier = Modifier
                        .clickable {
                            if (sortIndex == i) sortAsc = !sortAsc
                            else { sortIndex = i; sortAsc = true }
                        }
                        .padding(horizontal = 8.dp, vertical = 5.dp)
                )
            }
        }

        if (logs.isEmpty()) {
            Box(Modifier.fillMaxWidth().padding(12.dp)) {
                Text(
                    "本地暂无每日数量记录。进入本页会自动无线同步，也可点右上「同步」或到「设置 → 数据同步」做一次同步；PC 端需为升级后的版本。",
                    fontSize = 11.sp, color = StockOrange
                )
            }
        }

        if (shown.isEmpty()) {
            Box(Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
                Text(
                    if (products.isEmpty()) "暂无产品，请先到「设置 → 数据同步」拉取数据" else "没有匹配的产品",
                    fontSize = 13.sp, color = StockDim
                )
            }
        } else {
            LazyColumn(Modifier.fillMaxSize()) {
                items(shown, key = { it.p.id }) { r ->
                    StockRowItem(r) {
                        if (r.hasLogs) nav.navigate("stockdetail/${r.p.id}")
                        else noDataModel = r.p.model.ifBlank { r.p.name }
                    }
                }
            }
        }
    }
}

@Composable
private fun StockRowItem(r: StockRow, onClick: () -> Unit) {
    Row(
        Modifier.fillMaxWidth().background(rowColor(r.rank))
            .clickable(onClick = onClick)
            .padding(horizontal = 12.dp, vertical = 9.dp),
        verticalAlignment = Alignment.CenterVertically
    ) {
        Column(Modifier.weight(1f)) {
            Text(r.p.model, fontSize = 14.sp, fontWeight = FontWeight.Medium,
                 color = MaterialTheme.colorScheme.onSurface, maxLines = 1,
                 overflow = TextOverflow.Ellipsis)
            Spacer(Modifier.height(2.dp))
            Text("${r.p.name}  ${r.p.materialCode}", fontSize = 11.sp,
                 color = MaterialTheme.colorScheme.onSurfaceVariant, maxLines = 1,
                 overflow = TextOverflow.Ellipsis)
            Spacer(Modifier.height(5.dp))
            Row {
                UseChip("今", r.dayUse, StockBlue)
                UseChip("周", r.weekUse, StockOrange)
                UseChip("月", r.monthUse, StockPurple)
                UseChip("年", r.yearUse, MaterialTheme.colorScheme.onSurfaceVariant)
            }
        }
        Spacer(Modifier.width(8.dp))
        Text(r.qty.toString(), fontSize = 21.sp, fontWeight = FontWeight.Bold,
             color = when (r.rank) {
                 0 -> StockRed
                 1 -> StockOrange
                 2 -> StockGreen
                 else -> MaterialTheme.colorScheme.onSurface
             })
    }
    HorizontalDivider(color = MaterialTheme.colorScheme.outlineVariant)
}

@Composable
private fun UseChip(label: String, v: Int, c: Color) {
    Text("$label $v", fontSize = 11.sp, color = c,
         modifier = Modifier.padding(end = 10.dp))
}

// ===================== 库存详情：KPI + 趋势图 =====================
@Composable
fun StockDetailScreen(id: Int, nav: NavHostController) {
    val p = remember(id) { Repository.db.productById(id) }
    val logs = remember(id) { Repository.db.qtyLogsOf(id) }
    val usage = remember(logs) { usageOf(logs) }
    if (p == null) {
        Box(Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
            Text("产品不存在", color = StockDim)
        }
        return
    }
    val today = todayStr()
    val week = daysAgoStr(6)
    val month = today.take(7)
    val year = today.take(4)
    val dayUse = sumUse(usage) { it == today }
    val weekUse = sumUse(usage) { it >= week }
    val monthUse = sumUse(usage) { it.take(7) == month }
    val yearUse = sumUse(usage) { it.take(4) == year }
    val qty = if (logs.isEmpty()) p.qty else logs.maxByOrNull { it.recordDate }?.qty ?: p.qty
    val noWarn = p.minQty == 0 && p.maxQty == 0
    val state = when {
        noWarn -> "未设预警"
        qty < p.minQty -> "低于下限"
        qty > p.maxQty -> "高于上限"
        else -> "库存正常"
    }
    val stateColor = when {
        noWarn -> StockDim
        qty < p.minQty -> StockRed
        qty > p.maxQty -> StockOrange
        else -> StockGreen
    }

    // 图表数据：每日结存、每月月末结存、每月用量
    val dailyQty = logs.takeLast(60).map { it.recordDate to it.qty.toFloat() }
    val monthlyQty = logs.groupBy { it.recordDate.take(7) }
        .toSortedMap().map { (m, l) -> m to (l.maxByOrNull { it.recordDate }?.qty ?: 0).toFloat() }
    val monthUsePts = usage.groupBy { it.date.take(7) }
        .toSortedMap().map { (m, l) -> m to l.sumOf { it.used }.toFloat() }

    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).navigationBarsPadding()) {
        Row(
            Modifier.fillMaxWidth().background(Color(0xFF2B3947)).statusBarsPadding()
                .padding(horizontal = 12.dp, vertical = 10.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            Text("← 返回", fontSize = 14.sp, color = StockBlue,
                 modifier = Modifier.clickable { nav.popBackStack() })
            Spacer(Modifier.width(16.dp))
            Text("库存详情", fontSize = 15.sp, fontWeight = FontWeight.Bold, color = Color.White)
        }
        Column(Modifier.padding(12.dp)) {
            Text(p.name, fontSize = 18.sp, fontWeight = FontWeight.Bold,
                 color = MaterialTheme.colorScheme.onSurface)
            Text("${p.model}　物料编码：${p.materialCode}", fontSize = 12.sp, color = StockDim)
            Spacer(Modifier.height(6.dp))
            Text("$state　安全区间 ${p.minQty}~${p.maxQty}　最新数量 $qty",
                 fontSize = 12.sp, color = stateColor)
            Spacer(Modifier.height(10.dp))

            Row(Modifier.fillMaxWidth()) {
                KpiCard("最新数量", qty.toString(), StockGreen, Modifier.weight(1f))
                KpiCard("今日用量", dayUse.toString(), StockBlue, Modifier.weight(1f))
                KpiCard("周用量", weekUse.toString(), StockOrange, Modifier.weight(1f))
            }
            Spacer(Modifier.height(8.dp))
            Row(Modifier.fillMaxWidth()) {
                KpiCard("月用量", monthUse.toString(), StockPurple, Modifier.weight(1f))
                KpiCard("年用量", yearUse.toString(), Color.White, Modifier.weight(1f))
                Spacer(Modifier.weight(1f))
            }

            Spacer(Modifier.height(12.dp))
            MiniLineChart("每日数量趋势（最近 60 条）", dailyQty, StockGreen)
            Spacer(Modifier.height(10.dp))
            MiniLineChart("每月数量趋势（月末结存）", monthlyQty, StockBlue)
            Spacer(Modifier.height(10.dp))
            MiniBarChart("每日用量 / 入库（最近 30 天）", usage.takeLast(30))
            Text(
                "2026-11 前的首次录入按「建立库存」计，不统计入库；11 月起首次录入按从 0 入库计。",
                fontSize = 10.sp, color = StockDim
            )
            Spacer(Modifier.height(10.dp))
            MiniLineChart("每月用量趋势", monthUsePts, StockPurple)
        }
    }
}

@Composable
private fun KpiCard(label: String, value: String, color: Color, modifier: Modifier = Modifier) {
    Column(modifier.background(StockCard, RoundedCornerShape(10.dp)).padding(10.dp)) {
        Text(label, fontSize = 11.sp, color = StockDim)
        Text(value, fontSize = 22.sp, fontWeight = FontWeight.Bold, color = color)
    }
}

// 通用折线图：点击图表按横向位置选中最近的数据点，下方显示对应数值
@Composable
private fun MiniLineChart(title: String, points: List<Pair<String, Float>>, color: Color) {
    var selected by remember(points) { mutableIntStateOf(-1) }
    Column(Modifier.fillMaxWidth().background(StockCard, RoundedCornerShape(10.dp)).padding(10.dp)) {
        Text(title, fontSize = 13.sp, fontWeight = FontWeight.Bold, color = Color.White)
        Spacer(Modifier.height(6.dp))
        if (points.isEmpty()) {
            Text("暂无数据", fontSize = 11.sp, color = StockDim)
        } else {
            val vals = points.map { it.second }
            var mx = vals.maxOrNull() ?: 1f
            var mn = vals.minOrNull() ?: 0f
            if (mx == mn) { mx += 1f; mn = (mn - 1f).coerceAtLeast(0f) }
            val range = (mx - mn).coerceAtLeast(1f)
            Canvas(
                modifier = Modifier.fillMaxWidth().height(130.dp)
                    .pointerInput(points) {
                        detectTapGestures { off ->
                            val i = if (points.size == 1) 0
                            else ((off.x / size.width) * (points.size - 1)).roundToInt()
                                .coerceIn(0, points.size - 1)
                            selected = i
                        }
                    }
            ) {
                val left = 8.dp.toPx()
                val top = 8.dp.toPx()
                val bottom = size.height - 8.dp.toPx()
                val right = size.width - 8.dp.toPx()
                val plotW = (right - left).coerceAtLeast(1f)
                for (k in 0..3) {
                    val y = top + (bottom - top) * k / 3f
                    drawLine(Color.White.copy(alpha = 0.08f), Offset(left, y), Offset(right, y), 1f)
                }
                val pts = points.mapIndexed { i, p ->
                    val x = left + if (points.size == 1) plotW / 2f else i * plotW / (points.size - 1)
                    val y = top + (mx - p.second) / range * (bottom - top)
                    Offset(x, y)
                }
                val area = Path().apply {
                    moveTo(pts.first().x, bottom)
                    pts.forEach { lineTo(it.x, it.y) }
                    lineTo(pts.last().x, bottom)
                    close()
                }
                drawPath(area, Brush.verticalGradient(listOf(color.copy(alpha = 0.35f), Color.Transparent)))
                for (i in 1 until pts.size)
                    drawLine(color, pts[i - 1], pts[i], strokeWidth = 2.5.dp.toPx(), cap = StrokeCap.Round)
                pts.forEach { drawCircle(color, radius = 2.5.dp.toPx(), center = it) }
                if (selected in pts.indices) {
                    drawLine(Color.White.copy(alpha = 0.45f),
                        Offset(pts[selected].x, top), Offset(pts[selected].x, bottom), 1.5f)
                    drawCircle(Color.White, radius = 4.dp.toPx(), center = pts[selected])
                }
            }
            Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
                Text(points.first().first, fontSize = 10.sp, color = StockDim)
                Text(points.last().first, fontSize = 10.sp, color = StockDim)
            }
            val tip = if (selected in points.indices)
                "${points[selected].first}　${points[selected].second.toInt()}"
            else "点击图表查看对应日期数值"
            Text(tip, fontSize = 11.sp, color = if (selected in points.indices) color else StockDim)
        }
    }
}

// 每日用量 / 入库柱状图：蓝 = 用量，绿 = 入库
@Composable
private fun MiniBarChart(title: String, usage: List<Usage>) {
    var selected by remember(usage) { mutableIntStateOf(-1) }
    Column(Modifier.fillMaxWidth().background(StockCard, RoundedCornerShape(10.dp)).padding(10.dp)) {
        Text(title, fontSize = 13.sp, fontWeight = FontWeight.Bold, color = Color.White)
        Spacer(Modifier.height(6.dp))
        if (usage.isEmpty()) {
            Text("暂无数据（至少需要两天的数量）", fontSize = 11.sp, color = StockDim)
        } else {
            val mx = maxOf(1, usage.maxOf { maxOf(it.used, it.inc) }).toFloat()
            Canvas(
                modifier = Modifier.fillMaxWidth().height(120.dp)
                    .pointerInput(usage) {
                        detectTapGestures { off ->
                            selected = ((off.x / size.width) * usage.size).roundToInt()
                                .coerceIn(0, usage.size - 1)
                        }
                    }
            ) {
                val top = 8.dp.toPx()
                val bottom = size.height - 8.dp.toPx()
                for (k in 0..3) {
                    val y = top + (bottom - top) * k / 3f
                    drawLine(Color.White.copy(alpha = 0.08f), Offset(0f, y), Offset(size.width, y), 1f)
                }
                val slot = size.width / usage.size
                val bw = (slot / 3f).coerceAtMost(8.dp.toPx()).coerceAtLeast(1.5f)
                usage.forEachIndexed { i, u ->
                    val x = i * slot + slot * 0.15f
                    val h1 = u.used / mx * (bottom - top)
                    val h2 = u.inc / mx * (bottom - top)
                    if (h1 > 0f)
                        drawRect(Brush.verticalGradient(listOf(StockBlue, Color(0xFF1673DB))),
                            Offset(x, bottom - h1), Size(bw, h1))
                    if (h2 > 0f)
                        drawRect(StockGreen, Offset(x + bw + 1f, bottom - h2), Size(bw, h2))
                }
                if (selected in usage.indices)
                    drawLine(Color.White.copy(alpha = 0.4f),
                        Offset(selected * slot + slot / 2f, top),
                        Offset(selected * slot + slot / 2f, bottom), 1.5f)
            }
            Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
                Text(usage.first().date, fontSize = 10.sp, color = StockDim)
                Text(usage.last().date, fontSize = 10.sp, color = StockDim)
            }
            val tip = if (selected in usage.indices)
                "${usage[selected].date}　用量 ${usage[selected].used}　入库 ${usage[selected].inc}"
            else "蓝：用量　绿：入库　点击图表查看数值"
            Text(tip, fontSize = 11.sp, color = if (selected in usage.indices) StockBlue else StockDim)
        }
    }
}

/** 与「关于」弹窗同一套透明遮罩 + 深色卡片：点击未录入数量的产品时给出提示 */
@Composable
private fun StockNoDataDialog(model: String, onDismiss: () -> Unit) {
    val who = if (model.isBlank()) "该产品" else "「$model」"
    Dialog(
        onDismissRequest = onDismiss,
        properties = DialogProperties(usePlatformDefaultWidth = false)
    ) {
        Box(
            Modifier.fillMaxSize()
                .background(Color.Black.copy(alpha = 0.55f))
                .clickable(onClick = onDismiss),
            contentAlignment = Alignment.Center
        ) {
            Card(
                Modifier.widthIn(min = 280.dp, max = 340.dp)
                    .clickable { },
                shape = RoundedCornerShape(12.dp),
                colors = CardDefaults.cardColors(containerColor = Color(0xFF223040))
            ) {
                Column(
                    Modifier.padding(24.dp),
                    horizontalAlignment = Alignment.CenterHorizontally
                ) {
                    Image(
                        painter = painterResource(R.drawable.logo),
                        contentDescription = "logo",
                        modifier = Modifier.size(48.dp)
                    )
                    Spacer(Modifier.height(10.dp))
                    Text("暂无数量记录", color = Color.White,
                         fontWeight = FontWeight.Bold, fontSize = 18.sp)
                    Spacer(Modifier.height(10.dp))
                    Text(
                        "${who}还没有录入每日数量，暂时无法查看用量与趋势图。\n" +
                            "请先在电脑端录入数量，再到库存页同步后查看。",
                        color = Color(0xFFC8D2DC), fontSize = 13.sp,
                        lineHeight = 20.sp, textAlign = TextAlign.Center
                    )
                    Spacer(Modifier.height(16.dp))
                    HorizontalDivider(color = Color(0xFF4A5468))
                    Spacer(Modifier.height(12.dp))
                    Button(onClick = onDismiss, modifier = Modifier.fillMaxWidth()) {
                        Text("确 定", fontSize = 14.sp)
                    }
                }
            }
        }
    }
}
