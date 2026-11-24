package com.bonika.modmenu;

import android.animation.Animator;
import android.animation.AnimatorListenerAdapter;
import android.animation.ValueAnimator;
import android.app.Service;
import android.content.*;
import android.graphics.*;
import android.graphics.drawable.*;
import android.os.Build;
import android.os.Handler;
import android.os.Looper;
import android.text.*;
import android.text.style.*;
import android.view.*;
import android.view.animation.DecelerateInterpolator;
import android.view.animation.OvershootInterpolator;
import android.widget.*;

/**
 * MenuOverlay v7 — boha xvip (Ultra-smooth animations, throttled zero-lag drag,
 * hardware layers, crash-safe JNI calls, full libdatastore_shared hook toggles)
 */
public class MenuOverlay {
    static final int BG = 0xF50A0A0C, PANEL = 0xFF111114, SIDEBAR = 0xFF0D0D10,
            CARD = 0xFF17171B, STROKE = 0xFF26262C, TRACK = 0xFF2A2A30,
            RED = 0xFFE8112D, RED2 = 0xFFFF1E3C, TEXT = 0xFFFFFFFF,
            MUTED = 0xFF9A9AA3, GREEN = 0xFF3BFF7A;

    private final Context ctx;
    private final WindowManager wm;
    private final SharedPreferences prefs;
    private final float density;
    private final int screenW, screenH;
    private final Handler uiHandler = new Handler(Looper.getMainLooper());

    private WindowManager.LayoutParams params, bubbleParams;
    private FrameLayout root;
    private LinearLayout body;
    private View bubble;
    private TextView statusLabel;

    private boolean haptic = true, bubbleAdded = false, removing = false, isOpen = true;
    private int animToken = 0;
    private ValueAnimator snapAnimator;
    private int activeTab = 0;

    private final Runnable statusTicker = new Runnable() {
        @Override public void run() {
            if (!isOpen || removing || root == null || root.getVisibility() != View.VISIBLE) return;
            updateStatusText();
            uiHandler.postDelayed(this, 1500);
        }
    };

    public MenuOverlay(Context c) {
        ctx = c;
        wm = (WindowManager) c.getSystemService(Context.WINDOW_SERVICE);
        prefs = c.getSharedPreferences("modmenu", Context.MODE_PRIVATE);
        density = c.getResources().getDisplayMetrics().density;
        screenW = c.getResources().getDisplayMetrics().widthPixels;
        screenH = c.getResources().getDisplayMetrics().heightPixels;
        haptic = prefs.getBoolean("haptic", true);
        buildRoot();
        buildBubble();
    }

    private int dp(float v) { return (int) (v * density + 0.5f); }
    private int clamp(int v, int lo, int hi) { return Math.max(lo, Math.min(hi, v)); }

    private int overlayType() {
        return Build.VERSION.SDK_INT >= 26 ? WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY
                                           : WindowManager.LayoutParams.TYPE_PHONE;
    }

    private int baseFlags() {
        int flags = WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE
                  | WindowManager.LayoutParams.FLAG_NOT_TOUCH_MODAL
                  | WindowManager.LayoutParams.FLAG_HARDWARE_ACCELERATED;
        if (prefs.getBoolean("hideRec", false)) {
            flags |= WindowManager.LayoutParams.FLAG_SECURE;
        }
        return flags;
    }

    public void show() {
        int w = prefs.getInt("menuW", dp(460));
        int h = prefs.getInt("menuH", dp(305));
        w = clamp(w, dp(310), Math.max(dp(320), screenW - dp(16)));
        h = clamp(h, dp(220), Math.max(dp(240), screenH - dp(24)));
        params = new WindowManager.LayoutParams(w, h, overlayType(), baseFlags(), PixelFormat.TRANSLUCENT);
        params.gravity = Gravity.TOP | Gravity.START;
        params.x = clampDragX(prefs.getInt("menuX", dp(16)), w, false);
        params.y = clampDragY(prefs.getInt("menuY", dp(48)));
        wm.addView(root, params);
        isOpen = true;
        animateMenuIn();
        startStatusTicker();
    }

    public void hide() {
        stopStatusTicker();
        cancelAnims();
        try { wm.removeViewImmediate(root); } catch (Throwable ignored) {
            try { wm.removeView(root); } catch (Throwable ignored2) {}
        }
        try {
            if (bubble != null && bubbleAdded) wm.removeViewImmediate(bubble);
        } catch (Throwable ignored) {
            try { if (bubble != null) wm.removeView(bubble); } catch (Throwable ignored2) {}
        }
        bubbleAdded = false;
    }

    private void removeCompletely() {
        if (removing) return;
        removing = true;
        stopStatusTicker();
        cancelAnims();
        root.setLayerType(View.LAYER_TYPE_HARDWARE, null);
        root.animate().alpha(0f).scaleX(0.92f).scaleY(0.92f)
                .setDuration(140)
                .setInterpolator(new DecelerateInterpolator())
                .withEndAction(() -> {
                    root.setLayerType(View.LAYER_TYPE_NONE, null);
                    hide();
                    if (ctx instanceof Service) ((Service) ctx).stopSelf();
                    else try { ctx.stopService(new Intent(ctx, FloatingService.class)); } catch (Throwable ignored) {}
                }).start();
    }

    private void cancelAnims() {
        animToken++;
        if (snapAnimator != null) { snapAnimator.cancel(); snapAnimator = null; }
        if (root != null) root.animate().cancel();
        if (bubble != null) bubble.animate().cancel();
    }

    private void animateMenuIn() {
        final int token = ++animToken;
        final float targetAlpha = prefs.getFloat("opacity", 1f);
        root.setVisibility(View.VISIBLE);
        root.setLayerType(View.LAYER_TYPE_HARDWARE, null);
        root.setAlpha(0f);
        root.setScaleX(0.92f);
        root.setScaleY(0.92f);
        root.animate()
                .alpha(targetAlpha).scaleX(1f).scaleY(1f)
                .setDuration(170)
                .setInterpolator(new DecelerateInterpolator(1.6f))
                .withEndAction(() -> {
                    if (animToken == token && root != null) root.setLayerType(View.LAYER_TYPE_NONE, null);
                }).start();
    }

