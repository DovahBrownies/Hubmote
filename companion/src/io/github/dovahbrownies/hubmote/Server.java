package io.github.dovahbrownies.hubmote;

import android.content.pm.PackageInfo;
import android.content.pm.PackageManager;
import android.os.SystemClock;
import android.util.Log;

import java.io.BufferedInputStream;
import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.io.UnsupportedEncodingException;
import java.net.InetAddress;
import java.net.InetSocketAddress;
import java.net.ServerSocket;
import java.net.Socket;
import java.net.URLDecoder;
import java.nio.charset.StandardCharsets;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.concurrent.CopyOnWriteArrayList;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Semaphore;

/**
 * A small HTTP/1.1 server for the remote, one thread per connection (at
 * most MAX_CONNECTIONS; a request must arrive within REQUEST_MS, with at
 * most MAX_HEADERS header lines).
 *
 * Every request but /status and /pair* needs "Authorization: Bearer <token>"
 * with a token from pairing (Pairing), or gets 401. Requests a browser could
 * send for a web page (an Origin or Sec-Fetch-* header, or a Host that isn't
 * an address, as with DNS rebinding) get 403: the remote sends none of them.
 *
 *   POST /pair/start          shows a code on the TV for 2 minutes: the one
 *                             already showing, or a new one (429 if the last
 *                             one is under 5 s old; 423 {"retry":s} while
 *                             locked after too many wrong codes, Pairing)
 *   POST /pair?code=C         {"token":T} if C is the code on the TV; 403
 *                             {"left":N} if not; 410 if no code is showing
 *                             (expired, used, or 3 wrong tries)
 *   POST /pair/cancel         hides the code
 *   GET  /status              {"version":5,"name":"0.5","updates":true,
 *                             "paired":false}, or with a token also
 *                             "field":..,"id":.. ("version" is the app's
 *                             version code; "updates": /update can work,
 *                             false where the TV forbids installs from apps;
 *                             "media": /media can work, with notification
 *                             access)
 *   GET  /media               what is playing: {"pkg":..,"title":..,
 *                             "subtitle":..,"state":"playing|paused|
 *                             buffering|stopped|none","duration":ms,
 *                             "position":ms}, or {"state":"none"}
 *   GET  /apps                {"apps":[{"pkg":..,"label":..,"updated":..}]}
 *   GET  /icon?pkg=P&size=N   the app's icon, N x N, as LVGL RGB565A8 bytes
 *   POST /launch?pkg=P        opens the app
 *   POST /text?value=T        replaces the focused field's text with T
 *   POST /search?q=Q          opens Google TV's own search with Q
 *   POST /enter               the focused field's Enter / Go / Search
 *   POST /update?url=U&size=N&sha256=H
 *                             updates the companion from the APK at U (N
 *                             bytes, that SHA-256), see Updater; 400 if the
 *                             request is unusable
 *   POST /notify?text=T&seconds=S
 *                             shows T at the top right of the TV for S
 *                             seconds (3-30, 10 if left out); 400 without
 *                             text or past 160 characters (0.7+)
 *   POST /speedtest           starts an internet speed test (SpeedTest);
 *                             409 while one runs (0.7+)
 *   GET  /speedtest           {"state":"idle|latency|download|upload|done|
 *                             failed","progress":0-100,"ping":ms,"down":
 *                             Mbit/s,"up":Mbit/s}
 *   POST /keyboard?hidden=1|0 hides (1) or allows (0) the TV's on-screen
 *                             keyboard; it comes back by itself when no
 *                             remote is connected to /events
 *   GET  /events              newline-delimited JSON, kept open:
 *                             {"field":"none|text|password|number","id":N}
 *                             whenever the kind or the field (id) changes,
 *                             {"update":"downloading|installing|confirm|failed"}
 *                             while it updates, {"media":{...}} (as /media,
 *                             without the position) when the title or state
 *                             changes, {"speed":{...}} (as /speedtest)
 *                             while a speed test runs (and on connecting,
 *                             once one has run, 0.8+), and an empty line
 *                             every 15 s
 */
