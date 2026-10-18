package io.github.dovahbrownies.hubmote;

import android.animation.ValueAnimator;
import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.RectF;
import android.graphics.drawable.GradientDrawable;
import android.text.TextUtils;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.View;
import android.view.animation.LinearInterpolator;
import android.widget.LinearLayout;
import android.widget.TextView;

/** A message from the remote at the top right of the TV (POST /notify): its
 * text, and a ring that empties until the message goes away. */
final class Message {
    private Message() {
    }

    /** A ring that runs from full to empty over its animation. */
    private static final class Ring extends View {
        private final Paint track = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Paint arc = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final RectF box = new RectF();
        private float left = 1f;

        Ring(Context c, float stroke) {
            super(c);
            for (Paint p : new Paint[] {track, arc}) {
                p.setStyle(Paint.Style.STROKE);
                p.setStrokeWidth(stroke);
                p.setStrokeCap(Paint.Cap.ROUND);
            }
            track.setColor(0x33ECEEF2);
            arc.setColor(0xFFF2A93B);
        }

        void setLeft(float fraction) {
            left = fraction;
            invalidate();
        }

        @Override
        protected void onDraw(Canvas canvas) {
            float inset = arc.getStrokeWidth() / 2;
            box.set(inset, inset, getWidth() - inset, getHeight() - inset);
            canvas.drawArc(box, 0, 360, false, track);
            canvas.drawArc(box, -90, 360 * left, false, arc);
        }
    }

    /** The message's view; the ring starts emptying at once and takes ms. */
    static View build(Context c, String text, long ms) {
        float density = c.getResources().getDisplayMetrics().density;
        LinearLayout box = new LinearLayout(c);
        box.setOrientation(LinearLayout.HORIZONTAL);
        box.setGravity(Gravity.CENTER_VERTICAL);
        int pad = Math.round(18 * density);
        box.setPadding(pad, pad, Math.round(24 * density), pad);
        GradientDrawable bg = new GradientDrawable();
        bg.setColor(0xF0181A1F);
        bg.setCornerRadius(16 * density);
        box.setBackground(bg);

        int size = Math.round(28 * density);
        Ring ring = new Ring(c, 3.5f * density);
        LinearLayout.LayoutParams rlp = new LinearLayout.LayoutParams(size, size);
        rlp.rightMargin = Math.round(16 * density);
        box.addView(ring, rlp);

        LinearLayout texts = new LinearLayout(c);
        texts.setOrientation(LinearLayout.VERTICAL);
        TextView title = new TextView(c);
        title.setText("Hubmote");
        title.setTextSize(TypedValue.COMPLEX_UNIT_SP, 13);
        title.setTextColor(0xFFA3AAB6);
        texts.addView(title);
        TextView body = new TextView(c);
        body.setText(text);
        body.setTextSize(TypedValue.COMPLEX_UNIT_SP, 18);
        body.setTextColor(0xFFECEEF2);
        body.setMaxWidth(Math.round(420 * density));
        body.setMaxLines(4);
        body.setEllipsize(TextUtils.TruncateAt.END);
        texts.addView(body);
        box.addView(texts);

        ValueAnimator empty = ValueAnimator.ofFloat(1f, 0f);
        empty.setDuration(ms);
        empty.setInterpolator(new LinearInterpolator());
        empty.addUpdateListener(a -> ring.setLeft((Float) a.getAnimatedValue()));
        box.addOnAttachStateChangeListener(new View.OnAttachStateChangeListener() {
            @Override
            public void onViewAttachedToWindow(View v) {
                empty.start();
            }

            @Override
            public void onViewDetachedFromWindow(View v) {
                empty.cancel();
            }
        });
        return box;
    }
}