    private void applySecurityFlag() {
        if (params == null || root == null) return;
        params.flags = baseFlags();
        try { wm.updateViewLayout(root, params); } catch (Throwable ignored) {}
        if (bubbleParams != null && bubble != null && bubbleAdded) {
            bubbleParams.flags = baseFlags();
            try { wm.updateViewLayout(bubble, bubbleParams); } catch (Throwable ignored) {}
        }
    }

    private void startStatusTicker() {
        uiHandler.removeCallbacks(statusTicker);
        updateStatusText();
        uiHandler.postDelayed(statusTicker, 1500);
    }

    private void stopStatusTicker() {
        uiHandler.removeCallbacks(statusTicker);
    }

    private void updateStatusText() {
        if (statusLabel == null) return;
        try {
            if (!NativeBridge.isLibraryLoaded()) {
                statusLabel.setText("Status : Native lib not loaded");
                return;
            }
            int inst = NativeBridge.getHooksInstalled();
            int tot = NativeBridge.getHooksTotal();
            int flt = NativeBridge.getFaultsRecovered();
            if (inst > 0) {
                statusLabel.setText(flt > 0
                        ? String.format("Status : Ready (%d/%d hooks | %d shielded)", inst, tot, flt)
                        : String.format("Status : Ready (%d/%d hooks)", inst, tot));
            } else {
                statusLabel.setText("Status : Waiting for libil2cpp…");
            }
        } catch (Throwable ignored) {}
    }

    private GradientDrawable panelBg() {
        GradientDrawable d = new GradientDrawable(GradientDrawable.Orientation.LEFT_RIGHT,
                new int[]{BG, 0xF51C060B});
        d.setCornerRadius(dp(16));
        d.setStroke(Math.max(1, dp(1)), RED);
        return d;
    }

    private GradientDrawable round(int color, float radiusDp, int strokeColor, float strokeDp) {
        GradientDrawable d = new GradientDrawable();
        d.setColor(color);
        d.setCornerRadius(dp(radiusDp));
        if (strokeColor != 0) d.setStroke(Math.max(1, dp(strokeDp)), strokeColor);
        return d;
    }

    private GradientDrawable gradient(int from, int to, float radiusDp) {
        GradientDrawable d = new GradientDrawable(GradientDrawable.Orientation.LEFT_RIGHT, new int[]{from, to});
        d.setCornerRadius(dp(radiusDp));
        return d;
    }

    private GradientDrawable oval(int color) {
        GradientDrawable d = new GradientDrawable();
        d.setShape(GradientDrawable.OVAL);
        d.setColor(color);
        return d;
    }

    private void click(View v) { if (haptic && v != null) v.performHapticFeedback(HapticFeedbackConstants.VIRTUAL_KEY); }

    private TextView label(String s, float sp, int color) {
        TextView t = new TextView(ctx);
        t.setText(s);
        t.setTextSize(sp);
        t.setTextColor(color);
        return t;
    }

    private TextView twoTone(String first, String second, float sp) {
        SpannableStringBuilder sb = new SpannableStringBuilder(first + second);
        sb.setSpan(new ForegroundColorSpan(Color.WHITE), 0, first.length(), Spanned.SPAN_EXCLUSIVE_EXCLUSIVE);
        sb.setSpan(new StyleSpan(Typeface.BOLD), 0, first.length(), Spanned.SPAN_EXCLUSIVE_EXCLUSIVE);
        sb.setSpan(new ForegroundColorSpan(RED2), first.length(), sb.length(), Spanned.SPAN_EXCLUSIVE_EXCLUSIVE);
        sb.setSpan(new StyleSpan(Typeface.BOLD), first.length(), sb.length(), Spanned.SPAN_EXCLUSIVE_EXCLUSIVE);
        TextView t = new TextView(ctx);
        t.setText(sb);
        t.setTextSize(sp);
        return t;
    }

    private View logoView(int sizeDp) {
        int resId = 0;
        try { resId = ctx.getResources().getIdentifier("boha_logo", "drawable", ctx.getPackageName()); }
        catch (Exception ignored) {}
        if (resId != 0) {
            ImageView iv = new ImageView(ctx);
            iv.setImageResource(resId);
            iv.setScaleType(ImageView.ScaleType.CENTER_CROP);
            iv.setClipToOutline(true);
            iv.setOutlineProvider(new ViewOutlineProvider() {
                @Override public void getOutline(View v, Outline o) { o.setOval(0, 0, v.getWidth(), v.getHeight()); }
            });
            iv.setBackground(oval(0xFF000000));
            return iv;
        }
        TextView t = label("B", 17, Color.WHITE);
        t.setTypeface(Typeface.DEFAULT_BOLD);
        t.setGravity(Gravity.CENTER);
        GradientDrawable d = gradient(RED, 0xFF5A0A14, sizeDp / 2f);
        d.setShape(GradientDrawable.OVAL);
        d.setStroke(Math.max(1, dp(1)), RED2);
        t.setBackground(d);
        return t;
    }