final class Server {
    private static final int PING_MS = 15000;
    private static final int MAX_CONNECTIONS = 24;
    private static final int MAX_PER_ADDRESS = 8; /* one device can't take every slot */
    private static final int MAX_HEADERS = 32;
    private static final int MESSAGE_MAX = 160; /* /notify text, characters */
    private static final long REQUEST_MS = 10000;

    private final CompanionService svc;
    private final int port;
    private final String about; /* "version":5,"name":"0.5", for /status */
    private final List<OutputStream> streams = new CopyOnWriteArrayList<>();
    /* Event lines are written here: never on the main thread, where Android
     * forbids network use (NetworkOnMainThreadException). */
    private final ExecutorService sender = Executors.newSingleThreadExecutor();
    private volatile boolean running;
    private volatile ServerSocket socket;
    private final Semaphore slots = new Semaphore(MAX_CONNECTIONS);
    private final Map<InetAddress, Integer> perAddress = new HashMap<>();

    Server(CompanionService svc, int port) {
        this.svc = svc;
        this.port = port;
        long code = 0;
        String name = "";
        try {
            PackageInfo p = svc.getPackageManager().getPackageInfo(svc.getPackageName(), 0);
            code = Updater.code(p);
            name = p.versionName != null ? p.versionName : "";
        } catch (PackageManager.NameNotFoundException ignored) {
        }
        about = "\"version\":" + code + ",\"name\":\"" + name.replace("\"", "") + "\"";
    }

    void start() {
        running = true;
        daemon(this::acceptLoop, "hm-accept");
        daemon(this::pingLoop, "hm-ping");
    }

    void stop() {
        running = false;
        try {
            if (socket != null) {
                socket.close();
            }
        } catch (IOException ignored) {
        }
        for (OutputStream out : streams) {
            closeQuietly(out);
        }
        streams.clear();
    }

    void broadcast(String line) {
        sender.execute(() -> sendToAll(line));
    }

    private void sendToAll(String line) {
        byte[] b = (line + "\n").getBytes(StandardCharsets.UTF_8);
        for (OutputStream out : streams) {
            try {
                synchronized (out) {
                    out.write(b);
                    out.flush();
                }
            } catch (IOException e) {
                streams.remove(out);
                closeQuietly(out);
            }
        }
        if (streams.isEmpty()) {
            svc.hideKeyboard(false); /* no remote left to type with */
        }
    }

    private static void daemon(Runnable r, String name) {
        Thread t = new Thread(r, name);
        t.setDaemon(true);
        t.start();
    }

    private void acceptLoop() {
        while (running) {
            try (ServerSocket s = new ServerSocket()) {
                s.setReuseAddress(true);
                s.bind(new InetSocketAddress(port));
                socket = s;
                while (running) {
                    Socket c = s.accept();
                    InetAddress from = c.getInetAddress();
                    if (!take(from)) {
                        closeQuietly(c); /* too many at once */
                        continue;
                    }
                    try {
                        daemon(() -> {
                            try {
                                handle(c);
                            } finally {
                                give(from);
                            }
                        }, "hm-conn");
                    } catch (Throwable t) { /* no thread to be had (OutOfMemoryError) */
                        give(from);
                        closeQuietly(c);
                    }
                }
            } catch (IOException e) {
                if (!running) {
                    return;
                }
                Log.w(CompanionService.TAG, "server: " + e.getMessage() + ", retrying");
                sleep(2000);
            }
        }
    }

    private void pingLoop() {
        while (running) {
            sleep(PING_MS);
            broadcast("");
        }
    }

