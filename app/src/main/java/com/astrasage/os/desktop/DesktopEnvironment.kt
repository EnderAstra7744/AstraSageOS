package com.astrasage.os.desktop

/**
 * UI-layer profile only. Core (terminal, files, apps, user data) is shared.
 */
data class DesktopEnvironment(
    val id: String,
    val displayName: String,
    val description: String,
    val emoji: String,
    val gridCols: Int,
    val gridRows: Int,
    val taskbarHeightDp: Int,
    val iconScaleDefault: Float,
    val showIconNames: Boolean,
    val windowChromeRadiusDp: Int,
    val accentArgb: Int,
    val taskbarStyle: TaskbarStyle,
    val startMenuStyle: StartMenuStyle,
    val minimalMode: Boolean = false,
    val largeTouch: Boolean = false
)

enum class TaskbarStyle {
    MODERN_DOCK,    // AstraUI
    CLASSIC_BAR,    // AstraClassic
    MINIMAL_STRIP,  // AstraMinimal
    FLOW_BOTTOM     // AstraFlow — larger touch targets
}

enum class StartMenuStyle {
    MODERN,
    CLASSIC,
    MINIMAL,
    FLOW
}

object DesktopEnvironmentRegistry {
    val AstraUI = DesktopEnvironment(
        id = "astraui",
        displayName = "AstraUI",
        description = "Modern Desktop",
        emoji = "🟢",
        gridCols = 4,
        gridRows = 4,
        taskbarHeightDp = 52,
        iconScaleDefault = 1.0f,
        showIconNames = true,
        windowChromeRadiusDp = 12,
        accentArgb = 0xFFB8FF1A.toInt(),
        taskbarStyle = TaskbarStyle.MODERN_DOCK,
        startMenuStyle = StartMenuStyle.MODERN
    )

    val AstraClassic = DesktopEnvironment(
        id = "astraclassic",
        displayName = "AstraClassic",
        description = "Classic Desktop",
        emoji = "🔵",
        gridCols = 5,
        gridRows = 4,
        taskbarHeightDp = 40,
        iconScaleDefault = 0.95f,
        showIconNames = true,
        windowChromeRadiusDp = 2,
        accentArgb = 0xFF4FC3F7.toInt(),
        taskbarStyle = TaskbarStyle.CLASSIC_BAR,
        startMenuStyle = StartMenuStyle.CLASSIC
    )

    val AstraMinimal = DesktopEnvironment(
        id = "astraminimal",
        displayName = "AstraMinimal",
        description = "Minimal Desktop",
        emoji = "🟣",
        gridCols = 3,
        gridRows = 3,
        taskbarHeightDp = 36,
        iconScaleDefault = 0.9f,
        showIconNames = false,
        windowChromeRadiusDp = 6,
        accentArgb = 0xFFCE93D8.toInt(),
        taskbarStyle = TaskbarStyle.MINIMAL_STRIP,
        startMenuStyle = StartMenuStyle.MINIMAL,
        minimalMode = true
    )

    val AstraFlow = DesktopEnvironment(
        id = "astraflow",
        displayName = "AstraFlow",
        description = "Mobile Desktop",
        emoji = "🟠",
        gridCols = 2,
        gridRows = 4,
        taskbarHeightDp = 64,
        iconScaleDefault = 1.25f,
        showIconNames = true,
        windowChromeRadiusDp = 16,
        accentArgb = 0xFFFFB74D.toInt(),
        taskbarStyle = TaskbarStyle.FLOW_BOTTOM,
        startMenuStyle = StartMenuStyle.FLOW,
        largeTouch = true
    )

    val all: List<DesktopEnvironment> = listOf(AstraUI, AstraClassic, AstraMinimal, AstraFlow)

    fun byId(id: String): DesktopEnvironment =
        all.find { it.id.equals(id, true) || it.displayName.equals(id, true) } ?: AstraUI
}
