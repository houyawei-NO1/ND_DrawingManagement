package com.houyawei.nddrawing.ui.theme

import androidx.compose.material3.darkColorScheme
import androidx.compose.material3.lightColorScheme
import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.graphics.Color

// 取自 PC 端「南都图纸管理系统」的深色主题配色
private val NdGreen = Color(0xFF43CF7C)
private val NdGreenHover = Color(0xFF5CE0A0)
private val NdBgPanel = Color(0xFF2B3046)
private val NdBgCard = Color(0xFF22293C)
private val NdBorder = Color(0xFF4A5468)
private val NdDanger = Color(0xFFE06C75)
private val NdTextPrimary = Color(0xFFFFFFFF)
private val NdTextDark = Color(0xFF0B1F14)

private val DarkColorScheme = darkColorScheme(
    primary = NdGreen,
    onPrimary = NdTextDark,
    primaryContainer = NdGreenHover,
    onPrimaryContainer = NdTextDark,
    background = NdBgPanel,
    onBackground = NdTextPrimary,
    surface = NdBgCard,
    onSurface = NdTextPrimary,
    surfaceVariant = NdBgCard,
    onSurfaceVariant = NdTextPrimary,
    outline = NdBorder,
    outlineVariant = NdBorder,
    error = NdDanger,
    onError = Color.White,
    secondary = NdGreenHover,
    onSecondary = NdTextDark
)

// 浅色方案：沿用 PC 的绿色强调，适合户外强光
private val LightColorScheme = lightColorScheme(
    primary = NdGreen,
    onPrimary = NdTextDark,
    background = Color(0xFFF4F6FA),
    onBackground = Color(0xFF1A1F2E),
    surface = Color.White,
    onSurface = Color(0xFF1A1F2E),
    outline = NdBorder,
    error = NdDanger,
    onError = Color.White,
    secondary = NdGreenHover,
    onSecondary = NdTextDark
)

@Composable
fun NDDrawingTheme(dark: Boolean = true, content: @Composable () -> Unit) {
    MaterialTheme(
        colorScheme = if (dark) DarkColorScheme else LightColorScheme,
        content = content
    )
}