    private void buildRoot() {
        root = new FrameLayout(ctx);
        root.setBackground(panelBg());

        LinearLayout panel = new LinearLayout(ctx);
        panel.setOrientation(LinearLayout.VERTICAL);
        panel.setPadding(dp(12), dp(10), dp(12), dp(10));
        root.addView(panel, new FrameLayout.LayoutParams(-1, -1));

        // ----- header (drag handle) -----
        LinearLayout header = new LinearLayout(ctx);
        header.setOrientation(LinearLayout.HORIZONTAL);
        header.setGravity(Gravity.CENTER_VERTICAL);
        header.setPadding(0, 0, 0, dp(8));

        View logo = logoView(44);
        LinearLayout.LayoutParams logoLp = new LinearLayout.LayoutParams(dp(44), dp(44));
        logoLp.setMargins(0, 0, dp(10), 0);
        logo.setLayoutParams(logoLp);
        header.addView(logo);

        LinearLayout titles = new LinearLayout(ctx);
        titles.setOrientation(LinearLayout.VERTICAL);
        titles.setLayoutParams(new LinearLayout.LayoutParams(0, -2, 1f));
        LinearLayout titleRow = new LinearLayout(ctx);
        titleRow.setOrientation(LinearLayout.HORIZONTAL);
        titleRow.setGravity(Gravity.CENTER_VERTICAL);
        TextView crownIc = label("♛", 13, 0xFFFFC93B);
        LinearLayout.LayoutParams crLp = new LinearLayout.LayoutParams(-2, -2);
        crLp.setMargins(0, 0, dp(4), 0);
        crownIc.setLayoutParams(crLp);
        titleRow.addView(crownIc);
        titleRow.addView(twoTone("boha", " xvip", 18));
        titles.addView(titleRow);

        LinearLayout subRow = new LinearLayout(ctx);
        subRow.setOrientation(LinearLayout.HORIZONTAL);
        subRow.setGravity(Gravity.CENTER_VERTICAL);
        subRow.setPadding(0, dp(1), 0, 0);
        TextView mod = label("MOD MENU  ", 9, MUTED);
        mod.setTypeface(Typeface.DEFAULT_BOLD);
        TextView ver = label("v7.0 PRO", 9, RED2);
        ver.setTypeface(Typeface.DEFAULT_BOLD);
        subRow.addView(mod);
        subRow.addView(ver);
        titles.addView(subRow);
        header.addView(titles);

        View dot = new View(ctx);
        LinearLayout.LayoutParams dLp = new LinearLayout.LayoutParams(dp(7), dp(7));
        dLp.setMargins(0, 0, dp(5), 0);
        dot.setLayoutParams(dLp);
        dot.setBackground(oval(GREEN));
        TextView online = label("Online", 10, GREEN);
        LinearLayout onlineRow = new LinearLayout(ctx);
        onlineRow.setOrientation(LinearLayout.HORIZONTAL);
        onlineRow.setGravity(Gravity.CENTER_VERTICAL);
        LinearLayout.LayoutParams oLp = new LinearLayout.LayoutParams(-2, -2);
        oLp.setMargins(0, 0, dp(10), 0);
        onlineRow.setLayoutParams(oLp);
        onlineRow.addView(dot);
        onlineRow.addView(online);
        header.addView(onlineRow);

        TextView min = squareBtn("–", false);
        TextView close = squareBtn("✕", true);
        min.setOnClickListener(v -> { click(v); collapse(true); });
        close.setOnClickListener(v -> { click(v); removeCompletely(); });
        header.addView(min);
        header.addView(close);
        panel.addView(header);
        makeDraggable(header, false);

        View sep = new View(ctx);
        sep.setLayoutParams(new LinearLayout.LayoutParams(-1, Math.max(1, dp(0.7f))));
        sep.setBackgroundColor(STROKE);
        panel.addView(sep);

        // ----- middle: sidebar + content -----
        LinearLayout middle = new LinearLayout(ctx);
        middle.setOrientation(LinearLayout.HORIZONTAL);
        middle.setPadding(0, dp(8), 0, dp(8));
        middle.setLayoutParams(new LinearLayout.LayoutParams(-1, 0, 1f));
        panel.addView(middle);

        LinearLayout sidebar = new LinearLayout(ctx);
        sidebar.setOrientation(LinearLayout.VERTICAL);
        sidebar.setBackground(round(0xFF140609, 14, 0xFF2A0D13, 0.7f));
        sidebar.setPadding(dp(6), dp(8), dp(6), dp(8));
        LinearLayout.LayoutParams sbLp = new LinearLayout.LayoutParams(dp(98), -1);
        sbLp.setMargins(0, 0, dp(10), 0);
        sidebar.setLayoutParams(sbLp);
        middle.addView(sidebar);

        body = new LinearLayout(ctx);
        body.setOrientation(LinearLayout.VERTICAL);
        final ScrollView scroll = new ScrollView(ctx);
        scroll.setVerticalScrollBarEnabled(false);
        scroll.setOverScrollMode(View.OVER_SCROLL_NEVER);
        scroll.addView(body);
        scroll.setLayoutParams(new LinearLayout.LayoutParams(0, -1, 1f));
        middle.addView(scroll);

        final LinearLayout[] pages = {pageHome(), pageAim(), pageVisual(), pageMisc(), pageConfig()};
        String[] names = {"Home", "Aim", "Visual", "Move", "Config"};
        String[] icons = {"⌂", "⌖", "◉", "➤", "⚙"};
        final LinearLayout[] sideRows = new LinearLayout[names.length];
        final TextView[] sideIcons = new TextView[names.length];
        final TextView[] sideLabels = new TextView[names.length];
        for (int i = 0; i < names.length; i++) {
            final int idx = i;
            LinearLayout item = new LinearLayout(ctx);
            item.setOrientation(LinearLayout.HORIZONTAL);
            item.setGravity(Gravity.CENTER_VERTICAL);
            item.setPadding(dp(10), dp(9), dp(6), dp(9));
            LinearLayout.LayoutParams iLp = new LinearLayout.LayoutParams(-1, -2);
            iLp.setMargins(0, 0, 0, dp(4));
            item.setLayoutParams(iLp);
            TextView ic = label(icons[i], 16, MUTED);
            ic.setGravity(Gravity.CENTER);
            ic.setLayoutParams(new LinearLayout.LayoutParams(dp(24), -2));
            TextView lb = label(names[i], 12, MUTED);
            LinearLayout.LayoutParams lbLp = new LinearLayout.LayoutParams(-2, -2);
            lbLp.setMargins(dp(6), 0, 0, 0);
            lb.setLayoutParams(lbLp);
            item.addView(ic);
            item.addView(lb);
            item.setOnClickListener(v -> {
                if (activeTab == idx) return;
                activeTab = idx;
                click(v);
                scroll.scrollTo(0, 0);
                for (int j = 0; j < pages.length; j++) {
                    pages[j].animate().cancel();
                    pages[j].setVisibility(j == idx ? View.VISIBLE : View.GONE);
                    styleSide(sideRows[j], sideIcons[j], sideLabels[j], j == idx);
                }
                pages[idx].setAlpha(0f);
                pages[idx].setTranslationX(dp(6));
                pages[idx].animate().alpha(1f).translationX(0).setDuration(130)
                        .setInterpolator(new DecelerateInterpolator()).start();
            });
            sideRows[i] = item;
            sideIcons[i] = ic;
            sideLabels[i] = lb;
            sidebar.addView(item);
            body.addView(pages[i]);
            if (i > 0) pages[i].setVisibility(View.GONE);
            styleSide(item, ic, lb, i == 0);
        }

        View spacer = new View(ctx);
        spacer.setLayoutParams(new LinearLayout.LayoutParams(-1, 0, 1f));
        sidebar.addView(spacer);
        TextView crown = label("♛", 15, RED2);
        crown.setGravity(Gravity.CENTER);
        sidebar.addView(crown);
        TextView brand = twoTone("boha", " xvip", 11);
        brand.setGravity(Gravity.CENTER);
        sidebar.addView(brand);

        // ----- footer -----
        View sep2 = new View(ctx);
        sep2.setLayoutParams(new LinearLayout.LayoutParams(-1, Math.max(1, dp(0.7f))));
        sep2.setBackgroundColor(STROKE);
        panel.addView(sep2);

        LinearLayout footer = new LinearLayout(ctx);
        footer.setOrientation(LinearLayout.HORIZONTAL);
        footer.setGravity(Gravity.CENTER_VERTICAL);
        footer.setPadding(0, dp(8), dp(2), 0);
        TextView info = label("ⓘ", 14, RED2);
        LinearLayout.LayoutParams inLp = new LinearLayout.LayoutParams(-2, -2);
        inLp.setMargins(0, 0, dp(6), 0);
        info.setLayoutParams(inLp);
        statusLabel = label("Status : Initializing…", 11, MUTED);
        statusLabel.setLayoutParams(new LinearLayout.LayoutParams(0, -2, 1f));
        Button reset = new Button(ctx);
        reset.setText("🗑  Reset");
        reset.setAllCaps(false);
        reset.setTextSize(11);
        reset.setTextColor(RED2);
        reset.setTypeface(Typeface.DEFAULT_BOLD);
        reset.setBackground(round(0xFF1A0509, 10, RED, 1f));
        reset.setPadding(dp(12), 0, dp(12), 0);
        reset.setLayoutParams(new LinearLayout.LayoutParams(-2, dp(30)));
        reset.setOnClickListener(v -> {
            click(v);
            prefs.edit().clear().apply();
            Toast.makeText(ctx, "Settings reset — reopen menu", Toast.LENGTH_SHORT).show();
        });
        footer.addView(info);
        footer.addView(statusLabel);
        footer.addView(reset);
        panel.addView(footer);

        // ----- resize grip -----
        TextView grip = label("◢", 15, RED2);
        grip.setGravity(Gravity.CENTER);
        FrameLayout.LayoutParams gLp = new FrameLayout.LayoutParams(dp(30), dp(30));
        gLp.gravity = Gravity.BOTTOM | Gravity.END;
        grip.setLayoutParams(gLp);
        root.addView(grip);
        makeResizable(grip);
    }

