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
        prefs(ctx).edit().remove("user").remove("pass").putBoolean("setup_done", false).apply()

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

    fun getIconPositions(ctx: Context) = prefs(ctx).getString("positions", "{}") ?: "{}"
    fun setIconPositions(ctx: Context, json: String) =
        prefs(ctx).edit().putString("positions", json).apply()
}
