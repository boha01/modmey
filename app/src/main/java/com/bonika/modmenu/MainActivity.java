package com.bonika.modmenu;

import android.app.*;
import android.content.Intent;
import android.net.Uri;
import android.os.*;
import android.provider.Settings;
import android.widget.Toast;

/**
 * MainActivity — entry point when launched standalone or via startMenu(Context).
 */
public class MainActivity extends Activity {
    @Override protected void onCreate(Bundle b) {
        super.onCreate(b);
        if (Build.VERSION.SDK_INT >= 23 && !Settings.canDrawOverlays(this)) {
            Toast.makeText(this, "Allow display over other apps", Toast.LENGTH_LONG).show();
            try {
                startActivity(new Intent(Settings.ACTION_MANAGE_OVERLAY_PERMISSION,
                        Uri.parse("package:" + getPackageName())));
            } catch (Throwable ignored) {}
            return;
        }
        startMenu(this);
        finish();
    }

    public static void startMenu(final android.content.Context ctx) {
        if (ctx == null) return;
        if (Build.VERSION.SDK_INT >= 23 && !Settings.canDrawOverlays(ctx)) {
            try {
                Intent perm = new Intent(Settings.ACTION_MANAGE_OVERLAY_PERMISSION,
                        Uri.parse("package:" + ctx.getPackageName()));
                perm.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
                ctx.startActivity(perm);
            } catch (Throwable ignored) {}
            return;
        }
        try {
            Intent i = new Intent(ctx, FloatingService.class);
            if (Build.VERSION.SDK_INT >= 26) ctx.startForegroundService(i);
            else ctx.startService(i);
        } catch (Throwable ignored) {}
    }
}
