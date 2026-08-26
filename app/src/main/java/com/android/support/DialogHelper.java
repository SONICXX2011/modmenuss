package com.android.support;

import android.annotation.SuppressLint;
import android.app.Activity;
import android.app.AlertDialog;
import android.content.Context;
import android.content.DialogInterface;
import android.content.Intent;
import android.net.Uri;
import android.os.Handler;
import android.widget.Toast;

public class DialogHelper {

    public static void showDialogWithLink(Context context, String title, String message,
                                          String LinkBtnTitle, String CloseBtnTitle,
                                          int sec, final String url) {
        // ✅ چک کن context از نوع Activity هست و زنده است
        if (context == null) {
            return;
        }

        // اگه context از نوع Activity نیست، Toast بزن و برگرد
        if (!(context instanceof Activity)) {
            Toast.makeText(context, "Dialog can only be shown from Activity", Toast.LENGTH_SHORT).show();
            return;
        }

        Activity activity = (Activity) context;
        if (activity.isFinishing() || activity.isDestroyed()) {
            Toast.makeText(context, "Activity is not available", Toast.LENGTH_SHORT).show();
            return;
        }

        AlertDialog.Builder builder = new AlertDialog.Builder(context)
                .setTitle(title)
                .setMessage(message);

        if (url != null) {
            builder.setPositiveButton(LinkBtnTitle, new DialogInterface.OnClickListener() {
                @Override
                public void onClick(DialogInterface dialog, int which) {
                    openUrlSafely(context, url);
                }
            });
        }

        final AlertDialog dialog = builder.create();

        String closeInitTitle = CloseBtnTitle;
        if (sec > 0) {
            closeInitTitle = CloseBtnTitle + "(" + sec + ")";
        }

        dialog.setButton(DialogInterface.BUTTON_NEGATIVE, closeInitTitle, new DialogInterface.OnClickListener() {
            @Override
            public void onClick(DialogInterface dialog, int which) {
                // فقط بسته بشه
            }
        });

        // ✅ با یه تأخیر کوتاه نشون بده تا Activity کاملاً آماده بشه
        new Handler(activity.getMainLooper()).postDelayed(new Runnable() {
            @Override
            public void run() {
                if (!activity.isFinishing() && !activity.isDestroyed()) {
                    dialog.show();
                    if (sec > 0) startCountdown(dialog, CloseBtnTitle, sec);
                }
            }
        }, 300);
    }

    private static void startCountdown(final AlertDialog dialog, String CloseBtnTitle, final int seconds) {
        final Handler handler = new Handler();
        final Runnable countdownRunnable = new Runnable() {
            int remainingTime = seconds;

            @SuppressLint("SetTextI18n")
            @Override
            public void run() {
                if (remainingTime > 0 && dialog != null && dialog.isShowing()) {
                    dialog.getButton(AlertDialog.BUTTON_NEGATIVE)
                            .setText(CloseBtnTitle + "(" + remainingTime + ")");
                    remainingTime--;
                    handler.postDelayed(this, 1000);
                } else if (dialog != null && dialog.isShowing()) {
                    dialog.getButton(AlertDialog.BUTTON_NEGATIVE).performClick();
                }
            }
        };
        handler.post(countdownRunnable);
    }

    @SuppressLint("QueryPermissionsNeeded")
    public static void openUrlSafely(Context context, String url) {
        if (url == null || url.trim().isEmpty()) return;

        if (!url.startsWith("http://") && !url.startsWith("https://")) {
            url = "http://" + url;
        }

        try {
            Intent intent = new Intent(Intent.ACTION_VIEW, Uri.parse(url));
            intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
            if (intent.resolveActivity(context.getPackageManager()) != null) {
                context.startActivity(intent);
            } else {
                Intent browserIntent = new Intent(Intent.ACTION_VIEW, Uri.parse(url));
                browserIntent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
                browserIntent.setPackage("com.android.chrome");
                if (browserIntent.resolveActivity(context.getPackageManager()) != null) {
                    context.startActivity(browserIntent);
                } else {
                    browserIntent.setPackage(null);
                    context.startActivity(browserIntent);
                }
            }
        } catch (Exception e) {
            Toast.makeText(context, e.toString(), Toast.LENGTH_SHORT).show();
        }
    }
}