package io.github.dovahbrownies.hubmote;

import android.app.PendingIntent;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.pm.PackageInfo;
import android.content.pm.PackageInstaller;
import android.content.pm.PackageManager;
import android.os.Build;
import android.os.SystemClock;
import android.os.UserManager;
import android.util.Log;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.security.MessageDigest;
import java.util.Locale;
import java.util.concurrent.atomic.AtomicBoolean;

/**
 * Updates the companion itself when the remote asks (POST /update): downloads
 * the APK the remote names (from the project's release site), checks its
 * size and SHA-256, that it is this app and a newer version, and hands it to
 * Android's package installer. Android only installs it over this app if the
 * same key signed it.
 *
 * Android 12+ lets an app that updates itself skip the confirmation
 * (UPDATE_PACKAGES_WITHOUT_USER_ACTION), but only while it may also "install
 * unknown apps" (Settings > Apps > Special app access, or install.ps1 over
 * ADB). Otherwise the TV shows Android's install dialog, which also offers
 * that switch. Where the system forbids installs from apps altogether
 * (DISALLOW_INSTALL_UNKNOWN_SOURCES: Google TV sets it on some devices, and
 * then Android's installer closes without a word), possible() is false and
 * the remote doesn't offer the update. Progress goes to the remote over
 * /events:
 * {"update":"downloading"}, then "installing", "confirm" (the dialog is up)
 * or "failed". Android then stops this app and starts the new version,
 * which the remote sees in /status.
 */
final class Updater extends BroadcastReceiver {
    private static final String ACTION_DONE = "io.github.dovahbrownies.hubmote.UPDATE_DONE";
    /* A signature permission (AndroidManifest.xml): before Android 13 a
     * receiver registered at run time is open to every app, so only senders
     * holding it (this app, through the installer's PendingIntent) get in. */
    private static final String DONE_PERMISSION = "io.github.dovahbrownies.hubmote.permission.UPDATE_RESULT";
    private static final long MAX_BYTES = 32L * 1024 * 1024;
    private static final int TIMEOUT_MS = 20000;
    /* An update still "running" after this (a dialog left open) no longer
     * blocks a new one. */
    private static final long STUCK_MS = 10 * 60 * 1000;

    private final CompanionService svc;
    private final AtomicBoolean busy = new AtomicBoolean();
    private volatile long startedAt;
    /* Android's dialog is up: a new request (the user tapped Update again)
     * replaces this attempt. */
    private volatile boolean waiting;
    /* The session of the current attempt: answers about older ones (an
     * abandoned session reports "aborted") are ignored. */
    private volatile int sessionId = -1;

    Updater(CompanionService svc) {
        this.svc = svc;
    }

    void register() {
        abandonSessions(); /* an attempt from before a restart can't finish any more */
        File[] left = svc.getCacheDir().listFiles((dir, name) -> name.startsWith("update") && name.endsWith(".apk"));
        for (File f : left == null ? new File[0] : left) {
            f.delete(); /* a download cut short when the app stopped */
        }
        IntentFilter f = new IntentFilter(ACTION_DONE);
        if (Build.VERSION.SDK_INT >= 33) {
            svc.registerReceiver(this, f, Context.RECEIVER_NOT_EXPORTED);
        } else {
            svc.registerReceiver(this, f, DONE_PERMISSION, null);
        }
    }

    void unregister() {
        try {
            svc.unregisterReceiver(this);
        } catch (IllegalArgumentException ignored) {
        }
    }

    /** Whether this TV lets apps install at all (with or without asking). */
    boolean possible() {
        UserManager um = (UserManager) svc.getSystemService(Context.USER_SERVICE);
        return um == null || !(um.hasUserRestriction(UserManager.DISALLOW_INSTALL_UNKNOWN_SOURCES)
                || (Build.VERSION.SDK_INT >= 29
                        && um.hasUserRestriction(UserManager.DISALLOW_INSTALL_UNKNOWN_SOURCES_GLOBALLY)));
    }