    private void styleSide(LinearLayout row, TextView icon, TextView label, boolean active) {
        if (active) {
            row.setBackground(gradient(RED, RED2, 12));
            icon.setTextColor(Color.WHITE);
            label.setTextColor(Color.WHITE);
            label.setTypeface(Typeface.DEFAULT_BOLD);
        } else {
            row.setBackground(null);
            icon.setTextColor(MUTED);
            label.setTextColor(MUTED);
            label.setTypeface(Typeface.DEFAULT);
        }
    }

    private TextView squareBtn(String s, boolean isClose) {
        TextView t = label(s, 14, isClose ? RED2 : TEXT);
        t.setGravity(Gravity.CENTER);
        if (isClose) t.setTypeface(Typeface.DEFAULT_BOLD);
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(dp(32), dp(32));
        lp.setMargins(dp(6), 0, 0, 0);
        t.setLayoutParams(lp);
        t.setBackground(isClose ? round(0xFF2A0A10, 10, RED, 1f) : round(CARD, 10, STROKE, 0.7f));
        return t;
    }

    // ---------- bubble (collapsed state) ----------
    private void buildBubble() {
        FrameLayout b = new FrameLayout(ctx);
        View inner = logoView(56);
        b.addView(inner, new FrameLayout.LayoutParams(-1, -1));
        if (!(inner instanceof ImageView)) b.setBackground(oval(0x00000000));
        bubble = b;
        bubble.setVisibility(View.GONE);
        bubble.setOnClickListener(v -> { click(v); collapse(false); });
    }

    private void collapse(boolean hideMenu) {
        if (removing || root == null || bubble == null) return;
        cancelAnims();
        final int token = animToken;

        if (hideMenu) {
            isOpen = false;
            stopStatusTicker();
            root.setLayerType(View.LAYER_TYPE_HARDWARE, null);
            root.animate().alpha(0f).scaleX(0.90f).scaleY(0.90f)
                    .setDuration(140)
                    .setInterpolator(new DecelerateInterpolator(1.5f))
                    .withEndAction(() -> {
                        if (animToken != token) return;
                        root.setLayerType(View.LAYER_TYPE_NONE, null);
                        root.setVisibility(View.GONE);
                        root.setScaleX(1f);
                        root.setScaleY(1f);
                    }).start();

            if (!bubbleAdded) {
                bubbleParams = new WindowManager.LayoutParams(dp(56), dp(56), overlayType(), baseFlags(), PixelFormat.TRANSLUCENT);
                bubbleParams.gravity = Gravity.TOP | Gravity.START;
                bubbleParams.x = clampDragX(prefs.getInt("bubbleX", dp(14)), dp(56), true);
                bubbleParams.y = clampDragY(prefs.getInt("bubbleY", dp(120)));
                try {
                    wm.addView(bubble, bubbleParams);
                    bubbleAdded = true;
                    makeDraggable(bubble, true);
                } catch (Throwable ignored) {}
            }
            boolean stealth = prefs.getBoolean("stealth", false);
            if (!stealth && bubbleAdded) {
                bubble.setVisibility(View.VISIBLE);
                bubble.setLayerType(View.LAYER_TYPE_HARDWARE, null);
                bubble.setScaleX(0.65f);
                bubble.setScaleY(0.65f);
                bubble.setAlpha(0f);
                bubble.animate().scaleX(1f).scaleY(1f).alpha(1f)
                        .setDuration(170)
                        .setInterpolator(new OvershootInterpolator(1.1f))
                        .withEndAction(() -> {
                            if (animToken == token && bubble != null) bubble.setLayerType(View.LAYER_TYPE_NONE, null);
                        }).start();
            } else {
                bubble.setVisibility(View.GONE);
            }
        } else {
            isOpen = true;
            if (bubbleAdded) {
                bubble.setLayerType(View.LAYER_TYPE_NONE, null);
                bubble.setVisibility(View.GONE);
            }
            animateMenuIn();
            startStatusTicker();
        }
    }