    private void handle(Socket c) {
        boolean keep = false;
        try {
            c.setSoTimeout(5000);
            c.setTcpNoDelay(true);
            InputStream in = new BufferedInputStream(c.getInputStream());
            OutputStream out = c.getOutputStream();
            long deadline = SystemClock.elapsedRealtime() + REQUEST_MS;
            String request = readLine(in, deadline);
            if (request == null) {
                return;
            }
            String authorization = null;
            boolean fromBrowser = false;
            int headers = 0;
            for (String h = readLine(in, deadline); h != null && !h.isEmpty(); h = readLine(in, deadline)) {
                if (++headers > MAX_HEADERS) {
                    json(out, 431, "{\"error\":\"too many headers\"}");
                    return;
                }
                if (h.regionMatches(true, 0, "Authorization:", 0, 14)) {
                    authorization = h.substring(14).trim();
                } else if (h.regionMatches(true, 0, "Origin:", 0, 7) || h.regionMatches(true, 0, "Sec-Fetch-", 0, 10)
                        || (h.regionMatches(true, 0, "Host:", 0, 5) && !isAddress(h.substring(5).trim()))) {
                    fromBrowser = true;
                }
            }
            if (fromBrowser) {
                json(out, 403, "{\"error\":\"not for browsers\"}");
                return;
            }
            String[] parts = request.split(" ");
            if (parts.length < 2) {
                return;
            }
            String method = parts[0];
            String target = parts[1];
            int qm = target.indexOf('?');
            String path = qm < 0 ? target : target.substring(0, qm);
            Map<String, String> q = query(qm < 0 ? "" : target.substring(qm + 1));
            if (q == null) {
                json(out, 400, "{\"error\":\"bad request\"}");
                return;
            }
            boolean paired = svc.pairing().authorized(authorization);

            if (method.equals("GET") && path.equals("/status")) {
                json(out, 200, "{" + about + ",\"updates\":" + svc.updater().possible()
                        + ",\"media\":" + svc.nowPlaying().available() + ",\"paired\":" + paired
                        + (paired ? "," + svc.fieldJson().substring(1) : "}"));
            } else if (method.equals("POST") && path.startsWith("/pair")) {
                pair(out, path, q);
            } else if (!paired) {
                json(out, 401, "{\"error\":\"pairing needed\"}");
            } else if (method.equals("GET") && path.equals("/media")) {
                json(out, 200, svc.nowPlaying().json());
            } else if (method.equals("GET") && path.equals("/apps")) {
                json(out, 200, Apps.listJson(svc.getPackageManager(), svc.getPackageName()));
            } else if (method.equals("GET") && path.equals("/icon")) {
                int size = parseInt(q.get("size"), 112);
                byte[] icon = size < 16 || size > 256 ? null : Apps.icon(svc.getPackageManager(), q.get("pkg"), size);
                if (icon == null) {
                    json(out, 404, "{\"error\":\"no icon\"}");
                } else {
                    send(out, 200, "application/octet-stream", icon);
                }
            } else if (method.equals("POST") && path.equals("/launch")) {
                String pkg = q.get("pkg");
                boolean ok = pkg != null && svc.launch(pkg);
                json(out, ok ? 200 : 404, ok ? "{\"ok\":true}" : "{\"error\":\"not launchable\"}");
            } else if (method.equals("POST") && path.equals("/text")) {
                String value = q.get("value");
                boolean ok = value != null && svc.setText(value);
                json(out, ok ? 200 : 404, ok ? "{\"ok\":true}" : "{\"error\":\"no focused field\"}");
            } else if (method.equals("POST") && path.equals("/keyboard")) {
                svc.hideKeyboard("1".equals(q.get("hidden")));
                json(out, 200, "{\"ok\":true}");
            } else if (method.equals("POST") && path.equals("/search")) {
                String query = q.get("q");
                boolean ok = query != null && !query.isEmpty() && svc.search(query);
                json(out, ok ? 200 : 404, ok ? "{\"ok\":true}" : "{\"error\":\"no search\"}");
            } else if (method.equals("POST") && path.equals("/update")) {
                boolean ok = svc.updater().start(q.get("url"), parseLong(q.get("size")), q.get("sha256"));
                json(out, ok ? 200 : 400, ok ? "{\"ok\":true}" : "{\"error\":\"bad request\"}");
            } else if (method.equals("POST") && path.equals("/speedtest")) {
                boolean ok = svc.speedTest().start();
                json(out, ok ? 200 : 409, ok ? "{\"ok\":true}" : "{\"error\":\"running\"}");
            } else if (method.equals("GET") && path.equals("/speedtest")) {
                json(out, 200, svc.speedTest().json());
            } else if (method.equals("POST") && path.equals("/notify")) {
                /* A message at the top right of the TV for 3 to 30 s (0.7+). */
                String text = q.get("text");
                int seconds = Math.max(3, Math.min(30, parseInt(q.get("seconds"), 10)));
                boolean ok = text != null && !text.isEmpty() && text.length() <= MESSAGE_MAX;
                if (ok) {
                    svc.showMessage(text, seconds * 1000L);
                }
                json(out, ok ? 200 : 400, ok ? "{\"ok\":true}" : "{\"error\":\"bad request\"}");
            } else if (method.equals("POST") && path.equals("/enter")) {
                boolean ok = svc.enter();
                json(out, ok ? 200 : 404, ok ? "{\"ok\":true}" : "{\"error\":\"no focused field\"}");
            } else if (method.equals("GET") && path.equals("/events")) {
                keep = true;
                events(c, in, out);
            } else {
                json(out, 404, "{\"error\":\"not found\"}");
            }
        } catch (IOException ignored) {
        } catch (RuntimeException e) {
            /* A bad request must never end the app (and with it the service). */
            Log.w(CompanionService.TAG, "request failed: " + e);
        } finally {
            if (!keep) {
                closeQuietly(c);
            }
        }
    }

