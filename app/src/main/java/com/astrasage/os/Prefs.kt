package com.astrasage.os

import android.content.Context

object Prefs {
    private const val NAME = "astrasage_os"

    fun prefs(ctx: Context) = ctx.getSharedPreferences(NAME, Context.MODE_PRIVATE)

    fun isSetupDone(ctx: Context) = prefs(ctx).getBoolean("setup_done", false)
    fun setSetupDone(ctx: Context, v: Boolean) = prefs(ctx).edit().putBoolean("setup_done", v).apply()

    fun getLang(ctx: Context) = prefs(ctx).getString("lang", "tr") ?: "tr"
    fun setLang(ctx: Context, lang: String) = prefs(ctx).edit().putString("lang", lang).apply()

    fun getUser(ctx: Context) = prefs(ctx).getString("user", "") ?: ""
    fun getPass(ctx: Context) = prefs(ctx).getString("pass", "") ?: ""
    fun setAccount(ctx: Context, user: String, pass: String) =
        prefs(ctx).edit().putString("user", user).putString("pass", pass).putBoolean("setup_done", true).apply()

    fun clearAccount(ctx: Context) =
        prefs(ctx).edit()
            .remove("user").remove("pass")
            .putBoolean("setup_done", false)
            .putBoolean("welcome_done", false)
            .apply()

    fun isWelcomeDone(ctx: Context) = prefs(ctx).getBoolean("welcome_done", false)
    fun setWelcomeDone(ctx: Context, v: Boolean) = prefs(ctx).edit().putBoolean("welcome_done", v).apply()

    fun isDarkTheme(ctx: Context) = prefs(ctx).getBoolean("dark_theme", true)
    fun setDarkTheme(ctx: Context, v: Boolean) = prefs(ctx).edit().putBoolean("dark_theme", v).apply()

    fun getFontScale(ctx: Context) = prefs(ctx).getFloat("font_scale", 1.0f)
    fun getIconScale(ctx: Context) = prefs(ctx).getFloat("icon_scale", 1.0f)
    fun setIconScale(ctx: Context, v: Float) = prefs(ctx).edit().putFloat("icon_scale", v).apply()
    fun showIconNames(ctx: Context) = prefs(ctx).getBoolean("show_names", true)
    fun setShowIconNames(ctx: Context, v: Boolean) = prefs(ctx).edit().putBoolean("show_names", v).apply()
    fun setFontScale(ctx: Context, v: Float) = prefs(ctx).edit().putFloat("font_scale", v).apply()

    fun getDesktopPins(ctx: Context): Set<String> =
        prefs(ctx).getStringSet("desktop_pins", emptySet()) ?: emptySet()

    fun addDesktopPin(ctx: Context, path: String) {
        val set = getDesktopPins(ctx).toMutableSet()
        set.add(path)
        prefs(ctx).edit().putStringSet("desktop_pins", set).apply()
    }

    fun removeDesktopPin(ctx: Context, path: String) {
        val set = getDesktopPins(ctx).toMutableSet()
        set.remove(path)
        prefs(ctx).edit().putStringSet("desktop_pins", set).apply()
    }

    /** Hidden from desktop only (recycle bin) — not deleted from phone */
    fun getRecycle(ctx: Context): Set<String> =
        prefs(ctx).getStringSet("recycle_bin", emptySet()) ?: emptySet()

    fun moveToRecycle(ctx: Context, key: String) {
        val rec = getRecycle(ctx).toMutableSet()
        rec.add(key)
        val pins = getDesktopPins(ctx).toMutableSet()
        if (key.startsWith("file:")) pins.remove(key.removePrefix("file:"))
        prefs(ctx).edit()
            .putStringSet("recycle_bin", rec)
            .putStringSet("desktop_pins", pins)
            .apply()
    }

    fun restoreFromRecycle(ctx: Context, key: String) {
        val rec = getRecycle(ctx).toMutableSet()
        rec.remove(key)
        if (key.startsWith("file:")) {
            val pins = getDesktopPins(ctx).toMutableSet()
            pins.add(key.removePrefix("file:"))
            prefs(ctx).edit().putStringSet("recycle_bin", rec).putStringSet("desktop_pins", pins).apply()
        } else {
            prefs(ctx).edit().putStringSet("recycle_bin", rec).apply()
        }
    }

    fun emptyRecycle(ctx: Context) =
        prefs(ctx).edit().putStringSet("recycle_bin", emptySet()).apply()

    fun getPinnedApps(ctx: Context): Set<String> =
        prefs(ctx).getStringSet("pinned_apps", emptySet()) ?: emptySet()

    fun pinApp(ctx: Context, packageActivity: String) {
        val s = getPinnedApps(ctx).toMutableSet()
        s.add(packageActivity)
        // also remove from recycle if present
        val rec = getRecycle(ctx).toMutableSet()
        rec.remove("app:$packageActivity")
        prefs(ctx).edit().putStringSet("pinned_apps", s).putStringSet("recycle_bin", rec).apply()
    }

    fun unpinApp(ctx: Context, packageActivity: String) {
        val s = getPinnedApps(ctx).toMutableSet()
        s.remove(packageActivity)
        prefs(ctx).edit().putStringSet("pinned_apps", s).apply()
    }

    fun getHiddenApps(ctx: Context): Set<String> =
        prefs(ctx).getStringSet("hidden_apps", emptySet()) ?: emptySet()

    fun hideApp(ctx: Context, packageActivity: String) {
        val s = getHiddenApps(ctx).toMutableSet()
        s.add(packageActivity)
        val rec = getRecycle(ctx).toMutableSet()
        rec.add("app:$packageActivity")
        prefs(ctx).edit().putStringSet("hidden_apps", s).putStringSet("recycle_bin", rec).apply()
    }

    fun getIconPositions(ctx: Context) = prefs(ctx).getString("positions", "{}") ?: "{}"
    fun setIconPositions(ctx: Context, json: String) =
        prefs(ctx).edit().putString("positions", json).apply()
}
