package wsm;

import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import java.util.concurrent.ArrayBlockingQueue;
import java.util.concurrent.FutureTask;
import java.util.concurrent.ThreadFactory;
import java.util.concurrent.ThreadPoolExecutor;
import java.util.concurrent.TimeUnit;

/** Two workers allow a single slow status call and a command call to progress independently. */
public final class AndroidMenuScheduler implements MenuScheduler {
    private final Handler ui=new Handler(Looper.getMainLooper());
    private final ThreadPoolExecutor workers=new ThreadPoolExecutor(2,2,0,TimeUnit.SECONDS,
        new ArrayBlockingQueue<Runnable>(24),new ThreadFactory(){int count;
            public Thread newThread(Runnable r){Thread t=new Thread(r,"wsm-menu-"+(++count));t.setDaemon(true);return t;}});
    public long now(){return SystemClock.elapsedRealtime();}
    public Task worker(Runnable runnable){
        final FutureTask<Void> task=new FutureTask<Void>(runnable,null);workers.execute(task);
        return new Task(){public void cancel(){task.cancel(false);workers.remove(task);}};
    }
    public Task later(long delay,Runnable task){
        final Runnable callback=task;ui.postDelayed(callback,Math.max(0,delay));
        return new Task(){public void cancel(){ui.removeCallbacks(callback);}};
    }
    public void dispatch(Runnable task){ui.post(task);}
    /** Used by isolated previews/tests. Production retains one Activity-free scheduler. */
    public void close(){workers.shutdownNow();ui.removeCallbacksAndMessages(null);}
}