    /** A connection slot for a client: false if all are taken, or this
     * address holds MAX_PER_ADDRESS already. */
    private boolean take(InetAddress from) {
        synchronized (perAddress) {
            int n = perAddress.getOrDefault(from, 0);
            if (n >= MAX_PER_ADDRESS || !slots.tryAcquire()) {
                return false;
            }
            perAddress.put(from, n + 1);
            return true;
        }
    }

    private void give(InetAddress from) {
        synchronized (perAddress) {
            int n = perAddress.getOrDefault(from, 1) - 1;
            if (n <= 0) {
                perAddress.remove(from);
            } else {
                perAddress.put(from, n);
            }
            slots.release();
        }
    }

    /** "192.168.1.60", "192.168.1.60:6470" or "[fe80::1]:6470": an address,
     * not a name (a name means DNS, which DNS rebinding abuses). */
    private static boolean isAddress(String host) {
        return host.matches("\\[[0-9A-Fa-f:.%a-zA-Z]+\\](:\\d+)?") || host.matches("[0-9.]+(:\\d+)?");
    }

    private void pair(OutputStream out, String path, Map<String, String> q) throws IOException {
        if (path.equals("/pair/start")) {
            String code = svc.pairing().start();
            long locked = svc.pairing().lockedSeconds();
            if (code == null && locked > 0) {
                Log.w(CompanionService.TAG, "pairing locked for " + locked + " s after wrong codes");
                json(out, 423, "{\"error\":\"locked\",\"retry\":" + locked + "}");
            } else if (code == null) {
                json(out, 429, "{\"error\":\"too soon\"}");
            } else {
                svc.showCode(code);
                json(out, 200, "{\"ok\":true}");
            }
        } else if (path.equals("/pair/cancel")) {
            svc.pairing().cancel();
            svc.hideCode();
            json(out, 200, "{\"ok\":true}");
        } else if (path.equals("/pair")) {
            String[] token = new String[1];
            Pairing.Result r = svc.pairing().check(q.get("code"), token);
            if (r == Pairing.Result.OK) {
                svc.hideCode();
                Log.i(CompanionService.TAG, "a remote paired");
                json(out, 200, "{\"token\":\"" + token[0] + "\"}");
            } else if (r == Pairing.Result.WRONG) {
                int left = svc.pairing().triesLeft();
                if (left == 0) {
                    svc.hideCode();
                }
                json(out, 403, "{\"error\":\"wrong code\",\"left\":" + left + "}");
            } else {
                svc.hideCode();
                json(out, 410, "{\"error\":\"no code\"}");
            }
        } else {
            json(out, 404, "{\"error\":\"not found\"}");
        }
    }

