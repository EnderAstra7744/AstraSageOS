package com.astrasage.os.desktop

import android.content.Context
import android.graphics.Canvas
import android.graphics.Paint
import android.util.AttributeSet
import android.view.View

class GridOverlay @JvmOverloads constructor(
    context: Context, attrs: AttributeSet? = null
) : View(context, attrs) {
    var cols = 4
    var rows = 4
    var originX = 0f
    var originY = 0f
    var cellW = 1f
    var cellH = 1f
    var highlightCol = -1
    var highlightRow = -1
    var show = false

    private val linePaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = 0x33B8FF1A
        strokeWidth = 1.5f
        style = Paint.Style.STROKE
    }
    private val hlPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = 0x44B8FF1A
        style = Paint.Style.FILL
    }

    override fun onDraw(canvas: Canvas) {
        if (!show) return
        for (c in 0..cols) {
            val x = originX + c * cellW
            canvas.drawLine(x, originY, x, originY + rows * cellH, linePaint)
        }
        for (r in 0..rows) {
            val y = originY + r * cellH
            canvas.drawLine(originX, y, originX + cols * cellW, y, linePaint)
        }
        if (highlightCol in 0 until cols && highlightRow in 0 until rows) {
            canvas.drawRect(
                originX + highlightCol * cellW,
                originY + highlightRow * cellH,
                originX + (highlightCol + 1) * cellW,
                originY + (highlightRow + 1) * cellH,
                hlPaint
            )
        }
    }

    fun setHighlight(col: Int, row: Int) {
        highlightCol = col
        highlightRow = row
        invalidate()
    }
}
