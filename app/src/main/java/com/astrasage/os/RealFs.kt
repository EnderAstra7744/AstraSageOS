package com.astrasage.os

import android.os.Environment
import java.io.File

/**
 * Real device file system helpers (primary external storage).
 */
object RealFs {

    fun root(): File {
        val ext = Environment.getExternalStorageDirectory()
        return if (ext != null && ext.exists()) ext else File("/storage/emulated/0")
    }

    fun home(): File {
        val docs = Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_DOCUMENTS)
        if (docs != null) {
            val asHome = File(docs, "AstraSage")
            if (!asHome.exists()) asHome.mkdirs()
            return asHome
        }
        val f = File(root(), "AstraSage")
        if (!f.exists()) f.mkdirs()
        return f
    }

    fun list(dir: File): List<File> {
        if (!dir.exists() || !dir.isDirectory) return emptyList()
        val files = dir.listFiles() ?: return emptyList()
        return files.sortedWith(compareBy<File> { !it.isDirectory }.thenBy { it.name.lowercase() })
    }

    fun formatSize(bytes: Long): String {
        if (bytes < 1024) return "$bytes B"
        val kb = bytes / 1024.0
        if (kb < 1024) return String.format("%.1f KB", kb)
        val mb = kb / 1024.0
        if (mb < 1024) return String.format("%.1f MB", mb)
        return String.format("%.2f GB", mb / 1024.0)
    }

    fun relativePath(file: File): String {
        val r = root().absolutePath
        val p = file.absolutePath
        return if (p.startsWith(r)) p.removePrefix(r).ifEmpty { "/" } else p
    }
}
