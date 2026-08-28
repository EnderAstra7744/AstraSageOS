package com.astrasage.os

import android.content.Context
import android.graphics.*
import android.util.AttributeSet
import android.view.MotionEvent
import android.view.View
import kotlin.math.hypot

class PaintCanvasView @JvmOverloads constructor(
    context: Context, attrs: AttributeSet? = null
) : View(context, attrs) {

    enum class Tool { BRUSH, ERASER, LINE, RECT, CIRCLE, FILL }

    var tool = Tool.BRUSH
    var strokeWidth = 8f
    var color = Color.BLACK

    private var bitmap: Bitmap? = null
    private var canvasBmp: Canvas? = null
    private val paint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.STROKE
        strokeJoin = Paint.Join.ROUND
        strokeCap = Paint.Cap.ROUND
    }
    private val path = Path()
    private val undoStack = ArrayDeque<Bitmap>()
    private var startX = 0f
    private var startY = 0f
    private var preview: Bitmap? = null

    private fun ensureBmp() {
        if (bitmap == null && width > 0 && height > 0) {
            bitmap = Bitmap.createBitmap(width, height, Bitmap.Config.ARGB_8888)
            canvasBmp = Canvas(bitmap!!)
            canvasBmp?.drawColor(Color.WHITE)
        }
    }

    override fun onSizeChanged(w: Int, h: Int, oldw: Int, oldh: Int) {
        super.onSizeChanged(w, h, oldw, oldh)
        if (w > 0 && h > 0) {
            val old = bitmap
            bitmap = Bitmap.createBitmap(w, h, Bitmap.Config.ARGB_8888)
            canvasBmp = Canvas(bitmap!!)
            canvasBmp?.drawColor(Color.WHITE)
            if (old != null) canvasBmp?.drawBitmap(old, 0f, 0f, null)
        }
    }

    override fun onDraw(canvas: Canvas) {
        ensureBmp()
        bitmap?.let { canvas.drawBitmap(it, 0f, 0f, null) }
        preview?.let { canvas.drawBitmap(it, 0f, 0f, null) }
    }

    private fun pushUndo() {
        bitmap?.let {
            if (undoStack.size > 20) undoStack.removeFirst()
            undoStack.addLast(it.copy(Bitmap.Config.ARGB_8888, true))
        }
    }

    fun undo() {
        if (undoStack.isEmpty()) return
        bitmap = undoStack.removeLast()
        canvasBmp = Canvas(bitmap!!)
        invalidate()
    }

    fun clearCanvas() {
        pushUndo()
        canvasBmp?.drawColor(Color.WHITE)
        invalidate()
    }

    private fun applyPaint() {
        paint.strokeWidth = strokeWidth
        if (tool == Tool.ERASER) {
            paint.color = Color.WHITE
            paint.xfermode = null
            paint.style = Paint.Style.STROKE
        } else {
            paint.color = color
            paint.xfermode = null
            paint.style = if (tool == Tool.FILL) Paint.Style.FILL else Paint.Style.STROKE
        }
    }

    override fun onTouchEvent(event: MotionEvent): Boolean {
        ensureBmp()
        val x = event.x
        val y = event.y
        applyPaint()
        when (event.actionMasked) {
            MotionEvent.ACTION_DOWN -> {
                startX = x; startY = y
                when (tool) {
                    Tool.BRUSH, Tool.ERASER -> {
                        pushUndo()
                        path.reset()
                        path.moveTo(x, y)
                    }
                    Tool.FILL -> {
                        pushUndo()
                        floodFill(x.toInt(), y.toInt(), color)
                        invalidate()
                    }
                    else -> {}
                }
            }
            MotionEvent.ACTION_MOVE -> {
                when (tool) {
                    Tool.BRUSH, Tool.ERASER -> {
                        path.lineTo(x, y)
                        canvasBmp?.drawPath(path, paint)
                        path.reset()
                        path.moveTo(x, y)
                        invalidate()
                    }
                    Tool.LINE, Tool.RECT, Tool.CIRCLE -> {
                        preview = bitmap?.copy(Bitmap.Config.ARGB_8888, true)
                        val c = Canvas(preview!!)
                        drawShape(c, startX, startY, x, y)
                        invalidate()
                    }
                    else -> {}
                }
            }
            MotionEvent.ACTION_UP -> {
                when (tool) {
                    Tool.LINE, Tool.RECT, Tool.CIRCLE -> {
                        pushUndo()
                        drawShape(canvasBmp!!, startX, startY, x, y)
                        preview = null
                        invalidate()
                    }
                    else -> preview = null
                }
            }
        }
        return true
    }

    private fun drawShape(c: Canvas, x0: Float, y0: Float, x1: Float, y1: Float) {
        applyPaint()
        when (tool) {
            Tool.LINE -> c.drawLine(x0, y0, x1, y1, paint)
            Tool.RECT -> c.drawRect(
                minOf(x0, x1), minOf(y0, y1), maxOf(x0, x1), maxOf(y0, y1), paint
            )
            Tool.CIRCLE -> {
                val r = hypot((x1 - x0).toDouble(), (y1 - y0).toDouble()).toFloat()
                c.drawCircle(x0, y0, r, paint)
            }
            else -> {}
        }
    }

    private fun floodFill(sx: Int, sy: Int, replacement: Int) {
        val bmp = bitmap ?: return
        if (sx !in 0 until bmp.width || sy !in 0 until bmp.height) return
        val target = bmp.getPixel(sx, sy)
        if (target == replacement) return
        val w = bmp.width
        val h = bmp.height
        val stack = ArrayDeque<Pair<Int, Int>>()
        stack.add(sx to sy)
        var steps = 0
        while (stack.isNotEmpty() && steps < 200_000) {
            val (x, y) = stack.removeLast()
            if (x !in 0 until w || y !in 0 until h) continue
            if (bmp.getPixel(x, y) != target) continue
            bmp.setPixel(x, y, replacement)
            stack.add(x + 1 to y)
            stack.add(x - 1 to y)
            stack.add(x to y + 1)
            stack.add(x to y - 1)
            steps++
        }
    }
}
