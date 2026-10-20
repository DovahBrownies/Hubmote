package io.github.dovahbrownies.hubmote;

import android.os.SystemClock;
import android.util.Log;

import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Locale;
import java.util.Map;

/**
 * The TV's internet speed (POST /speedtest, GET /speedtest, 0.7+), measured
 * against Cloudflare's speed test (speed.cloudflare.com): latency, then
 * download, then upload, each for a few seconds. One test at a time, on its
 * own thread (never Android's main thread). Progress and the result go to
 * the remote as {"speed":{...}} events.
 *
 * Connections are reused from request to request (a fully read answer
 * leaves its connection in the pool), and only the transfer is timed: on a
 * slow or lossy link a new TLS handshake can take seconds, which counted
 * in, made a download of a few Mbit/s look like a fraction of one
 * (before 0.9).
 */
final class SpeedTest {
    private static final String TAG = "HubmoteSpeed";
    private static final String BASE = "https://speed.cloudflare.com";
    private static final int PINGS = 11;              /* those on a new connection (its handshake) are dropped */
    private static final int PINGS_MIN = 3;           /* enough to report, on a poor line */
    private static final long PING_MS = 6000;         /* the latency's time once it has PINGS_MIN */
    private static final long PING_MAX_MS = 20000;    /* and at most, with whatever it has */
    private static final long PHASE_MS = 6000;        /* download, then upload */
    private static final long PHASE_BYTES = 250_000_000L; /* a fast line stops early */
    /* Request sizes, climbed while a request takes under a second. Cloudflare
     * refuses some sizes (403: 12, 15, 16 and 100 MB in 2026-10, with no rule
     * published); these were accepted. Uploads stop at 25 MB. */
    private static final long[] LADDER = {1_000_000L, 2_500_000L, 10_000_000L, 25_000_000L, 50_000_000L};
    private static final int UP_TOP = 3;
    private static final int REFUSED_MAX = 3; /* refusals in a row before giving up */
    private static final long REPORT_MS = 500;
    private static final int TIMEOUT_MS = 10000;

    private final CompanionService svc;
    private Thread thread;
    private String measId = "0"; /* tags a test's requests, as Cloudflare's own page does */
    /* What the remote sees; written by the test's thread */
    private volatile String state = "idle"; /* idle, latency, download, upload, done, failed */
    private volatile int progress;
    private volatile double pingMs, downMbps, upMbps;
    private long lastReport;
    private volatile boolean stopped; /* the service is going: end the test */

    SpeedTest(CompanionService svc) {
        this.svc = svc;
    }

    /** Starts a test; false while one runs. */
    synchronized boolean start() {
        if (thread != null && thread.isAlive()) {
            return false;
        }
        pingMs = downMbps = upMbps = 0;
        state = "latency"; /* not the last test's result while this one starts */
        progress = 0;
        stopped = false;
        measId = Long.toString((long) (Math.random() * 1e15));
        thread = new Thread(this::run, "hm-speed");
        thread.setDaemon(true);
        thread.start();
        return true;
    }

    /** Ends a running test soon (its request finishes or times out first):
     * the service is being unbound. */
    void stop() {
        stopped = true;
    }

    private void checkStopped() throws IOException {
        if (stopped) {
            throw new IOException("stopped");
        }
    }

    /** The {"speed":...} event line for a remote that just connected, or
     * null before any test. */
    String eventLine() {
        return state.equals("idle") ? null : "{\"speed\":" + json() + "}";
    }

    String json() {
        return String.format(Locale.ROOT, "{\"state\":\"%s\",\"progress\":%d,\"ping\":%.1f,\"down\":%.1f,\"up\":%.1f}",
                state, progress, pingMs, downMbps, upMbps);
    }

    private void run() {
        try {
            report("latency", 0, true);
            pingMs = latency();
            report("download", 10, true);
            downMbps = transfer(false, 10, 55);
            report("upload", 55, true);
            upMbps = transfer(true, 55, 100);
            report("done", 100, true);
            Log.i(TAG, String.format(Locale.ROOT, "%.0f ms, %.1f down, %.1f up", pingMs, downMbps, upMbps));
        } catch (IOException | RuntimeException e) {
            Log.w(TAG, "speed test failed", e);
            report("failed", progress, true);
        }
    }

    private void report(String newState, int newProgress, boolean now) {
        state = newState;
        progress = newProgress;
        long t = SystemClock.elapsedRealtime();
        if (now || t - lastReport >= REPORT_MS) {
            lastReport = t;
            svc.speedChanged(json());
        }
    }

    private static HttpURLConnection open(String path) throws IOException {
        HttpURLConnection c = (HttpURLConnection) new URL(BASE + path).openConnection();
        c.setConnectTimeout(TIMEOUT_MS);
        c.setReadTimeout(TIMEOUT_MS);
        c.setUseCaches(false);
        return c;
    }

