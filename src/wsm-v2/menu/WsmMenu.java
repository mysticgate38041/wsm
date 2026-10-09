package wsm;

import android.app.Activity;
import android.app.Application;
import android.content.Context;
import android.content.pm.PackageInfo;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.view.ViewGroup;
import java.lang.ref.WeakReference;

/** Stable native entry points; Activity ownership and lifecycle wiring only. */
public final class WsmMenu {
    private static final Handler UI=new Handler(Looper.getMainLooper());
    private static Application application;
    private static MenuController controller;
    private static MenuPreferences preferences;
    private static MenuView view;
    private static WeakReference<Activity> owner=new WeakReference<Activity>(null);
    public static native String exec(String command);
    private WsmMenu(){}
    public static void show(final Context context){
        if(context==null)return;
        UI.post(new Runnable(){public void run(){initialize(context);if(context instanceof Activity)attach((Activity)context);}});
    }
    public static void showInActivity(final Activity activity){
        if(activity==null)return;
        UI.post(new Runnable(){public void run(){initialize(activity);attach(activity);}});
    }
    private static void initialize(Context context){
        if(application!=null)return;
        Context appContext=context.getApplicationContext();if(!(appContext instanceof Application))return;
        application=(Application)appContext;
        preferences=new MenuPreferences(application.getSharedPreferences("wsm_v6",0));
        controller=new MenuController(new NativeMenuBackend(),new AndroidMenuScheduler(),preferences.values(),preferences);
        try{PackageInfo info=application.getPackageManager().getPackageInfo(application.getPackageName(),0);
            long code=Build.VERSION.SDK_INT>=28?info.getLongVersionCode():info.versionCode;
            exec("__identity "+application.getPackageName()+" "+info.versionName+" "+code);
        }catch(Exception error){controller.notifyUser("Identitas aplikasi belum tersedia · "+error.getClass().getSimpleName());}
        application.registerActivityLifecycleCallbacks(new Application.ActivityLifecycleCallbacks(){
            public void onActivityResumed(Activity activity){attach(activity);}
            public void onActivityPaused(Activity activity){if(owner.get()==activity)detach();}
            public void onActivityDestroyed(Activity activity){if(owner.get()==activity)detach();}
            public void onActivityCreated(Activity activity,Bundle state){}
            public void onActivityStarted(Activity activity){}
            public void onActivityStopped(Activity activity){}
            public void onActivitySaveInstanceState(Activity activity,Bundle state){}
        });
    }
    private static void attach(Activity activity){
        if(controller==null||activity.isFinishing()||activity.isDestroyed()||(owner.get()==activity&&view!=null))return;
        ViewGroup parent=(ViewGroup)activity.findViewById(android.R.id.content);if(parent==null)return;
        if(view!=null)detach();
        owner=new WeakReference<Activity>(activity);
        exec("__activity resumed");
        view=new MenuView(activity,controller,preferences);view.attach(parent);controller.start(view);
    }
    private static void detach(){
        // Invalidate before notifying JNI or releasing widgets, so queued results are harmless.
        controller.stop();exec("__activity paused");
        if(view!=null){view.release();view=null;}
        owner.clear();
    }
}
