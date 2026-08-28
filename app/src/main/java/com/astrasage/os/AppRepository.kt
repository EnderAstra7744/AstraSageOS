package com.astrasage.os

import android.content.Intent
import android.content.pm.PackageManager
import android.content.pm.ResolveInfo
import java.util.Locale

/**
 * Loads every app that appears on the Android launcher,
 * with the exact icon and label the system uses.
 */
object AppRepository {

    fun loadLauncherApps(pm: PackageManager): List<AppInfo> {
        val main = Intent(Intent.ACTION_MAIN).apply {
            addCategory(Intent.CATEGORY_LAUNCHER)
        }

        val resolved: List<ResolveInfo> = pm.queryIntentActivities(main, 0)

        return resolved
            .mapNotNull { info ->
                val activity = info.activityInfo ?: return@mapNotNull null
                val label = info.loadLabel(pm)?.toString()?.trim().orEmpty()
                if (label.isEmpty()) return@mapNotNull null

                val icon = try {
                    info.loadIcon(pm)
                } catch (_: Exception) {
                    pm.defaultActivityIcon
                }

                AppInfo(
                    label = label,
                    packageName = activity.packageName,
                    activityName = activity.name,
                    icon = icon
                )
            }
            .distinctBy { "${it.packageName}/${it.activityName}" }
            .sortedBy { it.label.lowercase(Locale.getDefault()) }
    }

    fun filter(apps: List<AppInfo>, query: String): List<AppInfo> {
        val q = query.trim().lowercase(Locale.getDefault())
        if (q.isEmpty()) return apps
        return apps.filter {
            it.label.lowercase(Locale.getDefault()).contains(q) ||
                it.packageName.lowercase(Locale.getDefault()).contains(q)
        }
    }
}