    /** Streams events until the client goes away. */
    private void events(Socket c, InputStream in, OutputStream out) throws IOException {
        String speed = svc.speedTest().eventLine(); /* how a test went, if one ran while it was away */
        out.write(("HTTP/1.1 200 OK\r\nContent-Type: application/x-ndjson\r\nCache-Control: no-cache\r\n"
                + "Connection: close\r\n\r\n" + svc.fieldJson() + "\n" + (speed != null ? speed + "\n" : ""))
                .getBytes(StandardCharsets.UTF_8));
        out.flush();
        streams.add(out);
        try {
            c.setSoTimeout(0);
            c.setKeepAlive(true);
            while (in.read() >= 0) {
                /* the client never sends anything; this returns when it closes */
            }
        } catch (IOException ignored) {
        } finally {
            streams.remove(out);
            closeQuietly(c);
            if (streams.isEmpty()) {
                svc.hideKeyboard(false); /* no remote left to type with */
            }
        }
    }

    private static void json(OutputStream out, int status, String body) throws IOException {
        send(out, status, "application/json", body.getBytes(StandardCharsets.UTF_8));
    }

    private static void send(OutputStream out, int status, String type, byte[] body) throws IOException {
        String reason = status == 200 ? "OK"
                : status == 400 ? "Bad Request"
                : status == 403 ? "Forbidden"
                : status == 423 ? "Locked"
                : status == 431 ? "Request Header Fields Too Large"
                : status == 401 ? "Unauthorized"
                : status == 403 ? "Forbidden"
                : status == 409 ? "Conflict"
                : status == 410 ? "Gone"
                : status == 429 ? "Too Many Requests"
                : "Not Found";
        out.write(("HTTP/1.1 " + status + " " + reason + "\r\nContent-Type: " + type + "\r\nContent-Length: "
                + body.length + "\r\nConnection: close\r\n\r\n").getBytes(StandardCharsets.UTF_8));
        out.write(body);
        out.flush();
    }

    /** One line, or null at the end of the stream. Throws once the request
     * has taken longer than its deadline (a client sending it byte by byte). */
    private static String readLine(InputStream in, long deadline) throws IOException {
        ByteArrayOutputStream b = new ByteArrayOutputStream();
        for (int ch = in.read(); ch >= 0; ch = in.read()) {
            if (SystemClock.elapsedRealtime() > deadline) {
                throw new IOException("request too slow");
            }
            if (ch == '\n') {
                String s = b.toString("UTF-8");
                return s.endsWith("\r") ? s.substring(0, s.length() - 1) : s;
            }
            if (b.size() >= 8192) {
                throw new IOException("line too long");
            }
            b.write(ch);
        }
        return b.size() > 0 ? b.toString("UTF-8") : null;
    }

    /** The query's parameters, or null if its encoding is broken. */
    private static Map<String, String> query(String s) throws UnsupportedEncodingException {
        Map<String, String> m = new HashMap<>();
        try {
            for (String kv : s.split("&")) {
                int eq = kv.indexOf('=');
                if (eq > 0) {
                    m.put(URLDecoder.decode(kv.substring(0, eq), "UTF-8"), URLDecoder.decode(kv.substring(eq + 1), "UTF-8"));
                }
            }
        } catch (IllegalArgumentException e) { /* "%" or "%zz" */
            return null;
        }
        return m;
    }

    private static int parseInt(String s, int dflt) {
        try {
            return s == null ? dflt : Integer.parseInt(s);
        } catch (NumberFormatException e) {
            return dflt;
        }
    }

    private static long parseLong(String s) {
        try {
            return s == null ? 0 : Long.parseLong(s);
        } catch (NumberFormatException e) {
            return 0;
        }
    }

    private static void sleep(long ms) {
        try {
            Thread.sleep(ms);
        } catch (InterruptedException ignored) {
        }
    }

    private static void closeQuietly(AutoCloseable c) {
        try {
            c.close();
        } catch (Exception ignored) {
        }
    }
}
