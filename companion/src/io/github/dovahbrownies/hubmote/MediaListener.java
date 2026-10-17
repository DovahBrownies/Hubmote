package io.github.dovahbrownies.hubmote;

import android.service.notification.NotificationListenerService;

/**
 * Exists only so Android lets the companion read media sessions (what is
 * playing, NowPlaying): MediaSessionManager.getActiveSessions() requires an
 * enabled notification listener of the calling app. It reads no
 * notifications. install.ps1 / install.py enable it over ADB
 * (cmd notification allow_listener), as TV settings may not offer it.
 *
 * Android connects it once access is granted, which may be after the
 * companion started: then NowPlaying tries again.
 */
public final class MediaListener extends NotificationListenerService {
    /** Set by NowPlaying while it waits for access; main thread. */
    static Runnable onConnected;

    @Override
    public void onListenerConnected() {
        Runnable r = onConnected;
        if (r != null) {
            r.run();
        }
    }
}
