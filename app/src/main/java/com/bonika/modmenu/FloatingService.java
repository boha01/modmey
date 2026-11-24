package com.bonika.modmenu;

import android.app.*;
import android.content.Intent;
import android.content.pm.ServiceInfo;
import android.os.*;

/** Floating Service — hosts the smooth hardware-accelerated MenuOverlay. */
public class FloatingService extends Service {
    private MenuOverlay overlay;

    @Override public void onCreate() {
        super.onCreate();
        if (Build.VERSION.SDK_INT >= 26) {
            NotificationChannel ch = new NotificationChannel("modmenu", "ModMenu",
                    NotificationManager.IMPORTANCE_LOW);
            NotificationManager nm = (NotificationManager) getSystemService(NOTIFICATION_SERVICE);
            if (nm != null) nm.createNotificationChannel(ch);
            Notification notif = new Notification.Builder(this, "modmenu")
                    .setContentTitle("Mod Menu")
                    .setSmallIcon(android.R.drawable.ic_menu_compass)
                    .build();
            try {
                if (Build.VERSION.SDK_INT >= 34) {
                    startForeground(1337, notif, ServiceInfo.FOREGROUND_SERVICE_TYPE_SPECIAL_USE);
                } else {
                    startForeground(1337, notif);
                }
            } catch (Throwable ignored) {
                try { startForeground(1337, notif); } catch (Throwable ignored2) {}
            }
        }
        if (overlay == null) {
            overlay = new MenuOverlay(this);
            overlay.show();
        }
    }

    @Override public int onStartCommand(Intent i, int f, int id) {
        return START_STICKY;
    }

    @Override public void onDestroy() {
        if (overlay != null) {
            overlay.hide();
            overlay = null;
        }
        super.onDestroy();
    }

    @Override public IBinder onBind(Intent i) { return null; }
}