    // ---------- zero-lag throttled drag ----------
    private void makeDraggable(final View v, final boolean isBubble) {
        v.setClickable(true);
        v.setOnTouchListener(new View.OnTouchListener() {
            int startX, startY;
            float touchX, touchY;
            boolean dragging = false;
            long lastUpdateNs = 0;

            public boolean onTouch(View view, MotionEvent e) {
                WindowManager.LayoutParams pr = isBubble ? bubbleParams : params;
                if (pr == null) return false;
                switch (e.getActionMasked()) {
                    case MotionEvent.ACTION_DOWN:
                        if (isBubble && snapAnimator != null) {
                            snapAnimator.cancel();
                            snapAnimator = null;
                        }
                        startX = pr.x;
                        startY = pr.y;
                        touchX = e.getRawX();
                        touchY = e.getRawY();
                        dragging = false;
                        lastUpdateNs = 0;
                        return true;
                    case MotionEvent.ACTION_MOVE: {
                        int dx = (int) (e.getRawX() - touchX);
                        int dy = (int) (e.getRawY() - touchY);
                        if (!dragging && (Math.abs(dx) + Math.abs(dy)) > dp(6)) {
                            dragging = true;
                        }
                        if (dragging) {
                            int nx = clampDragX(startX + dx, pr.width, isBubble);
                            int ny = clampDragY(startY + dy);
                            long now = System.nanoTime();
                            if ((nx != pr.x || ny != pr.y) && (now - lastUpdateNs >= 8_000_000L)) {
                                pr.x = nx;
                                pr.y = ny;
                                lastUpdateNs = now;
                                try { wm.updateViewLayout(isBubble ? bubble : root, pr); } catch (Throwable ignored) {}
                            }
                        }
                        return true;
                    }
                    case MotionEvent.ACTION_UP:
                    case MotionEvent.ACTION_CANCEL:
                        if (dragging) {
                            int dx = (int) (e.getRawX() - touchX);
                            int dy = (int) (e.getRawY() - touchY);
                            pr.x = clampDragX(startX + dx, pr.width, isBubble);
                            pr.y = clampDragY(startY + dy);
                            try { wm.updateViewLayout(isBubble ? bubble : root, pr); } catch (Throwable ignored) {}
                            dragging = false;
                            savePos(isBubble, pr);
                            if (isBubble) snapBubbleToEdge();
                            return true;
                        }
                        if (e.getActionMasked() == MotionEvent.ACTION_UP && isBubble) {
                            view.performClick();
                            return true;
                        }
                        return false;
                }
                return false;
            }
        });
    }

    private int clampDragX(int x, int w, boolean isBubble) {
        if (isBubble) return clamp(x, 0, Math.max(0, screenW - w));
        return clamp(x, -w + dp(60), Math.max(0, screenW - dp(60)));
    }

    private int clampDragY(int y) {
        return clamp(y, 0, Math.max(0, screenH - dp(40)));
    }

    private void savePos(boolean isBubble, WindowManager.LayoutParams pr) {
        try {
            prefs.edit().putInt(isBubble ? "bubbleX" : "menuX", pr.x)
                    .putInt(isBubble ? "bubbleY" : "menuY", pr.y).apply();
        } catch (Exception ignored) {}
    }

    private void snapBubbleToEdge() {
        if (bubbleParams == null || bubble == null || bubble.getVisibility() != View.VISIBLE) return;
        int w = bubbleParams.width;
        int target = (bubbleParams.x + w / 2 < screenW / 2) ? dp(8) : Math.max(dp(8), screenW - w - dp(8));
        if (target == bubbleParams.x) return;
        if (snapAnimator != null) snapAnimator.cancel();
        snapAnimator = ValueAnimator.ofInt(bubbleParams.x, target);
        snapAnimator.setDuration(170);
        snapAnimator.setInterpolator(new DecelerateInterpolator(1.6f));
        snapAnimator.addUpdateListener(a -> {
            if (bubbleParams == null || bubble == null) return;
            bubbleParams.x = (int) a.getAnimatedValue();
            try { wm.updateViewLayout(bubble, bubbleParams); } catch (Throwable ignored) {}
        });
        snapAnimator.addListener(new AnimatorListenerAdapter() {
            @Override public void onAnimationEnd(Animator a) {
                if (bubbleParams != null) savePos(true, bubbleParams);
            }
        });
        snapAnimator.start();
    }

    // ---------- throttled resize grip ----------
    private void makeResizable(final View grip) {
        grip.setOnTouchListener(new View.OnTouchListener() {
            int startW, startH;
            float touchX, touchY;
            long lastUpdateNs = 0;

            public boolean onTouch(View view, MotionEvent e) {
                if (params == null) return false;
                switch (e.getActionMasked()) {
                    case MotionEvent.ACTION_DOWN:
                        startW = params.width;
                        startH = params.height;
                        touchX = e.getRawX();
                        touchY = e.getRawY();
                        lastUpdateNs = 0;
                        return true;
                    case MotionEvent.ACTION_MOVE: {
                        int nw = clamp(startW + (int) (e.getRawX() - touchX), dp(310), Math.max(dp(320), screenW - dp(16)));
                        int nh = clamp(startH + (int) (e.getRawY() - touchY), dp(220), Math.max(dp(240), screenH - dp(24)));
                        long now = System.nanoTime();
                        if ((nw != params.width || nh != params.height) && (now - lastUpdateNs >= 12_000_000L)) {
                            params.width = nw;
                            params.height = nh;
                            lastUpdateNs = now;
                            try { wm.updateViewLayout(root, params); } catch (Throwable ignored) {}
                        }
                        return true;
                    }
                    case MotionEvent.ACTION_UP:
                    case MotionEvent.ACTION_CANCEL:
                        params.width = clamp(startW + (int) (e.getRawX() - touchX), dp(310), Math.max(dp(320), screenW - dp(16)));
                        params.height = clamp(startH + (int) (e.getRawY() - touchY), dp(220), Math.max(dp(240), screenH - dp(24)));
                        try { wm.updateViewLayout(root, params); } catch (Throwable ignored) {}
                        prefs.edit().putInt("menuW", params.width).putInt("menuH", params.height).apply();
                        return true;
                }
                return false;
            }
        });
    }

