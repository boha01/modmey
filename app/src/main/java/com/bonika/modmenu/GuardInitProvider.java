package com.bonika.modmenu;

import android.content.ContentProvider;
import android.content.ContentValues;
import android.database.Cursor;
import android.net.Uri;

/**
 * Stub for the game's original GuardInitProvider (com.mycompany.application.GuardInitProvider).
 * If the real SilkGuard provider is available as an AAR, put it in app/libs/ and
 * change android:name back to com.mycompany.application.GuardInitProvider in the manifest.
 */
public class GuardInitProvider extends ContentProvider {
    @Override public boolean onCreate() { return true; }
    @Override public Cursor query(Uri u, String[] p, String s, String[] a, String o) { return null; }
    @Override public String getType(Uri u) { return null; }
    @Override public Uri insert(Uri u, ContentValues v) { return null; }
    @Override public int delete(Uri u, String s, String[] a) { return 0; }
    @Override public int update(Uri u, ContentValues v, String s, String[] a) { return 0; }
}
