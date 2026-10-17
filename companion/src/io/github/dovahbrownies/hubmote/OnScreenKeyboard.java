package io.github.dovahbrownies.hubmote;

import android.accessibilityservice.AccessibilityService;
import android.graphics.Rect;
import android.view.accessibility.AccessibilityNodeInfo;
import android.view.accessibility.AccessibilityWindowInfo;

import java.util.ArrayDeque;
import java.util.HashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;

/**
 * An app's own on-screen keyboard: the grid of letter keys some apps draw
 * instead of a text field (Plex, for example). Found through accessibility,
 * with no rules for particular apps: a key is anything actionable labelled
 * with one letter or digit, or named space, backspace, clear or search by
 * its label or view id. A screen with keys for most of the alphabet counts
 * as a keyboard.
 *
 * Apps that draw their whole interface as one picture (Netflix, YouTube)
 * show nothing here; the remote sends them key presses instead.
 */
final class OnScreenKeyboard {
    /** Distinct letters a-z needed before a screen counts as a keyboard. */
    private static final int MIN_LETTERS = 20;
    /** Stop looking after this many nodes: some screens are huge lists. */
    private static final int MAX_NODES = 2500;

    final Map<Character, AccessibilityNodeInfo> chars = new HashMap<>();
    AccessibilityNodeInfo space;
    AccessibilityNodeInfo backspace;
    AccessibilityNodeInfo clear;
    AccessibilityNodeInfo enter;
    int windowId = -1;

    private OnScreenKeyboard() {
    }

    /** The keyboard on screen, or null. Main thread. */
    static OnScreenKeyboard find(AccessibilityService svc) {
        List<AccessibilityWindowInfo> windows = svc.getWindows();
        for (AccessibilityWindowInfo w : windows) {
            if (w.getType() != AccessibilityWindowInfo.TYPE_APPLICATION) {
                continue; /* not the system keyboard, overlays or system bars */
            }
            AccessibilityNodeInfo root = w.getRoot();
            if (root == null) {
                continue;
            }
            OnScreenKeyboard k = scan(root);
            if (k != null) {
                k.windowId = w.getId();
                return k;
            }
        }
        return null;
    }

    private static final class Item {
        final AccessibilityNodeInfo node;
        final AccessibilityNodeInfo actionable; /* the node or its nearest clickable/focusable ancestor */

        Item(AccessibilityNodeInfo node, AccessibilityNodeInfo actionable) {
            this.node = node;
            this.actionable = actionable;
        }
    }

    private static OnScreenKeyboard scan(AccessibilityNodeInfo root) {
        OnScreenKeyboard k = new OnScreenKeyboard();
        Map<String, AccessibilityNodeInfo> named = new HashMap<>(); /* special-key candidates */
        Map<AccessibilityNodeInfo, Rect> namedBounds = new HashMap<>();
        Rect letters = null;
        int seen = 0;
        ArrayDeque<Item> todo = new ArrayDeque<>();
        todo.add(new Item(root, actionable(root) ? root : null));
        while (!todo.isEmpty() && seen < MAX_NODES) {
            Item it = todo.removeFirst();
            AccessibilityNodeInfo n = it.node;
            seen++;
            if (!n.isVisibleToUser()) {
                continue;
            }
            AccessibilityNodeInfo act = actionable(n) ? n : it.actionable;
            String label = label(n);
            if (act != null && label.length() == 1 && Character.isLetterOrDigit(label.charAt(0))) {
                char c = Character.toLowerCase(label.charAt(0));
                if (!k.chars.containsKey(c)) {
                    k.chars.put(c, act);
                    if (c >= 'a' && c <= 'z') {
                        Rect r = new Rect();
                        act.getBoundsInScreen(r);
                        if (letters == null) {
                            letters = new Rect(r);
                        } else {
                            letters.union(r);
                        }
                    }
                }
            } else if (act != null) {
                String role = role(n, label);
                if (role != null && !named.containsKey(role)) {
                    named.put(role, act);
                    Rect r = new Rect();
                    act.getBoundsInScreen(r);
                    namedBounds.put(act, r);
                }
            }
            for (int i = 0; i < n.getChildCount(); i++) {
                AccessibilityNodeInfo child = n.getChild(i);
                if (child != null) {
                    todo.add(new Item(child, act));
                }
            }
        }
        if (k.letterCount() < MIN_LETTERS || letters == null) {
            return null;
        }
        /* Special keys count only inside the keyboard (half a key of margin),
         * so a "Search" tab or a voice-search button elsewhere isn't taken. */
        int margin = Math.max(8, letters.height() / 12);
        Rect area = new Rect(letters.left - margin, letters.top - margin, letters.right + margin,
                letters.bottom + letters.height() / 3);
        k.space = inside(named.get("space"), namedBounds, area);
        k.backspace = inside(named.get("backspace"), namedBounds, area);
        k.clear = inside(named.get("clear"), namedBounds, area);
        k.enter = inside(named.get("enter"), namedBounds, area);
        return k;
    }

    int letterCount() {
        int n = 0;
        for (char c = 'a'; c <= 'z'; c++) {
            if (chars.containsKey(c)) {
                n++;
            }
        }
        return n;
    }

    /** The key for a character, or null if this keyboard has none for it. */
    AccessibilityNodeInfo keyFor(char c) {
        if (c == ' ') {
            return space;
        }
        return chars.get(Character.toLowerCase(c));
    }

    private static AccessibilityNodeInfo inside(AccessibilityNodeInfo node, Map<AccessibilityNodeInfo, Rect> bounds,
            Rect area) {
        if (node == null) {
            return null;
        }
        Rect r = bounds.get(node);
        return r != null && area.contains(r.centerX(), r.centerY()) ? node : null;
    }

    private static boolean actionable(AccessibilityNodeInfo n) {
        return n.isClickable() || n.isFocusable();
    }

    private static String label(AccessibilityNodeInfo n) {
        CharSequence d = n.getContentDescription();
        if (d != null && d.toString().trim().length() > 0) {
            return d.toString().trim();
        }
        CharSequence t = n.getText();
        return t == null ? "" : t.toString().trim();
    }

    /** "space", "backspace", "clear", "enter" or null, from the label or the view id. */
    private static String role(AccessibilityNodeInfo n, String label) {
        String l = label.toLowerCase(Locale.ROOT);
        String id = n.getViewIdResourceName();
        id = id == null ? "" : id.substring(id.indexOf('/') + 1).toLowerCase(Locale.ROOT);
        if (l.equals("backspace") || l.equals("delete") || l.equals("del") || l.equals("\u232b")
                || id.endsWith("backspace") || id.endsWith("delete")) {
            return "backspace";
        }
        if (l.equals("space") || l.equals("\u2423") || id.endsWith("space")) {
            return "space";
        }
        if (l.equals("clear") || id.endsWith("clear")) {
            return "clear";
        }
        if (l.equals("search") || l.equals("enter") || l.equals("go") || l.equals("done") || l.equals("ok")
                || id.endsWith("-enter") || id.endsWith("_enter") || id.endsWith("key-search")
                || id.endsWith("key_search")) {
            return "enter";
        }
        return null;
    }
}