    /** Starts an update from url (http or https); false if the request is
     * unusable or the TV forbids installs from apps. True also while an
     * update is already running. */
    boolean start(String url, long size, String sha256) {
        if (url == null || !(url.startsWith("https://") || url.startsWith("http://")) || size <= 0
                || size > MAX_BYTES || sha256 == null || !sha256.matches("[0-9a-fA-F]{64}")) {
            return false;
        }
        if (!possible()) {
            Log.w(CompanionService.TAG, "update: this TV doesn't let apps install (no unknown sources)");
            return false;
        }
        long now = SystemClock.elapsedRealtime();
        if (busy.get() && (waiting || now - startedAt > STUCK_MS)) {
            busy.set(false);
        }
        if (busy.compareAndSet(false, true)) {
            startedAt = now;
            waiting = false;
            String want = sha256.toLowerCase(Locale.ROOT);
            Thread t = new Thread(() -> run(url, size, want), "hm-update");
            t.setDaemon(true);
            t.start();
        }
        return true;
    }

    /** Not on the main thread: it downloads. */
    private void run(String url, long size, String sha256) {
        File apk = null;
        try {
            /* A file per attempt: one given up as stuck may still be writing its own. */
            apk = File.createTempFile("update", ".apk", svc.getCacheDir());
            svc.updateState("downloading");
            Log.i(CompanionService.TAG, "update: downloading " + url);
            download(url, apk, size, sha256);
            PackageManager pm = svc.getPackageManager();
            PackageInfo next = pm.getPackageArchiveInfo(apk.getPath(), 0);
            if (next == null || !svc.getPackageName().equals(next.packageName)) {
                throw new IOException("not this app");
            }
            long mine = code(pm.getPackageInfo(svc.getPackageName(), 0));
            if (code(next) <= mine) {
                throw new IOException("version " + code(next) + " isn't newer than " + mine);
            }
            svc.updateState("installing");
            Log.i(CompanionService.TAG, "update: installing " + next.versionName);
            install(apk);
        } catch (Exception e) {
            Log.w(CompanionService.TAG, "update failed: " + e);
            fail();
        } finally {
            if (apk != null && !apk.delete() && apk.exists()) {
                Log.w(CompanionService.TAG, "update: can't delete " + apk);
            }
        }
    }

    private void fail() {
        waiting = false;
        busy.set(false);
        svc.updateState("failed");
    }

    /** Drops this app's unfinished install sessions. */
    private void abandonSessions() {
        PackageInstaller pi = svc.getPackageManager().getPackageInstaller();
        for (PackageInstaller.SessionInfo s : pi.getMySessions()) {
            try {
                pi.abandonSession(s.getSessionId());
            } catch (RuntimeException ignored) {
            }
        }
    }

    private static void download(String url, File to, long size, String sha256) throws Exception {
        HttpURLConnection c = (HttpURLConnection) new URL(url).openConnection();
        c.setConnectTimeout(TIMEOUT_MS);
        c.setReadTimeout(TIMEOUT_MS);
        try {
            int status = c.getResponseCode();
            if (status != 200) {
                throw new IOException("HTTP " + status);
            }
            MessageDigest md = MessageDigest.getInstance("SHA-256");
            long got = 0;
            try (InputStream in = c.getInputStream(); OutputStream out = new FileOutputStream(to)) {
                byte[] buf = new byte[16384];
                for (int r; (r = in.read(buf)) > 0; ) {
                    got += r;
                    if (got > size) {
                        throw new IOException("longer than " + size + " bytes");
                    }
                    md.update(buf, 0, r);
                    out.write(buf, 0, r);
                }
            }
            if (got != size) {
                throw new IOException("got " + got + " of " + size + " bytes");
            }
            if (!hex(md.digest()).equals(sha256)) {
                throw new IOException("SHA-256 doesn't match");
            }
        } finally {
            c.disconnect();
        }
    }