    /** The median time to the answer of an empty download, less the time
     * Cloudflare says it spent on it (Server-Timing). */
    private double latency() throws IOException {
        List<Double> ms = new ArrayList<>();
        long start = SystemClock.elapsedRealtime();
        IOException last = null;
        boolean warm = false; /* the last ping left its connection open: this one reuses it */
        for (int i = 0; i < PINGS; i++) {
            checkStopped();
            long ran = SystemClock.elapsedRealtime() - start;
            if ((ms.size() >= PINGS_MIN && ran > PING_MS) || ran > PING_MAX_MS) {
                break; /* a slow line: enough samples, or as many as it will get */
            }
            long t0 = SystemClock.elapsedRealtimeNanos();
            HttpURLConnection c = open("/__down?measId=" + measId + "&bytes=0");
            boolean whole = false; /* answered and read: the connection serves the next ping */
            try {
                int status = c.getResponseCode();
                double took = (SystemClock.elapsedRealtimeNanos() - t0) / 1e6 - serverMs(c);
                if (status != 200) {
                    throw new IOException("HTTP " + status);
                }
                drain(c);
                whole = true;
                if (warm) { /* a ping on a new connection timed its handshake too */
                    ms.add(Math.max(0, took));
                }
                warm = true;
            } catch (IOException e) {
                last = e; /* a lost sample; the others count */
            } finally {
                if (!whole) {
                    c.disconnect();
                    warm = false;
                }
            }
            report("latency", 10 * (i + 1) / PINGS, false);
        }
        if (ms.isEmpty()) {
            throw last != null ? last : new IOException("no latency");
        }
        Collections.sort(ms);
        return ms.get(ms.size() / 2);
    }

    /** The time Cloudflare spent on the request, in ms: the largest "dur=" in
     * its Server-Timing headers ("cfSpeedEdge;dur=3, cfSpeedWorker;dur=22";
     * there is more than one header); 0 without them. */
    private static double serverMs(HttpURLConnection c) {
        double most = 0;
        for (Map.Entry<String, List<String>> h : c.getHeaderFields().entrySet()) {
            if (h.getKey() == null || !h.getKey().equalsIgnoreCase("Server-Timing")) {
                continue;
            }
            for (String timing : h.getValue()) {
                for (int at = timing.indexOf("dur="); at >= 0; at = timing.indexOf("dur=", at + 4)) {
                    int end = at + 4;
                    while (end < timing.length()
                            && (Character.isDigit(timing.charAt(end)) || timing.charAt(end) == '.')) {
                        end++;
                    }
                    try {
                        most = Math.max(most, Double.parseDouble(timing.substring(at + 4, end)));
                    } catch (NumberFormatException ignored) {
                    }
                }
            }
        }
        return most;
    }

