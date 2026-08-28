package com.astrasage.os

import android.content.Context
import android.os.Build
import android.os.Environment
import android.os.StatFs
import android.os.VibrationEffect
import android.os.Vibrator
import android.os.VibratorManager
import com.astrasage.os.desktop.DesktopManager
import java.io.File
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

/**
 * AST command engine — mirrors AstraSage Python (astrasage.py) CLI surface
 * as closely as practical inside the Android app (no real Python runtime).
 */
class AstEngine(
    private val context: Context,
    private val append: (String) -> Unit
) {
    var cwd: File = RealFs.home()

    private val history = mutableListOf<String>()

    fun banner() {
        append(
            """
            |╔══════════════════════════════════════╗
            |║         AstraSage OS  ·  AST         ║
            |║   Terminal (Python CLI uyumlu çekirdek)║
            |╚══════════════════════════════════════╝
            |
            |Komutlar `as ...` öneki ile çalışır (Python projesi ile aynı).
            |Kısa yollar: help, pwd, ls, cd, clear, neofetch, exit
            |
            """.trimMargin()
        )
    }

    fun prompt(): String {
        val short = try {
            val home = RealFs.home().absolutePath
            val p = cwd.absolutePath
            if (p.startsWith(home)) "~" + p.removePrefix(home) else p
        } catch (_: Exception) {
            cwd.absolutePath
        }
        return "user@astrasage:$short/\$>> "
    }

    fun run(raw: String) {
        val line = raw.trim()
        if (line.isEmpty()) {
            append("Lütfen bir komut girin.\n")
            return
        }
        if (line != "as history") {
            history.add(line)
            if (history.size > 100) history.removeAt(0)
        }

        val parts = line.split(Regex("\\s+"))
        val head = parts[0].lowercase(Locale.ROOT)

        when (head) {
            "help", "?" -> help()
            "clear", "cls" -> { /* handled by UI */ append("\u0000CLEAR\n") }
            "exit", "quit" -> append("\u0000EXIT\n")
            "pwd" -> append(cwd.absolutePath + "\n")
            "ls" -> cmdLs(parts.drop(1))
            "cd" -> cmdCd(parts.getOrNull(1))
            "cat", "read" -> cmdCat(parts.getOrNull(1))
            "mkdir" -> cmdMkdir(parts.getOrNull(1))
            "touch" -> cmdTouch(parts.getOrNull(1))
            "rm" -> cmdRm(parts.getOrNull(1))
            "df" -> cmdDf()
            "neofetch" -> cmdNeofetch()
            "whoami" -> append((Prefs.getUser(context).ifBlank { "user" }) + "\n")
            "uname" -> append("AstraSage OS · Android ${Build.VERSION.RELEASE}\n")
            "apps" -> cmdApps()
            "oc" -> {
                val last = history.dropLast(1).lastOrNull()
                if (last != null) {
                    append("→ $last\n")
                    run(last)
                } else append("Geçmiş boş.\n")
            }
            "as" -> runAs(parts.drop(1))
            else -> {
                // Python CLI'de çoğu şey `as` altındadır
                append("ast: '$head' bulunamadı. `as help` veya `help` yazın.\n")
            }
        }
    }

    private fun runAs(args: List<String>) {
        if (args.isEmpty()) {
            append("Kullanım: as <eylem> ...  |  as help\n")
            return
        }
        val eylem = args[0].lowercase(Locale.ROOT)
        val rest = args.drop(1)
        when (eylem) {
            "help", "--help", "-h" -> help()
            "pwd" -> append(cwd.absolutePath + "\n")
            "cd" -> cmdCd(rest.getOrNull(0))
            "ls" -> cmdLs(rest)
            "tree" -> cmdTree(rest.getOrNull(0))
            "read", "cat" -> cmdCat(rest.getOrNull(0))
            "info" -> cmdInfo(rest.getOrNull(0))
            "mkdir" -> cmdMkdir(rest.getOrNull(0))
            "touch" -> cmdTouch(rest.getOrNull(0))
            "rm", "delete" -> cmdRm(rest.getOrNull(0))
            "clear" -> append("\u0000CLEAR\n")
            "history" -> {
                if (rest.firstOrNull() == "-clear") {
                    history.clear()
                    append("Geçmiş temizlendi.\n")
                } else {
                    history.forEachIndexed { i, h -> append("${i + 1}  $h\n") }
                }
            }
            "sys" -> cmdSys(rest)
            "desktop" -> cmdDesktop(rest)
            "android" -> cmdAndroid(rest)
            "list" -> {
                if (rest.firstOrNull() == "-libraries") {
                    append("(Android derlemesi) Kütüphane listesi: çekirdek AST motoru gömülü.\n")
                } else append("Kullanım: as list -libraries\n")
            }
            "version", "--version" -> append("AstraSage AST ${versionString()}\n")
            else -> append("[HATA] '$eylem' geçersiz bir as eylemi. as help\n")
        }
    }

    private fun help() {
        append(
            """
            |AstraSage AST — komut özeti (Python CLI uyumlu)
            |
            |  as help
            |  as pwd
            |  as cd [yol|..]
            |  as ls [yol]
            |  as tree [yol]
            |  as read <dosya>
            |  as info <yol>
            |  as mkdir <ad>
            |  as touch <ad>
            |  as rm <ad>
            |  as history [-clear]
            |  as sys -neofetch
            |  as sys --version
            |  as sys --distro
            |  as desktop [list|current|switch <name>]
            |  as android -vibrate
            |  as list -libraries
            |  as clear
            |
            |Kısa yollar: help pwd ls cd cat mkdir touch rm df neofetch apps clear exit
            |
            """.trimMargin()
        )
    }

    private fun cmdCd(target: String?) {
        if (target.isNullOrBlank()) {
            append("Şu an: ${cwd.absolutePath}\n")
            return
        }
        val yeni = when {
            target == "~" || target == "\$HOME" -> RealFs.home()
            target == ".." -> cwd.parentFile ?: cwd
            target.startsWith("/") -> File(target)
            else -> File(cwd, target)
        }
        if (!yeni.isDirectory || !yeni.canRead()) {
            append("[HATA] '$target' klasörü bulunamadı.\n")
            return
        }
        cwd = yeni
        append("→ ${cwd.absolutePath}\n")
    }

    private fun cmdLs(args: List<String>) {
        val hedef = if (args.isNotEmpty()) resolve(args[0]) else cwd
        if (!hedef.isDirectory) {
            append("[HATA] dizin değil: ${hedef.absolutePath}\n")
            return
        }
        val list = RealFs.list(hedef)
        if (list.isEmpty()) {
            append("(boş klasör)\n")
            return
        }
        list.sortedBy { it.name.lowercase(Locale.ROOT) }.forEach { f ->
            if (f.isDirectory) append("[K] ${f.name}/\n") else append("    ${f.name}\n")
        }
    }

    private fun cmdTree(path: String?) {
        val root = if (path != null) resolve(path) else cwd
        fun walk(dir: File, prefix: String, depth: Int) {
            if (depth > 4) return
            val kids = RealFs.list(dir).sortedBy { it.name }
            kids.forEachIndexed { i, f ->
                val last = i == kids.lastIndex
                val branch = if (last) "└── " else "├── "
                append("$prefix$branch${f.name}${if (f.isDirectory) "/" else ""}\n")
                if (f.isDirectory) walk(f, prefix + if (last) "    " else "│   ", depth + 1)
            }
        }
        append("${root.name}/\n")
        walk(root, "", 0)
    }

    private fun cmdCat(name: String?) {
        if (name.isNullOrBlank()) {
            append("Kullanım: as read <dosya>\n")
            return
        }
        val f = resolve(name)
        when {
            !f.exists() -> append("[HATA] dosya yok\n")
            f.isDirectory -> append("cat: klasör\n")
            else -> {
                try {
                    val text = f.readText()
                    append(if (text.endsWith("\n")) text else text + "\n")
                } catch (e: Exception) {
                    append("[HATA] ${e.message}\n")
                }
            }
        }
    }

    private fun cmdInfo(name: String?) {
        if (name.isNullOrBlank()) {
            append("Kullanım: as info <yol>\n")
            return
        }
        val f = resolve(name)
        if (!f.exists()) {
            append("[HATA] yok\n")
            return
        }
        append("Ad     : ${f.name}\n")
        append("Yol    : ${f.absolutePath}\n")
        append("Tür    : ${if (f.isDirectory) "klasör" else "dosya"}\n")
        append("Boyut  : ${if (f.isFile) RealFs.formatSize(f.length()) else "-"}\n")
        append("Okunur : ${f.canRead()}  Yazılır: ${f.canWrite()}\n")
    }

    private fun cmdMkdir(name: String?) {
        if (name.isNullOrBlank()) {
            append("Kullanım: as mkdir <ad>\n")
            return
        }
        val f = resolve(name)
        if (f.mkdirs()) append("Oluşturuldu: ${f.absolutePath}\n")
        else append("[HATA] klasör oluşturulamadı\n")
    }

    private fun cmdTouch(name: String?) {
        if (name.isNullOrBlank()) {
            append("Kullanım: as touch <ad>\n")
            return
        }
        val f = resolve(name)
        try {
            if (!f.exists()) f.writeText("")
            else f.setLastModified(System.currentTimeMillis())
            append("OK: ${f.name}\n")
        } catch (e: Exception) {
            append("[HATA] ${e.message}\n")
        }
    }

    private fun cmdRm(name: String?) {
        if (name.isNullOrBlank()) {
            append("Kullanım: as rm <ad>\n")
            return
        }
        val f = resolve(name)
        if (!f.exists()) {
            append("[HATA] yok\n")
            return
        }
        val ok = if (f.isDirectory) f.deleteRecursively() else f.delete()
        append(if (ok) "Silindi: ${f.name}\n" else "[HATA] silinemedi\n")
    }

    private fun cmdDf() {
        try {
            val path = Environment.getExternalStorageDirectory().absolutePath
            val st = StatFs(path)
            append("Dosya sistemi: $path\n")
            append("Toplam : ${RealFs.formatSize(st.totalBytes)}\n")
            append("Boş    : ${RealFs.formatSize(st.availableBytes)}\n")
        } catch (e: Exception) {
            append("[HATA] ${e.message}\n")
        }
    }

    private fun cmdSys(args: List<String>) {
        if (args.isEmpty()) {
            append("Kullanım: as sys -neofetch | --version | --distro\n")
            return
        }
        when (args[0]) {
            "-neofetch", "neofetch" -> cmdNeofetch()
            "--version", "-version" -> append("AstraSage ${versionString()}\n")
            "--distro" -> append("AstraSage Dağıtımı: AstraSage OS (Android)\n")
            else -> append("[HATA] as sys ${args[0]}\n")
        }
    }

    private fun cmdNeofetch() {
        // Compact neofetch-style block (Python utils/neofetch çıktısına yakın)
        val ascii = """
            |  /@@@@@@@@@@@@@@@@@@@@@@@@\ 
            |  @K@@@@@@@@@@@@@@@@@@@M@@@ 
            |  @@@@@@@@@@@M@@@@@@@@@@@@ 
            |  @@M@@@@@@@@@@@@@@@@@@@|  
            |  @@@@@@@@@@@A@@@@@@@@@@@  
            |  @@@@@@M@@@@@@@@@@@@@@@[S 
            |  @@@@B@@@@@M@@@@@@@MM@@@] 
            |  @M@@@@@     0@@@@M@@@@@@ 
            |  g@@@@@@   .   @@@@@@@@M@ 
            |  @@M@@@    @   `@@@@"     
            |  @@@@@/   @@p   \@@   .@g 
            |  M@@@F           V@\      
            |  @@@D    _____    @BBBg__ 
            |  @@@    @@@@@@@       `"" 
            |  @@L___g@@@@@@@b___[g____ 
            |  @@@@@M@@@@@@M@@@@@@@BBP  
            |  \P@MM@@@@@@@M@@@M@@@BB@@ 
            """.trimMargin()
        append(ascii + "\n")
        val user = Prefs.getUser(context).ifBlank { "user" }
        append("$user@astrasage\n")
        append("───────────────\n")
        append("OS     : AstraSage OS (Android)\n")
        append("Host   : ${Build.MANUFACTURER} ${Build.MODEL}\n")
        append("Kernel : Android ${Build.VERSION.RELEASE} (API ${Build.VERSION.SDK_INT})\n")
        append("DE     : ${DesktopManager.current().displayName}\n")
        append("Shell  : AST (Python-uyumlu)\n")
        append("Term   : AstraSage Terminal\n")
        try {
            val st = StatFs(Environment.getExternalStorageDirectory().absolutePath)
            val used = st.totalBytes - st.availableBytes
            append("Disk   : ${RealFs.formatSize(used)} / ${RealFs.formatSize(st.totalBytes)}\n")
        } catch (_: Exception) {
        }
        append("Uptime : ${SimpleDateFormat("HH:mm:ss", Locale.getDefault()).format(Date())}\n")
    }

    private fun cmdDesktop(args: List<String>) {
        when {
            args.isEmpty() || args[0] == "list" -> {
                append("Desktop Environments:\n")
                append(DesktopManager.listStatus() + "\n")
            }
            args[0] == "current" ->
                append("Current Desktop Environment: ${DesktopManager.current().displayName}\n")
            args[0] == "switch" && args.size >= 2 -> {
                val name = args.drop(1).joinToString(" ")
                append("Switching Desktop Environment...\n")
                val de = DesktopManager.switchTo(context, name)
                append("${de.displayName} loaded.\n")
            }
            else -> append("usage: as desktop [list|current|switch <name>]\n")
        }
    }

    private fun cmdAndroid(args: List<String>) {
        when (args.firstOrNull()) {
            "-vibrate", "vibrate" -> {
                try {
                    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
                        context.getSystemService(VibratorManager::class.java)
                            ?.defaultVibrator
                            ?.vibrate(VibrationEffect.createOneShot(40, VibrationEffect.DEFAULT_AMPLITUDE))
                    } else {
                        @Suppress("DEPRECATION")
                        (context.getSystemService(Context.VIBRATOR_SERVICE) as Vibrator)
                            .vibrate(VibrationEffect.createOneShot(40, VibrationEffect.DEFAULT_AMPLITUDE))
                    }
                    append("OK vibrate\n")
                } catch (e: Exception) {
                    append("[HATA] ${e.message}\n")
                }
            }
            "-notify" -> append("(Android) Bildirim: AST — komut alındı\n")
            else -> append("Kullanım: as android -vibrate | -notify\n")
        }
    }

    private fun cmdApps() {
        val apps = AppRepository.loadLauncherApps(context.packageManager)
        append("Yüklü uygulamalar: ${apps.size}\n")
        apps.take(40).forEach { append("  ${it.label}  (${it.packageName})\n") }
        if (apps.size > 40) append("  ... +${apps.size - 40} daha\n")
    }

    private fun resolve(path: String): File =
        when {
            path.startsWith("/") -> File(path)
            path.startsWith("~") -> File(RealFs.home(), path.removePrefix("~").removePrefix("/"))
            else -> File(cwd, path)
        }

    private fun versionString() = "2.0.0-android"
}
