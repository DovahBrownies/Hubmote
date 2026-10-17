package io.github.dovahbrownies.hubmote;

import android.accessibilityservice.AccessibilityService;
import android.app.SearchManager;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.graphics.PixelFormat;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.text.InputType;
import android.util.Log;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.View;
import android.view.WindowManager;
import android.view.accessibility.AccessibilityEvent;
import android.view.accessibility.AccessibilityNodeInfo;
import android.widget.LinearLayout;
import android.widget.TextView;

import java.util.concurrent.Callable;
import java.util.concurrent.FutureTask;
import java.util.concurrent.TimeUnit;

/**
 * The whole companion. As an accessibility service it is started at boot,
 * kept running, sees which field has input focus, can type into it, and may
 * open apps from the background. It also hosts the HTTP server the remote
 * talks to.
 */
public final class CompanionService extends AccessibilityService {
    static final String TAG = "HubmoteCompanion";
    static final int PORT = 6470;
    /* The build doesn't generate R, so the overlay's text lives here. */
    private static final String PAIR_TITLE = "Hubmote";
    private static final String PAIR_HINT = "Enter this code on the remote";

    /** "none", "text", "password" or "number". */
    private volatile String field = "none";
    /** Changes whenever a different field gets input focus. */
    private volatile int fieldId;
    private AccessibilityNodeInfo focused; /* main thread only */
    private final Handler main = new Handler(Looper.getMainLooper());
    private Server server;
    private Pairing pairing;
    private Updater updater;
    private NowPlaying nowPlaying;
    private View codeView; /* the pairing code on screen; main thread only */

    /* An app's own on-screen keyboard (OnScreenKeyboard), main thread only:
     * whether one is showing, and when the screen was last searched for it. */
    private static final long SCAN_GAP_MS = 400;
    private static final long KEY_GAP_MS = 150; /* between key presses: faster ones get dropped (Plex) */
    private static final long FOCUS_GAP_MS = 120; /* between moving focus to a key and pressing OK */
    /* The same key again: apps drop quick repeats, even from a real remote
     * (Plex takes one of four OKs sent back to back). */
    private static final long REPEAT_GAP_MS = 500;
    private boolean keyboardShown;
    private long lastScan = -SCAN_GAP_MS;
    private boolean rescanPosted;
    /* What was typed into it, as the keys it has (lowercase); typing only. */
    private final Object typing = new Object();
    private String kbTyped;
    private volatile String kbLatest; /* the newest text asked for: queued requests skip to it */
    private int kbWindow = -1;
    private AccessibilityNodeInfo lastKey; /* the key pressed last: it still has focus */
    private long lastKeyAt;

    @Override
    protected void onServiceConnected() {
        pairing = new Pairing(this);
        updater = new Updater(this);
        updater.register();
        nowPlaying = new NowPlaying(this, main);
        server = new Server(this, PORT);
        server.start();
        Log.i(TAG, "listening on port " + PORT);
        nowPlaying.start();
        main.post(this::update); /* a field or keyboard may already be on screen */
    }

    @Override
    public boolean onUnbind(Intent intent) {
        hideKeyboard(false);
        hideCode();
        if (updater != null) {
            updater.unregister();
        }
        if (nowPlaying != null) {
            nowPlaying.stop();
        }
        if (server != null) {
            server.stop();
            server = null;
        }
        return super.onUnbind(intent);
    }

    @Override
    public void onInterrupt() {
    }

    @Override
    public void onAccessibilityEvent(AccessibilityEvent event) {
        update();
    }

    /** What has input focus: a text field, an app's own on-screen keyboard
     * (reported as a text field: the remote types into it the same way), or
     * nothing. Main thread. */
    private void update() {
        AccessibilityNodeInfo node = focusedEditable();
        boolean kb = node == null && keyboardOnScreen();
        String kind = node != null ? kindOf(node) : kb ? "text" : "none";
        boolean moved = node == null ? focused != null : !node.equals(focused);
        if (kb && !keyboardShown) {
            moved = true; /* a keyboard appearing is a new field */
        }
        keyboardShown = kb;
        focused = node;
        if (moved && (node != null || kb)) {
            fieldId++;
        }
        if (!kind.equals(field) || moved) {
            field = kind;
            if (server != null) {
                server.broadcast(fieldJson());
            }
        }
    }

    String fieldJson() {
        return "{\"field\":\"" + field + "\",\"id\":" + fieldId + "}";
    }

