package io.github.dovahbrownies.hubmote;

import android.content.ComponentName;
import android.content.Context;
import android.media.MediaDescription;
import android.media.MediaMetadata;
import android.media.session.MediaController;
import android.media.session.MediaSessionManager;
import android.media.session.PlaybackState;
import android.os.Handler;
import android.os.SystemClock;
import android.util.Log;

import java.text.Normalizer;
import java.util.ArrayList;
import java.util.List;

/**
 * What is playing, from Android's media sessions: the app, the title and
 * subtitle (episode, artist), and whether it plays. Apps publish these for
 * the system's own "now playing" card; some leave out the title.
 *
 * Needs notification-listener access (MediaListener); without it
 * available() is false and nothing is reported. Main thread only, except
 * json(), which any thread may call.
 */
final class NowPlaying {
    private static final int TITLE_MAX = 90; /* characters; the remote keeps 95 bytes */
    private final CompanionService svc;
    private final Handler main;
    private final ComponentName listener;
    private MediaSessionManager msm;
    private final List<MediaController> controllers = new ArrayList<>();
    private final MediaSessionManager.OnActiveSessionsChangedListener onSessions = this::setSessions;
    private final MediaController.Callback onChange = new MediaController.Callback() {
        @Override
        public void onPlaybackStateChanged(PlaybackState state) {
            update();
        }

        @Override
        public void onMetadataChanged(MediaMetadata metadata) {
            update();
        }

        @Override
        public void onSessionDestroyed() {
            update();
        }
    };
    private volatile boolean available;
    /* {"pkg":..,"title":..,"subtitle":..,"state":..,"position":ms,"duration":ms} */
    private volatile String json = "{\"state\":\"none\"}"; /* without the position */
    private volatile PlaybackState playback; /* the position is reckoned from it at each /media */
    private String reported = ""; /* json without the position, as last sent to the remote */

    NowPlaying(CompanionService svc, Handler main) {
        this.svc = svc;
        this.main = main;
        this.listener = new ComponentName(svc, MediaListener.class);
    }

    void start() {
        msm = (MediaSessionManager) svc.getSystemService(Context.MEDIA_SESSION_SERVICE);
        MediaListener.onConnected = this::start; /* access granted later: try again then */
        if (available) {
            return;
        }
        try {
            msm.addOnActiveSessionsChangedListener(onSessions, listener, main);
            available = true;
            Log.i(CompanionService.TAG, "now playing: reading media sessions");
            setSessions(msm.getActiveSessions(listener));
        } catch (SecurityException e) {
            Log.i(CompanionService.TAG, "now playing: no notification access yet (install.ps1 grants it)");
        }
    }

    void stop() {
        MediaListener.onConnected = null;
        if (msm != null && available) {
            msm.removeOnActiveSessionsChangedListener(onSessions);
        }
        setSessions(null);
        available = false;
    }

    boolean available() {
        return available;
    }

    String json() {
        String now = json;
        long position = position(playback);
        return position >= 0 && now.contains("\"pkg\"") ? now.substring(0, now.length() - 1) + ",\"position\":" + position + "}"
                : now;
    }

    private void setSessions(List<MediaController> list) {
        for (MediaController c : controllers) {
            c.unregisterCallback(onChange);
        }
        controllers.clear();
        if (list != null) {
            for (MediaController c : list) {
                c.registerCallback(onChange, main);
                controllers.add(c);
            }
        }
        update();
    }

    /** The session to show: the first one playing, else the first with a
     * title (paused), else none. */
    private void update() {
        MediaController best = null;
        for (MediaController c : controllers) {
            PlaybackState st = c.getPlaybackState();
            if (st != null && st.getState() == PlaybackState.STATE_PLAYING) {
                best = c;
                break;
            }
            if (best == null && title(c) != null) {
                best = c;
            }
        }
        String now;
        if (best == null) {
            now = "{\"state\":\"none\"}";
        } else {
            MediaMetadata m = best.getMetadata();
            MediaDescription d = m != null ? m.getDescription() : null;
            String title = title(best);
            String subtitle = d != null && d.getSubtitle() != null ? d.getSubtitle().toString()
                    : m != null ? m.getString(MediaMetadata.METADATA_KEY_ARTIST) : null;
            long duration = m != null ? m.getLong(MediaMetadata.METADATA_KEY_DURATION) : 0;
            now = "{\"pkg\":" + Apps.quote(best.getPackageName())
                    + ",\"title\":" + Apps.quote(plain(title))
                    + ",\"subtitle\":" + Apps.quote(plain(subtitle))
                    + ",\"state\":\"" + state(best.getPlaybackState()) + "\""
                    + ",\"duration\":" + Math.max(duration, 0) + "}";
        }
        playback = best != null ? best.getPlaybackState() : null;
        json = now;
        if (!now.equals(reported)) {
            reported = now;
            Log.i(CompanionService.TAG, "now playing: " + (best == null ? "nothing"
                    : best.getPackageName() + " " + state(best.getPlaybackState())));
            svc.mediaChanged(now);
        }
    }

    private static String title(MediaController c) {
        MediaMetadata m = c.getMetadata();
        if (m == null) {
            return null;
        }
        CharSequence t = m.getDescription() != null ? m.getDescription().getTitle() : null;
        if (t == null) {
            t = m.getString(MediaMetadata.METADATA_KEY_TITLE);
        }
        return t != null && t.length() > 0 ? t.toString() : null;
    }

    /** Accents dropped ("Pokémon" becomes "Pokemon"): the remote's fonts
     * have only ASCII and a few symbols. Other scripts stay as they are. */
    private static String plain(String s) {
        String p = s == null ? "" : Normalizer.normalize(s, Normalizer.Form.NFD).replaceAll("\\p{M}+", "");
        return p.length() > TITLE_MAX ? p.substring(0, TITLE_MAX - 1) + "…" : p;
    }

    private static String state(PlaybackState st) {
        if (st == null) {
            return "none";
        }
        switch (st.getState()) {
            case PlaybackState.STATE_PLAYING:
                return "playing";
            case PlaybackState.STATE_PAUSED:
                return "paused";
            case PlaybackState.STATE_BUFFERING:
            case PlaybackState.STATE_CONNECTING:
                return "buffering";
            case PlaybackState.STATE_STOPPED:
                return "stopped";
            default:
                return "none";
        }
    }

    /** The position now, in ms, or -1 if unknown. */
    private static long position(PlaybackState st) {
        if (st == null || st.getPosition() < 0) {
            return -1;
        }
        long pos = st.getPosition();
        if (st.getState() == PlaybackState.STATE_PLAYING) {
            pos += (long) ((SystemClock.elapsedRealtime() - st.getLastPositionUpdateTime()) * st.getPlaybackSpeed());
        }
        return pos;
    }
}
