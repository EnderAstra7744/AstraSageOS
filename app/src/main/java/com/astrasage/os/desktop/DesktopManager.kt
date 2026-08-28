package com.astrasage.os.desktop

import android.content.Context
import com.astrasage.os.Prefs

/**
 * Central DE + grid coordinator. UI-only; does not touch files/apps core data.
 */
object DesktopManager {
    private var current: DesktopEnvironment = DesktopEnvironmentRegistry.AstraUI
    private var grid: GridManager = GridManager(current.gridCols, current.gridRows)
    private var listener: ((DesktopEnvironment) -> Unit)? = null

    fun current(): DesktopEnvironment = current
    fun grid(): GridManager = grid
    fun all(): List<DesktopEnvironment> = DesktopEnvironmentRegistry.all

    fun setListener(l: ((DesktopEnvironment) -> Unit)?) {
        listener = l
    }

    fun init(ctx: Context) {
        val id = Prefs.getActiveDesktop(ctx)
        current = DesktopEnvironmentRegistry.byId(id)
        grid = GridManager(current.gridCols, current.gridRows)
        grid.loadJson(Prefs.getGridLayout(ctx, current.id))
    }

    fun switchTo(ctx: Context, idOrName: String): DesktopEnvironment {
        // Save current layout
        Prefs.setGridLayout(ctx, current.id, grid.toJson().toString())

        current = DesktopEnvironmentRegistry.byId(idOrName)
        Prefs.setActiveDesktop(ctx, current.id)

        grid = GridManager(current.gridCols, current.gridRows)
        grid.loadJson(Prefs.getGridLayout(ctx, current.id))

        listener?.invoke(current)
        return current
    }

    fun saveLayout(ctx: Context) {
        Prefs.setGridLayout(ctx, current.id, grid.toJson().toString())
    }

    fun listStatus(): String {
        val sb = StringBuilder()
        all().forEachIndexed { i, de ->
            val mark = if (de.id == current.id) " [ACTIVE]" else ""
            sb.append("${i + 1}. ${de.displayName}$mark\n")
        }
        return sb.toString().trimEnd()
    }
}