    // ---------- widgets ----------
    private LinearLayout page() {
        LinearLayout l = new LinearLayout(ctx);
        l.setOrientation(LinearLayout.VERTICAL);
        l.setPadding(dp(2), dp(2), dp(2), dp(2));
        return l;
    }

    private LinearLayout card() {
        LinearLayout c = new LinearLayout(ctx);
        c.setOrientation(LinearLayout.VERTICAL);
        c.setBackground(round(CARD, 14, STROKE, 0.7f));
        c.setPadding(dp(12), dp(6), dp(12), dp(6));
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(-1, -2);
        lp.setMargins(0, 0, 0, dp(10));
        c.setLayoutParams(lp);
        return c;
    }

    private LinearLayout groupCard(LinearLayout p) {
        LinearLayout g = card();
        p.addView(g);
        return g;
    }

    private void addRow(LinearLayout group, View row) {
        if (group.getChildCount() > 0) {
            View div = new View(ctx);
            LinearLayout.LayoutParams dlp = new LinearLayout.LayoutParams(-1, Math.max(1, dp(0.7f)));
            dlp.setMargins(dp(42), 0, dp(2), 0);
            div.setLayoutParams(dlp);
            div.setBackgroundColor(STROKE);
            group.addView(div);
        }
        group.addView(row);
    }

    private void sectionHead(LinearLayout p, String title, String subtitle) {
        TextView t = label(title, 15, TEXT);
        t.setTypeface(Typeface.DEFAULT_BOLD);
        TextView s = label(subtitle, 10, MUTED);
        s.setPadding(0, dp(2), 0, dp(10));
        p.addView(t);
        p.addView(s);
    }

    private interface BoolCb { void run(boolean v); }
    private interface FloatCb { void run(float v); }

    private void featureToggle(LinearLayout g, final String key, String icon, String name, String desc, boolean def, final BoolCb cb) {
        LinearLayout r = new LinearLayout(ctx);
        r.setOrientation(LinearLayout.HORIZONTAL);
        r.setGravity(Gravity.CENTER_VERTICAL);
        r.setPadding(dp(2), dp(9), dp(2), dp(9));
        if (icon != null && !icon.isEmpty()) {
            TextView ic = label(icon, 16, RED2);
            ic.setGravity(Gravity.CENTER);
            ic.setLayoutParams(new LinearLayout.LayoutParams(dp(30), -2));
            r.addView(ic);
        }
        LinearLayout texts = new LinearLayout(ctx);
        texts.setOrientation(LinearLayout.VERTICAL);
        texts.setLayoutParams(new LinearLayout.LayoutParams(0, -2, 1f));
        TextView t = label(name, 13, TEXT);
        t.setTypeface(Typeface.DEFAULT_BOLD);
        texts.addView(t);
        if (desc != null && !desc.isEmpty()) {
            TextView d = label(desc, 10, MUTED);
            d.setPadding(0, dp(1), 0, 0);
            texts.addView(d);
        }
        final FrameLayout track = new FrameLayout(ctx);
        track.setLayoutParams(new LinearLayout.LayoutParams(dp(50), dp(28)));
        final GradientDrawable trackOn = round(RED, 14, 0, 0);
        final GradientDrawable trackOff = round(TRACK, 14, 0, 0);
        final View thumb = new View(ctx);
        FrameLayout.LayoutParams thLp = new FrameLayout.LayoutParams(dp(22), dp(22));
        thLp.gravity = Gravity.CENTER_VERTICAL;
        thLp.setMargins(dp(3), 0, dp(3), 0);
        thumb.setLayoutParams(thLp);
        thumb.setBackground(oval(Color.WHITE));
        track.addView(thumb);
        r.addView(texts);
        r.addView(track);
        addRow(g, r);

        final boolean[] state = {prefs.getBoolean(key, def)};
        track.setBackground(state[0] ? trackOn : trackOff);
        thumb.setTranslationX(state[0] ? dp(22) : 0);
        if (state[0]) try { cb.run(true); } catch (Throwable ignored) {}
        r.setOnClickListener(v -> {
            state[0] = !state[0];
            prefs.edit().putBoolean(key, state[0]).apply();
            click(v);
            track.setBackground(state[0] ? trackOn : trackOff);
            thumb.animate().cancel();
            thumb.animate().translationX(state[0] ? dp(22) : 0).setDuration(130).start();
            try { cb.run(state[0]); } catch (Throwable ignored) {}
        });
    }

    private void toggle(LinearLayout p, String key, String name, boolean def, BoolCb cb) {
        featureToggle(p, key, null, name, null, def, cb);
    }

