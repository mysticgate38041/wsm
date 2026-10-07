package com.wsm.fixture;

import android.util.Log;

/** Entry point the WSM engine calls back into (2-way JNI proof for gate G3/G4). */
public class Probe {
    public static void engineAlive(String info) {
        Log.i("WSMFixture", "ENGINE CALLBACK: " + info);
        MainActivity.reportFromEngine(info);
    }
}
