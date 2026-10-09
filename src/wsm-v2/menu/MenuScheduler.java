package wsm;

/** All controller calls and timer callbacks run on one owner (Android's main thread). */
public interface MenuScheduler {
    interface Task { void cancel(); }
    long now();
    Task worker(Runnable task); // bounded; throws if saturated
    Task later(long delayMillis,Runnable task);
    void dispatch(Runnable task);
}
