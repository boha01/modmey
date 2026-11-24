package com.mycompany.application;

import android.content.Context;
import android.os.Handler;
import android.os.Looper;

/**
 * Compatibility bridge for ChillbaseActivity.onCreate -> MainActivity.Start(Context).
 * Replaces SPINOX / datastore_shared loader: invokes SilkGuard signature spoofer
 * (if classes6.dex is present) and launches com.bonika.modmenu on the main looper.
 */
public class MainActivity {
    public static void Start(final Context ctx) {
        if (ctx == null) return;
        try {
            Class<?> sg = Class.forName("com.silk.guard.SilkGuard");
            sg.getMethod("install", Context.class).invoke(null, ctx);
        } catch (Throwable ignored) {}

        final Context appCtx = ctx.getApplicationContext() != null ? ctx.getApplicationContext() : ctx;
        new Handler(Looper.getMainLooper()).postDelayed(new Runnable() {
            @Override public void run() {
                com.bonika.modmenu.MainActivity.startMenu(ctx);
                // Ensure NativeBridge class is initialized even if overlay permission is pending
                try {
                    com.bonika.modmenu.NativeBridge.isLibraryLoaded();
                } catch (Throwable ignored) {
                    try { com.bonika.modmenu.MainActivity.startMenu(appCtx); } catch (Throwable ignored2) {}
                }
            }
        }, 600);
    }
}