    private void featureSlider(LinearLayout g, final String key, String icon, String name, String desc,
                               final float min, final float max, float def, final String unit, final FloatCb cb) {
        LinearLayout col = new LinearLayout(ctx);
        col.setOrientation(LinearLayout.VERTICAL);
        col.setPadding(dp(2), dp(9), dp(2), dp(7));
        LinearLayout head = new LinearLayout(ctx);
        head.setOrientation(LinearLayout.HORIZONTAL);
        head.setGravity(Gravity.CENTER_VERTICAL);
        if (icon != null && !icon.isEmpty()) {
            TextView ic = label(icon, 16, RED2);
            ic.setGravity(Gravity.CENTER);
            ic.setLayoutParams(new LinearLayout.LayoutParams(dp(30), -2));
            head.addView(ic);
        }
        LinearLayout texts = new LinearLayout(ctx);
        texts.setOrientation(LinearLayout.VERTICAL);
        texts.setLayoutParams(new LinearLayout.LayoutParams(0, -2, 1f));
        TextView t = label(name, 13, TEXT);
        t.setTypeface(Typeface.DEFAULT_BOLD);
        texts.addView(t);
        if (desc != null && !desc.isEmpty()) {
            TextView d = label(desc, 10, MUTED);
            d.setPadding(0, dp(1), 0, 0);
            texts.addView(d);
        }
        final float saved = clampFloat(prefs.getFloat(key, def), min, max);
        final TextView val = label(saved + unit, 12, TEXT);
        val.setTypeface(Typeface.DEFAULT_BOLD);
        val.setGravity(Gravity.CENTER);
        val.setPadding(dp(10), dp(3), dp(10), dp(3));
        val.setBackground(round(0xFF0D0D10, 9, STROKE, 0.7f));
        head.addView(texts);
        head.addView(val);
        col.addView(head);

        SeekBar s = new SeekBar(ctx);
        s.setMax(100);
        styleSeek(s);
        s.setProgress((int) ((saved - min) / (max - min) * 100));
        try { cb.run(saved); } catch (Throwable ignored) {}
        final float[] lastVal = {saved};
        s.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            public void onProgressChanged(SeekBar sb, int pr, boolean u) {
                float v = min + (max - min) * pr / 100f;
                float r = Math.round(v * 10) / 10f;
                if (r != lastVal[0]) {
                    lastVal[0] = r;
                    val.setText(r + unit);
                    try { cb.run(r); } catch (Throwable ignored) {}
                }
            }
            public void onStartTrackingTouch(SeekBar sb) {}
            public void onStopTrackingTouch(SeekBar sb) {
                prefs.edit().putFloat(key, lastVal[0]).apply();
            }
        });
        col.addView(s);
        addRow(g, col);
    }

    private float clampFloat(float v, float min, float max) {
        return Math.max(min, Math.min(max, v));
    }

    private void slider(LinearLayout p, String key, String name, float min, float max, float def, String unit, FloatCb cb) {
        featureSlider(p, key, null, name, null, min, max, def, unit, cb);
    }

    private void styleSeek(SeekBar s) {
        GradientDrawable thumbD = new GradientDrawable();
        thumbD.setShape(GradientDrawable.OVAL);
        thumbD.setColor(RED2);
        thumbD.setStroke(Math.max(1, dp(2)), Color.WHITE);
        thumbD.setSize(dp(18), dp(18));
        s.setThumb(thumbD);
        GradientDrawable trackD = round(TRACK, 4, 0, 0);
        trackD.setSize(-1, dp(6));
        GradientDrawable progD = gradient(RED, RED2, 4);
        progD.setSize(-1, dp(6));
        ClipDrawable clip = new ClipDrawable(progD, Gravity.START, ClipDrawable.HORIZONTAL);
        LayerDrawable layers = new LayerDrawable(new Drawable[]{trackD, clip});
        layers.setId(0, android.R.id.background);
        layers.setId(1, android.R.id.progress);
        s.setProgressDrawable(layers);
        s.setPadding(dp(4), dp(6), dp(4), dp(6));
    }

    private Button styledButton(String text, boolean primary) {
        Button b = new Button(ctx);
        b.setText(text);
        b.setAllCaps(false);
        b.setTextSize(13);
        b.setTextColor(primary ? Color.WHITE : TEXT);
        if (primary) b.setTypeface(Typeface.DEFAULT_BOLD);
        b.setBackground(primary ? gradient(RED, RED2, 12) : round(CARD, 12, STROKE, 0.8f));
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(-1, dp(42));
        lp.setMargins(0, dp(4), 0, dp(6));
        b.setLayoutParams(lp);
        return b;
    }

    private Button dangerButton(String text) {
        Button b = new Button(ctx);
        b.setText(text);
        b.setAllCaps(false);
        b.setTextSize(13);
        b.setTextColor(Color.WHITE);
        b.setTypeface(Typeface.DEFAULT_BOLD);
        b.setBackground(round(0xFF2A0A10, 12, RED, 1f));
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(-1, dp(42));
        lp.setMargins(0, dp(4), 0, dp(6));
        b.setLayoutParams(lp);
        return b;
    }

    // ---------- HOME ----------
    private LinearLayout pageHome() {
        LinearLayout p = page();
        sectionHead(p, "Main Features", "Core mods & protection");
        LinearLayout g = groupCard(p);
        featureToggle(g, "god", "✚", "God Mode", "No damage & fall protection", false, NativeBridge::setGodMode);
        featureToggle(g, "esp", "◉", "ESP", "Show players through walls", false, NativeBridge::setEspEnabled);
        featureToggle(g, "bulletWall", "⚡", "Bullet Wallhack", "Bullets pierce obstacles", false, NativeBridge::setBulletWallhack);
        featureToggle(g, "plrSpeedOn", "➤", "Player Speed", "Multiply movement velocity", false, NativeBridge::setPlayerSpeedEnabled);
        featureSlider(g, "plrSpeed", "↕", "Speed Multiplier", "Adjust walk/run multiplier", 1f, 8f, 1f, "x", NativeBridge::setPlayerSpeedMultiplier);
        featureToggle(g, "devSpoof", "🛡", "Device ID Spoof", "Spoof SystemInfo & Auth ID", false, NativeBridge::setDeviceSpoof);
        return p;
    }

    // ---------- AIM / COMBAT ----------
    private LinearLayout pageAim() {
        LinearLayout p = page();
        sectionHead(p, "Aim / Combat", "Weapon & combat hooks");
        LinearLayout g = groupCard(p);
        toggle(g, "silentAim", "Silent Aim (Weapon Hooks)", false, NativeBridge::setSilentAim);
        toggle(g, "bulletWall", "Bullet Wallhack (WallPen)", false, NativeBridge::setBulletWallhack);
        toggle(g, "god", "God Mode (No Damage)", false, NativeBridge::setGodMode);
        toggle(g, "noFall", "No Fall Damage", false, NativeBridge::setNoFallDamage);
        toggle(g, "ammo", "Infinite Ammo", false, NativeBridge::setInfiniteAmmo);
        toggle(g, "rapid", "Rapid Fire", false, NativeBridge::setRapidFire);
        toggle(g, "recoil", "No Recoil", false, NativeBridge::setNoRecoil);
        return p;
    }

    // ---------- VISUAL ----------
    private LinearLayout pageVisual() {
        LinearLayout p = page();
        sectionHead(p, "Visual", "ESP, camera, range & head");
        LinearLayout g = groupCard(p);
        toggle(g, "esp", "Player ESP", false, NativeBridge::setEspEnabled);
        slider(g, "espDist", "ESP Distance", 50, 1000, 500, "m", NativeBridge::setEspDistance);
        toggle(g, "extRange", "Extended Render Range (No Culling)", false, NativeBridge::setExtendedRange);
        toggle(g, "skeleton", "Skeleton ESP", false, NativeBridge::setSkeletonEsp);
        toggle(g, "npcs", "Show NPCs", true, NativeBridge::setShowNpcs);
        toggle(g, "vehiclesEsp", "Show Vehicles", false, NativeBridge::setShowVehicles);
        toggle(g, "fovOn", "Override Camera FOV", false, NativeBridge::setFovOverride);
        slider(g, "fov", "Camera FOV", 40, 120, 60, "°", NativeBridge::setFov);
        toggle(g, "freeCam", "FreeCam (Detach Follow)", false, NativeBridge::setFreeCam);
        toggle(g, "night", "Night Vision / Lighting", false, NativeBridge::setNightVision);
        toggle(g, "headBig", "Head Enlarge", false, NativeBridge::setHeadEnlarge);

        LinearLayout colorCard = card();
        p.addView(colorCard);
        colorCard.addView(label("ESP Box Color", 13, TEXT));
        LinearLayout colors = new LinearLayout(ctx);
        colors.setOrientation(LinearLayout.HORIZONTAL);
        int[] palette = {0xFFFF1E3C, 0xFF3BFF7A, 0xFFFFD93B, 0xFFFFFFFF, 0xFF3B82F6, 0xFFFF8A3B};
        int selected = prefs.getInt("espColor", RED2);
        for (int c : palette) {
            View sw = new View(ctx);
            LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(dp(30), dp(30));
            lp.setMargins(dp(5), dp(10), dp(5), dp(4));
            sw.setLayoutParams(lp);
            GradientDrawable d = new GradientDrawable();
            d.setShape(GradientDrawable.OVAL);
            d.setColor(c);
            if (c == selected) d.setStroke(Math.max(1, dp(2)), Color.WHITE);
            sw.setBackground(d);
            sw.setOnClickListener(v -> {
                click(v);
                prefs.edit().putInt("espColor", c).apply();
                try { NativeBridge.setEspBoxColor(c); } catch (Throwable ignored) {}
            });
            colors.addView(sw);
        }
        try { NativeBridge.setEspBoxColor(selected); } catch (Throwable ignored) {}
        colorCard.addView(colors);
        return p;
    }

    // ---------- MOVEMENT & VEHICLE ----------
    private LinearLayout pageMisc() {
        LinearLayout p = page();
        sectionHead(p, "Movement & Vehicle", "Physics, SkyWalk & stamina");
        LinearLayout g = groupCard(p);
        toggle(g, "skyWalk", "SkyWalk (Stay Grounded & Shoot)", false, NativeBridge::setSkyWalk);
        toggle(g, "walkAir", "Walk On Air", false, NativeBridge::setWalkOnAir);
        toggle(g, "autoWalk", "Auto Walk", false, NativeBridge::setAutoWalk);
        toggle(g, "infStam", "Infinite Stamina", false, NativeBridge::setInfiniteStamina);
        toggle(g, "plrSpeedOn", "Player Speed Hack", false, NativeBridge::setPlayerSpeedEnabled);
        slider(g, "plrSpeed", "Player Speed", 1f, 8f, 1f, "x", NativeBridge::setPlayerSpeedMultiplier);
        toggle(g, "vehSpeedOn", "Vehicle Speed Hack", false, NativeBridge::setVehicleSpeedEnabled);
        slider(g, "vehSpeed", "Vehicle Multiplier", 1, 10, 1, "x", NativeBridge::setVehicleSpeedMultiplier);
        toggle(g, "brake", "Instant Brake", false, NativeBridge::setInstantBrake);

        Button tp = styledButton("Teleport to Waypoint", true);
        tp.setOnClickListener(v -> {
            click(v);
            try { NativeBridge.teleportToWaypoint(); } catch (Throwable ignored) {}
        });
        p.addView(tp);
        return p;
    }

    // ---------- CONFIG & PROTECTION ----------
    private LinearLayout pageConfig() {
        LinearLayout p = page();
        sectionHead(p, "Config & Protection", "Menu behavior & security");
        LinearLayout g = groupCard(p);
        slider(g, "opacity", "Menu Opacity", 0.3f, 1f, 1f, "", v -> {
            if (root != null && isOpen) root.setAlpha(v);
        });
        toggle(g, "haptic", "Haptic Feedback", true, v -> haptic = v);
        toggle(g, "stealth", "Hide Bubble (Stealth)", false, v -> {});
        toggle(g, "hideRec", "Hide From Recording (FLAG_SECURE)", false, v -> applySecurityFlag());
        toggle(g, "devSpoof", "Device ID Spoofer (Unban)", false, NativeBridge::setDeviceSpoof);

        Button regen = styledButton("Regenerate Spoof Device ID", true);
        regen.setOnClickListener(v -> {
            click(v);
            try { NativeBridge.regenerateSpoofId(); } catch (Throwable ignored) {}
            Toast.makeText(ctx, "New Spoof ID generated", Toast.LENGTH_SHORT).show();
        });
        Button save = styledButton("Save Settings", false);
        save.setOnClickListener(v -> {
            click(v);
            prefs.edit().apply();
            Toast.makeText(ctx, "Saved", Toast.LENGTH_SHORT).show();
        });
        final TextView pos = label("Player Pos: (waiting for hook…)", 12, MUTED);
        Button chk = styledButton("Refresh Hook Telemetry", false);
        chk.setOnClickListener(v -> {
            click(v);
            updateStatusText();
            try {
                float[] xyz = NativeBridge.getPlayerPosition();
                pos.setText(NativeBridge.isHooked() && xyz != null
                        ? String.format("Pos: %.2f, %.2f, %.2f | Hooks: %d/%d",
                                xyz[0], xyz[1], xyz[2], NativeBridge.getHooksInstalled(), NativeBridge.getHooksTotal())
                        : "Hooks not ready yet");
            } catch (Throwable ignored) {
                pos.setText("Native library unavailable");
            }
        });
        Button remove = dangerButton("Remove Menu Completely");
        remove.setOnClickListener(v -> { click(v); removeCompletely(); });
        p.addView(regen);
        p.addView(save);
        p.addView(chk);
        p.addView(pos);
        p.addView(remove);
        return p;
    }
}
