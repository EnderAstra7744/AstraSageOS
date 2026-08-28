package com.astrasage.os.desktop

import org.json.JSONObject

data class GridCell(val col: Int, val row: Int) {
    fun key() = "$col,$row"
}

/**
 * Occupancy grid for desktop icons. No two items share a cell.
 * Positions are stored as col,row (not free pixels).
 */
class GridManager(
    var cols: Int,
    var rows: Int
) {
    /** key → "col,row" */
    private val occupancy = mutableMapOf<String, GridCell>()

    fun clear() = occupancy.clear()

    fun isOccupied(cell: GridCell, ignoreKey: String? = null): Boolean {
        return occupancy.any { (k, c) -> k != ignoreKey && c.col == cell.col && c.row == cell.row }
    }

    fun get(key: String): GridCell? = occupancy[key]

    fun place(key: String, cell: GridCell): Boolean {
        if (cell.col !in 0 until cols || cell.row !in 0 until rows) return false
        if (isOccupied(cell, key)) return false
        occupancy[key] = cell
        return true
    }

    fun remove(key: String) {
        occupancy.remove(key)
    }

    fun findFirstFree(): GridCell? {
        for (r in 0 until rows) {
            for (c in 0 until cols) {
                val cell = GridCell(c, r)
                if (!isOccupied(cell)) return cell
            }
        }
        // Expand rows if full
        rows += 1
        return GridCell(0, rows - 1)
    }

    fun placeOrNextFree(key: String, preferred: GridCell?): GridCell {
        val tryCell = preferred
        if (tryCell != null && place(key, tryCell)) return tryCell
        occupancy.remove(key)
        val free = findFirstFree() ?: GridCell(0, 0)
        occupancy[key] = free
        return free
    }

    /** Snap pixel center to nearest free cell (or occupied by same key). */
    fun snapToNearest(key: String, centerX: Float, centerY: Float, cellW: Float, cellH: Float, originX: Float, originY: Float): GridCell {
        val col = ((centerX - originX) / cellW).toInt().coerceIn(0, cols - 1)
        val row = ((centerY - originY) / cellH).toInt().coerceIn(0, (rows - 1).coerceAtLeast(0))
        val preferred = GridCell(col, row)
        if (!isOccupied(preferred, key)) {
            occupancy[key] = preferred
            return preferred
        }
        // Spiral search nearest free
        for (radius in 1..maxOf(cols, rows) + 2) {
            for (dr in -radius..radius) {
                for (dc in -radius..radius) {
                    if (kotlin.math.abs(dr) != radius && kotlin.math.abs(dc) != radius) continue
                    val c = GridCell(col + dc, row + dr)
                    if (c.col in 0 until cols && c.row in 0 until rows && !isOccupied(c, key)) {
                        occupancy[key] = c
                        return c
                    }
                }
            }
        }
        return placeOrNextFree(key, null)
    }

    fun pixelX(cell: GridCell, cellW: Float, originX: Float) = originX + cell.col * cellW
    fun pixelY(cell: GridCell, cellH: Float, originY: Float) = originY + cell.row * cellH

    fun toJson(): JSONObject {
        val o = JSONObject()
        occupancy.forEach { (k, c) ->
            o.put(k, JSONObject().put("c", c.col).put("r", c.row))
        }
        return o
    }

    fun loadJson(json: String) {
        occupancy.clear()
        try {
            val o = JSONObject(json)
            val keys = o.keys()
            while (keys.hasNext()) {
                val k = keys.next()
                val cell = o.getJSONObject(k)
                occupancy[k] = GridCell(cell.optInt("c"), cell.optInt("r"))
            }
        } catch (_: Exception) {
        }
    }

    fun occupiedCells(): Map<String, GridCell> = occupancy.toMap()
}