    /** Opens an app's TV launch activity; false if it has none. */
    boolean launch(String pkg) {
        PackageManager pm = getPackageManager();
        Intent i = pm.getLeanbackLaunchIntentForPackage(pkg);
        if (i == null) {
            i = pm.getLaunchIntentForPackage(pkg);
        }
        if (i == null) {
            return false;
        }
        final Intent start = i.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_RESET_TASK_IF_NEEDED);
        main.post(() -> {
            try {
                startActivity(start);
            } catch (RuntimeException e) {
                Log.w(TAG, "launch " + pkg + " failed", e);
            }
        });
        return true;
    }

    /** Opens Google TV's own search with this text: results from every app.
     * False if nothing handles the search. */
    boolean search(String query) {
        Intent i = new Intent(SearchManager.INTENT_ACTION_GLOBAL_SEARCH)
                .putExtra(SearchManager.QUERY, query)
                .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
        if (getPackageManager().resolveActivity(i, 0) == null) {
            return false;
        }
        main.post(() -> {
            try {
                startActivity(i);
            } catch (RuntimeException e) {
                Log.w(TAG, "search failed", e);
            }
        });
        return true;
    }

    /** Replaces the focused field's text. Works in apps that never show an
     * input method to the remote protocol, such as browsers. Without a text
     * field, presses the keys of the app's own on-screen keyboard. */
    boolean setText(String text) {
        Boolean edited = onMainGet(() -> {
            AccessibilityNodeInfo node = focusedEditable();
            if (node == null) {
                return null;
            }
            Bundle args = new Bundle();
            args.putCharSequence(AccessibilityNodeInfo.ACTION_ARGUMENT_SET_TEXT_CHARSEQUENCE, text);
            return node.performAction(AccessibilityNodeInfo.ACTION_SET_TEXT, args);
        });
        return edited != null ? edited : typeOnKeyboard(text);
    }

    /** The field's Enter / Go / Search action (Android 11+), or the
     * on-screen keyboard's Search / Enter key. True also for an on-screen
     * keyboard without one (it searches as you type): a key press would
     * type the key that has focus. */
    boolean enter() {
        Boolean done = onMainGet(() -> {
            AccessibilityNodeInfo node = focusedEditable();
            return node == null ? null
                    : node.performAction(AccessibilityNodeInfo.AccessibilityAction.ACTION_IME_ENTER.getId());
        });
        if (done != null) {
            return done;
        }
        synchronized (typing) {
            OnScreenKeyboard k = onMainGet(() -> OnScreenKeyboard.find(this));
            if (k == null) {
                return false;
            }
            if (k.enter != null) {
                press(k.enter);
            }
            return true;
        }
    }

    /** Types into an app's own on-screen keyboard by pressing its keys: the
     * remote sends the whole text after every edit, so this works out the
     * difference to what was typed so far (Backspace, or Clear and again).
     * Characters the keyboard has no key for are left out. Not on the main
     * thread: it waits between key presses. */
    private boolean typeOnKeyboard(String asked) {
        kbLatest = asked;
        synchronized (typing) {
            String value = kbLatest; /* a newer request may have come in while this one waited */
            OnScreenKeyboard k = onMainGet(() -> OnScreenKeyboard.find(this));
            if (k == null) {
                kbTyped = null;
                return false;
            }
            String have = kbTyped;
            if (have == null || k.windowId != kbWindow) {
                /* A new keyboard: start from an empty query if it can. */
                kbWindow = k.windowId;
                lastKey = null;
                have = "";
                Log.i(TAG, "on-screen keyboard: " + k.letterCount() + " letters" + (k.space != null ? ", space" : "")
                        + (k.backspace != null ? ", backspace" : "") + (k.clear != null ? ", clear" : "")
                        + (k.enter != null ? ", enter" : ""));
                if (k.clear != null) {
                    press(k.clear);
                }
            }
            StringBuilder want = new StringBuilder();
            for (int i = 0; i < value.length(); i++) {
                char c = Character.toLowerCase(value.charAt(i));
                if (k.keyFor(c) != null) {
                    want.append(c);
                }
            }
            int common = 0;
            while (common < have.length() && common < want.length() && have.charAt(common) == want.charAt(common)) {
                common++;
            }
            int extra = have.length() - common;
            if (extra > 0 && common == 0 && k.clear != null) {
                press(k.clear); /* nothing kept: one Clear beats a row of Backspaces */
            } else if (extra > 0 && k.backspace != null) {
                for (int i = 0; i < extra; i++) {
                    press(k.backspace);
                }
            } else if (extra > 0 && k.clear != null) {
                press(k.clear);
                common = 0;
            }
            StringBuilder now = new StringBuilder(want.substring(0, Math.min(common, want.length())));
            for (int i = common; i < want.length(); i++) {
                char c = want.charAt(i);
                if (!press(k.keyFor(c))) {
                    /* The app may have redrawn its keys: find them again, once. */
                    OnScreenKeyboard again = onMainGet(() -> OnScreenKeyboard.find(this));
                    if (again == null || again.keyFor(c) == null || !press(again.keyFor(c))) {
                        break;
                    }
                    k = again;
                }
                now.append(c);
            }
            kbTyped = now.toString();
            Log.i(TAG, "on-screen keyboard: typed " + now.length() + " of " + want.length() + " keys (" + extra
                    + " removed)");
            return true;
        }
    }

    /** Presses one key the way a TV user does: focus on it, then the D-pad's
     * OK (Android 13+). Every TV app takes OK on a focused key; some ignore
     * an accessibility click (Plex's React Native keys do). Keys that can't
     * take focus get the click. Then gives the app a moment. */
    private boolean press(AccessibilityNodeInfo key) {
        if (key == null) {
            return false;
        }
        if (key.equals(lastKey)) {
            long wait = lastKeyAt + REPEAT_GAP_MS - SystemClock.uptimeMillis();
            if (wait > 0) {
                SystemClock.sleep(wait);
            }
        }
        /* 2: focused, OK follows once the app has moved its focus; 1: clicked; 0: failed */
        Integer how = onMainGet(() -> {
            if (Build.VERSION.SDK_INT >= 33 && key.isFocusable()) {
                /* The same key again: it has focus, and asking again makes
                 * some apps drop the next OK (Plex). Any other key: always ask,
                 * as the node is a snapshot whose focused state may be old. */
                if (key.equals(lastKey) || key.performAction(AccessibilityNodeInfo.ACTION_FOCUS)) {
                    return 2;
                }
            }
            return key.performAction(AccessibilityNodeInfo.ACTION_CLICK) ? 1 : 0;
        });
        boolean ok = how != null && how != 0;
        lastKey = ok ? key : null;
        lastKeyAt = SystemClock.uptimeMillis();
        if (how != null && how == 2) {
            SystemClock.sleep(FOCUS_GAP_MS);
            Boolean centered = onMainGet(() -> performGlobalAction(GLOBAL_ACTION_DPAD_CENTER));
            ok = centered != null && centered;
        }
        SystemClock.sleep(KEY_GAP_MS);
        return ok;
    }

    /** Whether an on-screen keyboard is showing. Searching the screen costs a
     * few milliseconds, so at most every SCAN_GAP_MS, with one more search
     * after a burst of events so the last state is never missed. */
    private boolean keyboardOnScreen() {
        long now = SystemClock.uptimeMillis();
        if (now - lastScan < SCAN_GAP_MS) {
            if (!rescanPosted) {
                rescanPosted = true;
                main.postDelayed(() -> {
                    rescanPosted = false;
                    update();
                }, SCAN_GAP_MS - (now - lastScan));
            }
            return keyboardShown;
        }
        lastScan = now;
        return OnScreenKeyboard.find(this) != null;
    }

    /** Hides the TV's on-screen keyboard while the remote's is open. */
    void hideKeyboard(boolean hidden) {
        main.post(() -> getSoftKeyboardController().setShowMode(
                hidden ? AccessibilityService.SHOW_MODE_HIDDEN : AccessibilityService.SHOW_MODE_AUTO));
    }

    Pairing pairing() {
        return pairing;
    }

    Updater updater() {
        return updater;
    }

    NowPlaying nowPlaying() {
        return nowPlaying;
    }

    /** What is playing changed (title or state): tell the remote. */
    void mediaChanged(String json) {
        Server s = server;
        if (s != null) {
            s.broadcast("{\"media\":" + json + "}");
        }
    }

    /** An update's progress, for the remote: "downloading", "installing",
     * "confirm" or "failed". */
    void updateState(String state) {
        Server s = server;
        if (s != null) {
            s.broadcast("{\"update\":\"" + state + "\"}");
        }
    }

    /** Shows a pairing code over whatever is on the TV, until it is used,
     * cancelled or expires. An accessibility overlay needs no permission. */
    void showCode(String code) {
        main.post(() -> {
            removeCodeView();
            LinearLayout box = new LinearLayout(this);
            box.setOrientation(LinearLayout.VERTICAL);
            box.setGravity(Gravity.CENTER_HORIZONTAL);
            int pad = dp(32);
            box.setPadding(pad, dp(24), pad, dp(24));
            GradientDrawable bg = new GradientDrawable();
            bg.setColor(0xF0181A1F);
            bg.setCornerRadius(dp(20));
            box.setBackground(bg);
            box.setTag(code);
            box.addView(text(PAIR_TITLE, 18, 0xFFA3AAB6, false));
            TextView digits = text(code, 56, 0xFFF2A93B, true);
            digits.setLetterSpacing(0.25f);
            digits.setPadding(0, dp(8), 0, dp(8));
            box.addView(digits);
            box.addView(text(PAIR_HINT, 16, 0xFFECEEF2, false));

            WindowManager.LayoutParams lp = new WindowManager.LayoutParams(
                    WindowManager.LayoutParams.WRAP_CONTENT, WindowManager.LayoutParams.WRAP_CONTENT,
                    WindowManager.LayoutParams.TYPE_ACCESSIBILITY_OVERLAY,
                    WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE | WindowManager.LayoutParams.FLAG_NOT_TOUCHABLE,
                    PixelFormat.TRANSLUCENT);
            lp.gravity = Gravity.CENTER;
            try {
                ((WindowManager) getSystemService(WINDOW_SERVICE)).addView(box, lp);
                codeView = box;
            } catch (RuntimeException e) {
                Log.w(TAG, "can't show the pairing code", e);
            }
        });
        main.postDelayed(() -> {
            if (pairing.showing(code)) {
                pairing.cancel();
            }
            hideCodeNow(code);
        }, Pairing.CODE_TTL_MS);
    }

    void hideCode() {
        main.post(this::removeCodeView);
    }

    /* After the timeout: hide unless a newer code replaced this one. */
    private void hideCodeNow(String code) {
        if (codeView != null && code.equals(codeView.getTag())) {
            removeCodeView();
        }
    }

    private void removeCodeView() {
        if (codeView != null) {
            try {
                ((WindowManager) getSystemService(WINDOW_SERVICE)).removeView(codeView);
            } catch (RuntimeException ignored) {
            }
            codeView = null;
        }
    }

    private TextView text(String s, int sp, int color, boolean mono) {
        TextView v = new TextView(this);
        v.setText(s);
        v.setTextSize(TypedValue.COMPLEX_UNIT_SP, sp);
        v.setTextColor(color);
        v.setGravity(Gravity.CENTER);
        if (mono) {
            v.setTypeface(Typeface.MONOSPACE, Typeface.BOLD);
        }
        return v;
    }

    private int dp(int v) {
        return Math.round(v * getResources().getDisplayMetrics().density);
    }

    /** Runs on the main thread and waits up to 2 s for the answer (null if
     * it failed or took too long). */
    private <T> T onMainGet(Callable<T> work) {
        FutureTask<T> task = new FutureTask<>(work);
        main.post(task);
        try {
            return task.get(2, TimeUnit.SECONDS);
        } catch (Exception e) {
            Log.w(TAG, "main-thread call failed", e);
            return null;
        }
    }

    /** The field with input focus in any window: the TV's own keyboard is
     * often the active window while a field is being edited. */
    private AccessibilityNodeInfo focusedEditable() {
        AccessibilityNodeInfo f = findFocus(AccessibilityNodeInfo.FOCUS_INPUT);
        return f != null && f.isEditable() ? f : null;
    }

    private static String kindOf(AccessibilityNodeInfo f) {
        if (f == null) {
            return "none";
        }
        int type = f.getInputType();
        int cls = type & InputType.TYPE_MASK_CLASS;
        if (cls == InputType.TYPE_CLASS_NUMBER || cls == InputType.TYPE_CLASS_PHONE) {
            return "number";
        }
        int variation = type & InputType.TYPE_MASK_VARIATION;
        if (cls == InputType.TYPE_CLASS_TEXT && (variation == InputType.TYPE_TEXT_VARIATION_PASSWORD
                || variation == InputType.TYPE_TEXT_VARIATION_VISIBLE_PASSWORD
                || variation == InputType.TYPE_TEXT_VARIATION_WEB_PASSWORD)) {
            return "password";
        }
        return "text";
    }
}
