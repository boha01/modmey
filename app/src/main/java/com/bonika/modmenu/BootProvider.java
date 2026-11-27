package com.bonika.modmenu;

import android.content.ContentProvider;
import android.content.ContentValues;
import android.content.Intent;
import android.database.Cursor;
import android.net.Uri;
import android.os.Build;

/**
 * Runs FIRST (initOrder=1) before any other ContentProvider, including the
 * game's GuardInitProvider (initOrder=MAX). Loads the native library (which
 * spawns hackThread and starts waiting for libdatastore_shared.so), then
 * starts the overlay service so the menu appears without needing any game
 * activity to launch first.
 */
public class BootProvider extends ContentProvider {
    @Override public boolean onCreate() {
        try {
            NativeBridge.load();
            Intent svc = new Intent(getContext(), FloatingService.class);
            if (Build.VERSION.SDK_INT >= 26) getContext().startForegroundService(svc);
            else getContext().startService(svc);
        } catch (Throwable ignored) {}
        return true;
    }
    @Override public Cursor query(Uri u, String[] p, String s, String[] a, String o) { return null; }
    @Override public String getType(Uri u) { return null; }
    @Override public Uri insert(Uri u, ContentValues v) { return null; }
    @Override public int delete(Uri u, String s, String[] a) { return 0; }
    @Override public int update(Uri u, ContentValues v, String s, String[] a) { return 0; }
}
