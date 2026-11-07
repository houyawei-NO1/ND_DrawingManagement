package com.houyawei.nddrawing.ui

import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.interaction.collectIsPressedAsState
import androidx.compose.foundation.layout.*
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.drawBehind
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.navigation.NavHostController
import com.houyawei.nddrawing.R

// ===== PC 端 menubar 配色 =====
private val MenuBarBg = Color(0xFF2B3947)
private val MenuBarActiveBg = Color(0xFF2E5C8A)
private val MenuBarActiveLine = Color(0xFF4E93D8)
private val MenuBarItemPressedBg = Color(0xFF37495C)
private val MenuBarText = Color(0xFFE9EEF3)
private val MenuBarActiveText = Color(0xFF8FC6F2)

private data class MenuEntry(val route: String, val label: String)

// 主页 / 扫描 / 设置：与路由名一致，用 current 决定哪一项高亮
// 「同步」已改名为「设置」，原同步功能作为设置下的「数据同步」子页面
private val MENU_ENTRIES = listOf(
    MenuEntry("list", "主页"),
    MenuEntry("scan", "扫描"),
    MenuEntry("settings", "设置"),
)

/**
 * 顶部菜单栏：深色条 + 纯文字菜单项，当前页常驻高亮（蓝色底 + 底部亮蓝线），
 * 风格对齐 PC 端主界面 menubar。主页 / 扫描 / 设置 三个页面共用。
 */
@Composable
fun AppMenuBar(current: String, nav: NavHostController, onAbout: () -> Unit) {
    Row(
        Modifier
            .fillMaxWidth()
            .background(MenuBarBg)
            .statusBarsPadding()
            .height(44.dp)
            .padding(start = 10.dp, end = 4.dp),
        verticalAlignment = Alignment.CenterVertically
    ) {
        Image(
            painter = painterResource(R.drawable.logo),
            contentDescription = "南都 logo",
            modifier = Modifier.size(22.dp)
        )
        Spacer(Modifier.width(7.dp))
        // 标题占据剩余宽度，空间不足时省略，保证右侧菜单项始终可见
        Text(
            "南都图纸",
            fontSize = 14.sp,
            fontWeight = FontWeight.Medium,
            color = MenuBarText,
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
            modifier = Modifier.weight(1f)
        )
        MENU_ENTRIES.forEach { entry ->
            MenuBarItem(
                text = entry.label,
                active = current == entry.route,
                onClick = {
                    // 始终回到主页再进目标页，避免扫描/同步互相叠加返回栈
                    nav.navigate(entry.route) {
                        popUpTo("list") { inclusive = false }
                        launchSingleTop = true
                    }
                }
            )
        }
        // 关于：弹窗而非页面跳转
        MenuBarItem("关于", active = false, onClick = onAbout)
    }
}

@Composable
private fun MenuBarItem(text: String, active: Boolean, onClick: () -> Unit) {
    val interaction = remember { MutableInteractionSource() }
    val pressed by interaction.collectIsPressedAsState()
    val bg = when {
        active -> MenuBarActiveBg
        pressed -> MenuBarItemPressedBg
        else -> Color.Transparent
    }
    Box(
        Modifier
            .fillMaxHeight()
            .background(bg)
            // 用绘制而不是 fillMaxWidth 的子 Box：否则会撑满 Row 剩余宽度，把后面的菜单项挤出屏幕
            .drawBehind {
                if (active) {
                    val lineH = 2.dp.toPx()
                    drawRect(
                        color = MenuBarActiveLine,
                        topLeft = Offset(0f, size.height - lineH),
                        size = Size(size.width, lineH)
                    )
                }
            }
            .clickable(interactionSource = interaction, indication = null, onClick = onClick),
        contentAlignment = Alignment.Center
    ) {
        Text(
            text,
            fontSize = 14.sp,
            color = if (active) MenuBarActiveText else MenuBarText,
            maxLines = 1,
            modifier = Modifier.padding(horizontal = 14.dp)
        )
    }
}
