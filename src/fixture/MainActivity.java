package com.wsm.fixture;

import android.app.Activity;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;
import android.widget.TextView;

/** Minimal fixture activity: shows handshake status reported by WSM engine. */
public class MainActivity extends Activity {
    private static TextView statusView;
    private static final Handler HANDLER = new Handler(Looper.getMainLooper());

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        statusView = new TextView(this);
        statusView.setTextSize(16f);
        statusView.setPadding(48, 96, 48, 48);
        statusView.setText("WSM Fixture ready\nwaiting for engine handshake...");
        setContentView(statusView);
        Log.i("WSMFixture", "activity created pid=" + android.os.Process.myPid());
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
        statusView = null;
    }

    /** Called from WSM engine (native thread) via Probe.engineAlive. */
    static void reportFromEngine(final String info) {
        HANDLER.post(new Runnable() {
            @Override
            public void run() {
                if (statusView != null) {
                    statusView.append("\n[engine] " + info);
                }
            }
        });
    }
}