    /**
     * Downloads (or uploads) ever larger amounts, up the LADDER while a
     * request takes under a second, for PHASE_MS or PHASE_BYTES; a request
     * still running at PHASE_MS is cut short and counts as far as it got. A
     * refused size is skipped (a rung down, and not climbed to again); a
     * request that fails (a stall the read timeout ends) ends the phase with
     * what was measured, or the test if nothing was. The result is the 90th
     * percentile of the requests that took 200 ms or more (a single slow
     * start doesn't count), in Mbit/s.
     *
     * Each request is timed from its first byte: a download's from its
     * answer's headers, an upload's from its connection being up. PHASE_MS
     * starts the same way, with the first request, so a slow handshake
     * doesn't use it up.
     */
    private double transfer(boolean up, int fromProgress, int toProgress) throws IOException {
        List<Double> speeds = new ArrayList<>();
        int rung = 0;
        int top = up ? UP_TOP : LADDER.length - 1;
        int refused = 0;
        long moved = 0;
        long timed = 0; /* ns the finished requests took, for a line too fast to time one */
        long start = 0; /* the phase's clock: from the first request's first byte */
        byte[] buf = new byte[64 * 1024];
        while ((start == 0 || SystemClock.elapsedRealtime() - start < PHASE_MS) && moved < PHASE_BYTES) {
            checkStopped();
            long size = LADDER[rung];
            long t0 = 0;
            long done = 0;
            boolean cut = false;   /* stopped at PHASE_MS */
            boolean whole = false; /* answered and read: the connection serves the next request */
            HttpURLConnection c = open((up ? "/__up?measId=" + measId : "/__down?measId=" + measId + "&bytes=" + size));
            try {
                try {
                    if (up) {
                        c.setDoOutput(true);
                        c.setRequestMethod("POST");
                        c.setRequestProperty("Content-Type", "application/octet-stream");
                        c.setFixedLengthStreamingMode(size);
                        c.connect(); /* the handshake, if any, before the clock */
                        t0 = SystemClock.elapsedRealtimeNanos();
                        if (start == 0) {
                            start = SystemClock.elapsedRealtime();
                        }
                        try (OutputStream out = c.getOutputStream()) {
                            while (done < size) {
                                if (over(start) || stopped) {
                                    /* counted now: closing a short body fails */
                                    measure(speeds, done, t0);
                                    cut = true;
                                    break;
                                }
                                int n = (int) Math.min(buf.length, size - done);
                                out.write(buf, 0, n);
                                done += n;
                                live(up, done, t0, start, fromProgress, toProgress);
                            }
                        }
                        if (cut) {
                            break;
                        }
                        if (refused(c.getResponseCode(), ++refused)) {
                            top = Math.min(top, Math.max(0, rung - 1));
                            rung = Math.max(0, rung - 1);
                            continue;
                        }
                        drain(c);
                        whole = true;
                    } else {
                        if (refused(c.getResponseCode(), ++refused)) {
                            top = Math.min(top, Math.max(0, rung - 1));
                            rung = Math.max(0, rung - 1);
                            continue;
                        }
                        t0 = SystemClock.elapsedRealtimeNanos(); /* the answer's headers are in */
                        if (start == 0) {
                            start = SystemClock.elapsedRealtime();
                        }
                        whole = true;
                        try (InputStream in = c.getInputStream()) {
                            for (int n; (n = in.read(buf)) > 0; ) {
                                done += n;
                                live(up, done, t0, start, fromProgress, toProgress);
                                if (over(start) || stopped) {
                                    whole = false; /* the rest stays unread: this connection goes */
                                    break; /* counted below, as far as it got */
                                }
                            }
                        }
                    }
                } catch (IOException e) {
                    if (cut && !speeds.isEmpty()) {
                        break; /* an upload cut at PHASE_MS: closing its short body fails, as expected */
                    }
                    if (speeds.isEmpty()) {
                        throw e;
                    }
                    Log.i(TAG, (up ? "upload" : "download") + " stalled; keeping " + speeds.size() + " measurements");
                    break;
                }
            } finally {
                if (!whole) {
                    c.disconnect();
                }
            }
            refused = 0;
            double seconds = (SystemClock.elapsedRealtimeNanos() - t0) / 1e9;
            timed += SystemClock.elapsedRealtimeNanos() - t0;
            moved += done;
            measure(speeds, done, t0);
            if (seconds < 1.0 && rung < top) {
                rung++;
            }
        }
        checkStopped();
        if (speeds.isEmpty() && moved > 0 && timed > 0) {
            /* every request was too quick to time alone: all of them together */
            return moved * 8 / (timed / 1e9) / 1e6;
        }
        if (speeds.isEmpty()) {
            throw new IOException("no measurement");
        }
        Collections.sort(speeds);
        return speeds.get(Math.min(speeds.size() - 1, (int) (speeds.size() * 0.9)));
    }

    private static boolean over(long start) {
        return start != 0 && SystemClock.elapsedRealtime() - start >= PHASE_MS;
    }

    /** Reads an answer to its end and closes it, which leaves its
     * connection in the pool for the next request. */
    private static void drain(HttpURLConnection c) throws IOException {
        try (InputStream in = c.getInputStream()) {
            byte[] skip = new byte[1024];
            while (in.read(skip) >= 0) {
                /* the answer itself isn't needed */
            }
        }
    }

    /** A request's speed, if it ran long enough (200 ms) to tell. */
    private static void measure(List<Double> speeds, long done, long t0) {
        double seconds = (SystemClock.elapsedRealtimeNanos() - t0) / 1e9;
        if (seconds >= 0.2) {
            speeds.add(done * 8 / seconds / 1e6);
        }
    }

    /** False for 200; true for a refusal to skip; an IOException after
     * REFUSED_MAX of them in a row. */
    private static boolean refused(int status, int inARow) throws IOException {
        if (status == 200) {
            return false;
        }
        if (inARow >= REFUSED_MAX) {
            throw new IOException("HTTP " + status);
        }
        Log.i(TAG, "refused: HTTP " + status);
        return true;
    }

    /** The current request's speed so far, and the phase's progress by time. */
    private void live(boolean up, long done, long t0, long start, int fromProgress, int toProgress) {
        long now = SystemClock.elapsedRealtime();
        if (now - lastReport < REPORT_MS) {
            return;
        }
        double seconds = (SystemClock.elapsedRealtimeNanos() - t0) / 1e9;
        if (seconds > 0.1) {
            double mbps = done * 8 / seconds / 1e6;
            if (up) {
                upMbps = mbps;
            } else {
                downMbps = mbps;
            }
        }
        long ran = start == 0 ? 0 : Math.min(PHASE_MS, now - start);
        report(up ? "upload" : "download", fromProgress + (int) ((toProgress - fromProgress) * ran / PHASE_MS), false);
    }
}
