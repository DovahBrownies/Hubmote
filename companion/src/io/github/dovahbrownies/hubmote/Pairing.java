package io.github.dovahbrownies.hubmote;

import android.content.Context;
import android.content.SharedPreferences;
import android.os.SystemClock;

import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.security.SecureRandom;
import java.util.ArrayList;
import java.util.List;

/**
 * Which remotes may use the API. A remote pairs once: the TV shows a code,
 * the user types it on the remote, and the remote gets a token it sends with
 * every request from then on. Tokens survive restarts; clearing the app's
 * storage (Settings > Apps) forgets them all.
 *
 * Guessing is slowed down: a code takes 3 wrong tries, a new request while
 * a code is showing gets the same code, and wrong tries count across codes:
 * after 6 (two codes' worth) every 3 more lock pairing for 1 minute, then
 * 2, 4... up to 4 hours (kept across restarts) until a remote pairs. So a
 * device on the network gets a few dozen guesses a day out of 16.8 million
 * codes.
 */
final class Pairing {
    static final int CODE_LEN = 6;
    static final long CODE_TTL_MS = 120_000;
    static final long START_GAP_MS = 5_000; /* one new code per 5 s at most */
    static final int TRIES = 3;             /* wrong codes before it is dropped */
    private static final int MAX_TOKENS = 8; /* oldest dropped first */
    private static final int FREE_WRONGS = 6; /* wrong tries before the lockout starts */
    private static final long LOCK_MIN_MS = 60_000;
    private static final long LOCK_MAX_MS = 4 * 3600_000L;
    private static final String HEX = "0123456789ABCDEF";

    /** What a code check came to. */
    enum Result { OK, WRONG, NO_CODE }

    private final SharedPreferences prefs;
    private final SecureRandom random = new SecureRandom();
    private final List<String> tokens = new ArrayList<>();
    private String code;      /* the code on screen, or null */
    private long codeExpires; /* elapsedRealtime */
    private long lastStart = -START_GAP_MS;
    private int triesLeft;
    private int wrongs; /* wrong tries since the last pairing */
    private long lockedUntil; /* currentTimeMillis: no new code before this */

    Pairing(Context context) {
        prefs = context.getSharedPreferences("pairing", Context.MODE_PRIVATE);
        String saved = prefs.getString("tokens", "");
        for (String t : saved.split(",")) {
            if (!t.isEmpty()) {
                tokens.add(t);
            }
        }
        wrongs = prefs.getInt("wrongs", 0);
        /* A clock set wrong at boot can't stretch the lock past its longest. */
        lockedUntil = Math.min(prefs.getLong("locked", 0), System.currentTimeMillis() + LOCK_MAX_MS);
    }

    /** Seconds until a new code can be made after too many wrong ones, or 0. */
    synchronized long lockedSeconds() {
        long left = lockedUntil - System.currentTimeMillis();
        return left > 0 ? (left + 999) / 1000 : 0;
    }

    /** True if the request's Authorization header carries a known token. */
    synchronized boolean authorized(String authorization) {
        if (authorization == null || !authorization.startsWith("Bearer ")) {
            return false;
        }
        byte[] given = authorization.substring(7).trim().getBytes(StandardCharsets.US_ASCII);
        boolean ok = false;
        for (String t : tokens) {
            /* constant time per token, and every token is compared */
            ok |= MessageDigest.isEqual(given, t.getBytes(StandardCharsets.US_ASCII));
        }
        return ok;
    }

    /** The code to show on the TV: the one showing, or a new one. Null if
     * one was made too recently, or while locked (lockedSeconds()). */
    synchronized String start() {
        long now = SystemClock.elapsedRealtime();
        if (code != null && now <= codeExpires) {
            return code; /* never replaced while it can still be typed */
        }
        if (now - lastStart < START_GAP_MS || lockedSeconds() > 0) {
            return null;
        }
        lastStart = now;
        StringBuilder b = new StringBuilder(CODE_LEN);
        for (int i = 0; i < CODE_LEN; i++) {
            b.append(HEX.charAt(random.nextInt(16)));
        }
        code = b.toString();
        codeExpires = now + CODE_TTL_MS;
        triesLeft = TRIES;
        return code;
    }

    /** Drops the code on screen (expired, cancelled or used). */
    synchronized void cancel() {
        code = null;
    }

    synchronized boolean showing(String c) {
        return code != null && code.equals(c);
    }

    /** Checks a typed code. On OK, token[0] is the remote's new token. */
    synchronized Result check(String typed, String[] token) {
        if (code == null || SystemClock.elapsedRealtime() > codeExpires) {
            code = null;
            return Result.NO_CODE;
        }
        byte[] a = typed == null ? new byte[0] : typed.trim().toUpperCase().getBytes(StandardCharsets.US_ASCII);
        if (!MessageDigest.isEqual(a, code.getBytes(StandardCharsets.US_ASCII))) {
            if (--triesLeft <= 0) {
                code = null;
            }
            wrong();
            return Result.WRONG;
        }
        code = null;
        wrongs = 0;
        lockedUntil = 0;
        byte[] raw = new byte[16];
        random.nextBytes(raw);
        StringBuilder b = new StringBuilder(32);
        for (byte x : raw) {
            b.append(HEX.charAt((x >> 4) & 15)).append(HEX.charAt(x & 15));
        }
        String t = b.toString();
        tokens.add(t);
        while (tokens.size() > MAX_TOKENS) {
            tokens.remove(0);
        }
        prefs.edit().putString("tokens", String.join(",", tokens)).putInt("wrongs", 0).putLong("locked", 0).apply();
        token[0] = t;
        return Result.OK;
    }

    /* Counted across codes, so taking a code down after 2 wrong tries and
     * asking for a new one doesn't help: from FREE_WRONGS on, every TRIES
     * wrong tries drop the code and lock pairing, twice as long each time. */
    private void wrong() {
        wrongs++;
        int over = wrongs - FREE_WRONGS;
        if (over >= 0 && over % TRIES == 0) {
            long ms = LOCK_MIN_MS << Math.min(over / TRIES, 20);
            lockedUntil = System.currentTimeMillis() + Math.min(ms, LOCK_MAX_MS);
            code = null;
        }
        prefs.edit().putInt("wrongs", wrongs).putLong("locked", lockedUntil).apply();
    }

    synchronized int triesLeft() {
        return code == null ? 0 : triesLeft;
    }
}
