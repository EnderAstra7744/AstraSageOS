package com.astrasage.os

import android.graphics.drawable.Drawable

/**
 * Represents a real installed launcher application on the device.
 * Icon and label come from PackageManager — same as the phone home screen.
 */
data class AppInfo(
    val label: String,
    val packageName: String,
    val activityName: String,
    val icon: Drawable
)
