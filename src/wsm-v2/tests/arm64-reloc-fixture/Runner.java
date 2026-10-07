package com.wsm.relocprobe;
import android.app.Instrumentation;
import android.os.Bundle;
public final class Runner extends Instrumentation {
    static native int test();
    @Override public void onCreate(Bundle arguments){super.onCreate(arguments);start();}
    @Override public void onStart(){
        Bundle results=new Bundle();
        try {
            System.loadLibrary("wsmrelocprobe");int rc=test();
            results.putInt("nativeResult",rc);
            results.putString("scope","ARM64_SYNTHETIC_ONLY_NO_GAME_MEMORY");
            results.putString("status",rc==0?"PASS":"FAIL");finish(rc==0?-1:0,results);
        }catch(Throwable error){results.putString("status","FAIL");results.putString("error",error.toString());finish(0,results);}
    }
}