    /** One install session for the APK; the answer comes to onReceive(). */
    private void install(File apk) throws IOException {
        abandonSessions();
        PackageInstaller pi = svc.getPackageManager().getPackageInstaller();
        PackageInstaller.SessionParams params =
                new PackageInstaller.SessionParams(PackageInstaller.SessionParams.MODE_FULL_INSTALL);
        params.setAppPackageName(svc.getPackageName());
        params.setSize(apk.length());
        if (Build.VERSION.SDK_INT >= 31) {
            params.setRequireUserAction(PackageInstaller.SessionParams.USER_ACTION_NOT_REQUIRED);
        }
        int id = pi.createSession(params);
        sessionId = id;
        try (PackageInstaller.Session session = pi.openSession(id)) {
            try (InputStream in = new FileInputStream(apk);
                 OutputStream out = session.openWrite("companion.apk", 0, apk.length())) {
                byte[] buf = new byte[16384];
                for (int r; (r = in.read(buf)) > 0; ) {
                    out.write(buf, 0, r);
                }
                session.fsync(out);
            }
            /* Explicit (this package) and mutable: the installer adds the result. */
            Intent done = new Intent(ACTION_DONE).setPackage(svc.getPackageName());
            int flags = PendingIntent.FLAG_UPDATE_CURRENT | (Build.VERSION.SDK_INT >= 31 ? PendingIntent.FLAG_MUTABLE : 0);
            session.commit(PendingIntent.getBroadcast(svc, id, done, flags).getIntentSender());
        } catch (IOException | RuntimeException e) {
            pi.abandonSession(id);
            throw e;
        }
    }

    /** The installer's answer. On success Android usually stops this app
     * before it arrives. */
    @Override
    public void onReceive(Context context, Intent intent) {
        int id = intent.getIntExtra(PackageInstaller.EXTRA_SESSION_ID, -1);
        if (id < 0 || id != sessionId) {
            return; /* an older attempt, or not the installer's answer */
        }
        int status = intent.getIntExtra(PackageInstaller.EXTRA_STATUS, PackageInstaller.STATUS_FAILURE);
        if (status == PackageInstaller.STATUS_PENDING_USER_ACTION) {
            Intent confirm = confirmIntent(intent);
            if (confirm != null) {
                /* It opens the installer's dialog: it needs no access to
                 * anything of this app's. */
                confirm.setFlags(confirm.getFlags() & ~(Intent.FLAG_GRANT_READ_URI_PERMISSION
                        | Intent.FLAG_GRANT_WRITE_URI_PERMISSION | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION
                        | Intent.FLAG_GRANT_PREFIX_URI_PERMISSION));
                try {
                    svc.startActivity(confirm.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK));
                    Log.i(CompanionService.TAG, "update: Android asks on the TV");
                    waiting = true;
                    svc.updateState("confirm");
                    return;
                } catch (RuntimeException e) {
                    Log.w(CompanionService.TAG, "update: can't show the install dialog", e);
                }
            }
            fail();
        } else if (status == PackageInstaller.STATUS_SUCCESS) {
            waiting = false;
            busy.set(false);
            Log.i(CompanionService.TAG, "update: installed");
        } else {
            Log.w(CompanionService.TAG, "update: not installed (" + status + "): "
                    + intent.getStringExtra(PackageInstaller.EXTRA_STATUS_MESSAGE));
            fail();
        }
    }

    @SuppressWarnings("deprecation")
    private static Intent confirmIntent(Intent i) {
        return Build.VERSION.SDK_INT >= 33 ? i.getParcelableExtra(Intent.EXTRA_INTENT, Intent.class)
                : (Intent) i.getParcelableExtra(Intent.EXTRA_INTENT);
    }

    @SuppressWarnings("deprecation")
    static long code(PackageInfo p) {
        return Build.VERSION.SDK_INT >= 28 ? p.getLongVersionCode() : p.versionCode;
    }

    private static String hex(byte[] b) {
        StringBuilder s = new StringBuilder(b.length * 2);
        for (byte x : b) {
            s.append(Character.forDigit((x >> 4) & 15, 16)).append(Character.forDigit(x & 15, 16));
        }
        return s.toString();
    }
}
