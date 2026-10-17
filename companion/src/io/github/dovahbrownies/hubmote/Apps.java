package io.github.dovahbrownies.hubmote;

import android.content.Intent;
import android.content.pm.PackageManager;
import android.content.pm.ResolveInfo;
import android.graphics.Bitmap;
import android.graphics.BitmapShader;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Rect;
import android.graphics.RectF;
import android.graphics.Shader;
import android.graphics.drawable.AdaptiveIconDrawable;
import android.graphics.drawable.Drawable;

import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/** The TV apps on the home screen, and their icons in the remote's format. */
final class Apps {
    private Apps() {
    }

    /** {"apps":[{"pkg":..,"label":..,"updated":ms}]}, sorted by label. */
    static String listJson(PackageManager pm, String self) {
        Intent q = new Intent(Intent.ACTION_MAIN).addCategory(Intent.CATEGORY_LEANBACK_LAUNCHER);
        Map<String, String> labels = new LinkedHashMap<>();
        for (ResolveInfo ri : pm.queryIntentActivities(q, 0)) {
            String pkg = ri.activityInfo.packageName;
            if (!pkg.equals(self) && !labels.containsKey(pkg)) {
                labels.put(pkg, ri.loadLabel(pm).toString());
            }
        }
        List<Map.Entry<String, String>> apps = new ArrayList<>(labels.entrySet());
        apps.sort((a, b) -> a.getValue().compareToIgnoreCase(b.getValue()));

        StringBuilder sb = new StringBuilder("{\"apps\":[");
        for (int i = 0; i < apps.size(); i++) {
            String pkg = apps.get(i).getKey();
            long updated = 0;
            try {
                updated = pm.getPackageInfo(pkg, 0).lastUpdateTime;
            } catch (PackageManager.NameNotFoundException ignored) {
            }
            sb.append(i == 0 ? "" : ",").append("{\"pkg\":").append(quote(pkg)).append(",\"label\":")
                    .append(quote(apps.get(i).getValue())).append(",\"updated\":").append(updated).append('}');
        }
        return sb.append("]}").toString();
    }

    /**
     * The app's icon as an LVGL 9 RGB565A8 image: size x size RGB565 pixels
     * (little-endian), then size x size alpha bytes. Corners are rounded like
     * the remote's Home tiles (26 px at 112 px). Null if unknown.
     */
    static byte[] icon(PackageManager pm, String pkg, int size) {
        if (pkg == null) {
            return null;
        }
        Drawable d;
        try {
            Intent i = pm.getLeanbackLaunchIntentForPackage(pkg);
            d = i != null ? pm.getActivityIcon(i) : pm.getApplicationIcon(pkg);
        } catch (PackageManager.NameNotFoundException e) {
            return null;
        }

        Bitmap full = Bitmap.createBitmap(size, size, Bitmap.Config.ARGB_8888);
        Canvas c = new Canvas(full);
        if (d instanceof AdaptiveIconDrawable) {
            /* Layers are 108 dp with the middle 72 dp visible: draw them
             * full-bleed and let the rounded square below be the mask. */
            AdaptiveIconDrawable a = (AdaptiveIconDrawable) d;
            int pad = size / 4;
            Rect r = new Rect(-pad, -pad, size + pad, size + pad);
            for (Drawable layer : new Drawable[] {a.getBackground(), a.getForeground()}) {
                if (layer != null) {
                    layer.setBounds(r);
                    layer.draw(c);
                }
            }
        } else {
            c.drawColor(Color.WHITE); /* behind transparent icons */
            d.setBounds(0, 0, size, size);
            d.draw(c);
        }

        Bitmap out = Bitmap.createBitmap(size, size, Bitmap.Config.ARGB_8888);
        Paint p = new Paint(Paint.ANTI_ALIAS_FLAG);
        p.setShader(new BitmapShader(full, Shader.TileMode.CLAMP, Shader.TileMode.CLAMP));
        float radius = size * 26f / 112f;
        new Canvas(out).drawRoundRect(new RectF(0, 0, size, size), radius, radius, p);

        int n = size * size;
        int[] px = new int[n];
        out.getPixels(px, 0, size, 0, 0, size, size);
        byte[] data = new byte[n * 3];
        for (int i = 0; i < n; i++) {
            int argb = px[i];
            int r = (argb >> 16) & 0xFF;
            int g = (argb >> 8) & 0xFF;
            int b = argb & 0xFF;
            int v = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
            data[2 * i] = (byte) v;
            data[2 * i + 1] = (byte) (v >> 8);
            data[2 * n + i] = (byte) (argb >>> 24);
        }
        return data;
    }

    /** s as a JSON string, quoted. */
    static String quote(String s) {
        StringBuilder b = new StringBuilder("\"");
        for (int i = 0; i < s.length(); i++) {
            char ch = s.charAt(i);
            if (ch == '"' || ch == '\\') {
                b.append('\\').append(ch);
            } else if (ch < 0x20) {
                b.append(String.format("\\u%04x", (int) ch));
            } else {
                b.append(ch);
            }
        }
        return b.append('"').toString();
    }
}
