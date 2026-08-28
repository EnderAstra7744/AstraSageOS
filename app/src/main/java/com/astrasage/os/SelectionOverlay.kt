package com.astrasage.os

import android.content.Context
import android.graphics.Canvas
import android.graphics.Paint
import android.graphics.RectF
import android.util.AttributeSet
import android.view.View
import androidx.core.content.ContextCompat

/**
 * Windows-style rubber-band selection rectangle (blue fill + border).
 */
class SelectionOverlay @JvmOverloads constructor(
    context: Context,
    attrs: AttributeSet? = null
) : View(context, attrs) {

    private val fillPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.FILL
        color = ContextCompat.getColor(context, R.color.select_fill)
    }
    private val strokePaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.STROKE
        strokeWidth = 2f * resources.displayMetrics.density
        color = ContextCompat.getColor(context, R.color.select_stroke)
    }

    private val rect = RectF()
    var active = false
        private set

    fun setRect(left: Float, top: Float, right: Float, bottom: Float) {
        rect.set(
            minOf(left, right),
            minOf(top, bottom),
            maxOf(left, right),
            maxOf(top, bottom)
        )
        active = true
        visibility = VISIBLE
        invalidate()
    }

    fun clear() {
        active = false
        visibility = INVISIBLE
        invalidate()
    }

    fun selectionRect(): RectF = RectF(rect)

    override fun onDraw(canvas: Canvas) {
        if (!active) return
        canvas.drawRect(rect, fillPaint)
        canvas.drawRect(rect, strokePaint)
    }
}
