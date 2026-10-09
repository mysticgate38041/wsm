import wsm.*;
import java.util.*;
import java.util.concurrent.RejectedExecutionException;

/** Deterministic scheduling tests: no Android runtime, wall-clock sleeps, JNI, or game process. */
public final class MenuControllerTest {
    static void require(boolean value,String message){if(!value)throw new AssertionError(message);}
    static final class FakeScheduler implements MenuScheduler {
        static final class Job implements Task {
            final Runnable body;final long due;boolean cancelled;
            Job(Runnable body,long due){this.body=body;this.due=due;}
            public void cancel(){cancelled=true;}
            void run(){if(!cancelled)body.run();}
        }
        long time;int capacity=24;boolean inWorker;
        final List<Job> workers=new ArrayList<Job>();
        final List<Job> timers=new ArrayList<Job>();
        final List<Runnable> ui=new ArrayList<Runnable>();
        public long now(){return time;}
        public Task worker(Runnable body){int queued=0;for(Job job:workers)if(!job.cancelled)queued++;if(queued>=capacity)throw new RejectedExecutionException();Job job=new Job(body,0);workers.add(job);return job;}
        public Task later(long delay,Runnable body){Job job=new Job(body,time+delay);timers.add(job);return job;}
        public void dispatch(Runnable body){ui.add(body);}
        void oneWorker(){Job job=workers.remove(0);inWorker=true;try{job.run();}finally{inWorker=false;}}
        void drain(){int guard=0;while(!workers.isEmpty()||!ui.isEmpty()){require(++guard<1000,"scheduler drain bounded");while(!workers.isEmpty())oneWorker();while(!ui.isEmpty())ui.remove(0).run();}}
        void advance(long delta){long target=time+delta;int guard=0;while(true){Job next=null;for(Job job:timers)if(!job.cancelled&&job.due<=target&&(next==null||job.due<next.due))next=job;if(next==null)break;require(++guard<10000,"timer bounded");timers.remove(next);time=next.due;next.run();drain();}time=target;}
        void advanceTimersOnly(long delta){time+=delta;for(Job job:new ArrayList<Job>(timers))if(job.due<=time){timers.remove(job);job.run();}}
        int queuedWorkers(){int count=0;for(Job job:workers)if(!job.cancelled)count++;return count;}
        int scheduled(){int count=0;for(Job job:timers)if(!job.cancelled)count++;return count;}
    }
    static final class Backend implements MenuBackend {
        final FakeScheduler scheduler;
        long epoch=7,nextId;boolean ready=true,failStatus;
        int statusCalls,panicCalls;
        final List<String> commands=new ArrayList<String>();
        final Map<Long,String> outcomes=new LinkedHashMap<Long,String>();
        final Map<String,Value> features=new LinkedHashMap<String,Value>();
        Backend(FakeScheduler scheduler){this.scheduler=scheduler;for(FeatureDefinition f:FeatureDefinition.ALL)features.put(f.id,new Value(false,f.initial));}
        public Status status(){require(scheduler.inWorker,"status runs off UI owner");statusCalls++;if(failStatus)throw new IllegalStateException("status failure");return new Status(epoch,ready,ready?"ready":"scene_pending","fixture diagnostics",features);}
        public Reply execute(String command){require(scheduler.inWorker,"ordinary commands run off UI owner");commands.add(command);if(command.startsWith("result ")){long id=Long.parseLong(command.substring(7));String phase=outcomes.get(id);return new Reply(id,epoch,phase==null?"accepted":phase,"fixture "+phase);}long id=++nextId;return new Reply(id,0,"accepted","");}
        public Reply panic(){require(!scheduler.inWorker,"panic bypasses worker queue");panicCalls++;return new Reply(++nextId,0,"accepted","");}
    }
    static final class Harness implements MenuController.Listener {
        final FakeScheduler scheduler=new FakeScheduler();final Backend backend=new Backend(scheduler);
        final Map<String,Float> savedValues=new LinkedHashMap<String,Float>();
        final MenuController controller=new MenuController(backend,scheduler,Collections.<String,Float>emptyMap(),new MenuController.ValueSink(){public void save(String id,float value){savedValues.put(id,value);}});
        MenuSnapshot snapshot;int renders;
        public void render(MenuSnapshot snapshot){require(!scheduler.inWorker,"render belongs to UI owner");this.snapshot=snapshot;renders++;}
        Harness(){controller.start(this);scheduler.drain();require(snapshot.ready,"initial status ready");}
        MenuSnapshot.Control control(String id){return controller.snapshot().controls.get(id);}
    }
    static void definitions(){
        require(FeatureDefinition.ALL.size()==18,"exactly 18 controls");Set<String> ids=new HashSet<String>();for(FeatureDefinition f:FeatureDefinition.ALL)require(ids.add(f.id),"unique feature id");
        require(FeatureDefinition.find("god").command(true,1).equals("feat god 1.0"),"legacy feat contract");
        require(FeatureDefinition.find("speed").command(true,2).equals("speed 2.0"),"legacy direct contract");
        require(FeatureDefinition.find("nocd").command(false,1).equals("nocd 0.0"),"legacy direct off");
        FeatureDefinition f=FeatureDefinition.find("speed");require(f.constrain(Float.NaN)==2&&f.constrain(Float.POSITIVE_INFINITY)==2,"nonfinite becomes default");require(f.constrain(99)==5&&f.constrain(-1)==1,"range clamping");require(f.constrain(2.13f)==2.25f,"step quantization");
        try{FeatureDefinition.ALL.clear();throw new AssertionError("definitions mutable");}catch(UnsupportedOperationException expected){}
    }
    static void delayedCompletion(){
        Harness h=new Harness();h.controller.toggle("god");require(h.control("god").pending&&!h.control("god").applied,"accepted is never applied");h.scheduler.drain();
        require(h.backend.commands.get(0).equals("epoch 7 feat god 1.0"),"epoch-bound command");require(h.control("god").pending,"accepted missing epoch is valid");
        h.scheduler.advance(100);require(h.control("god").pending,"delayed backend completion stays pending");
        h.backend.outcomes.put(1L,"applied");h.scheduler.advance(200);require(h.control("god").applied&&!h.control("god").pending,"terminal applied changes state");
        h.controller.toggle("god");h.scheduler.drain();h.backend.outcomes.put(2L,"rejected");h.scheduler.advance(100);require(h.control("god").applied&&h.control("god").phase.equals("rejected"),"rejection preserves known applied state");
        h.backend.features.put("god",new MenuBackend.Value(true,1));h.controller.refreshStatus();h.scheduler.drain();require(h.control("god").phase.equals("rejected"),"poll preserves actionable error");
    }
    static void boundedPollAndLifecycle(){
        Harness h=new Harness();h.controller.refreshStatus();h.controller.refreshStatus();h.controller.refreshStatus();require(h.scheduler.queuedWorkers()==1,"single status in flight");
        h.scheduler.oneWorker();require(h.scheduler.ui.size()==1,"hold status delivery");int calls=h.backend.statusCalls;
        h.controller.stop();h.controller.start(h);h.controller.refreshStatus();require(h.scheduler.queuedWorkers()==0,"reattach cannot overlap old in-flight status");
        h.scheduler.drain();h.scheduler.advance(0);require(h.backend.statusCalls==calls+1&&h.controller.snapshot().ready,"fresh generation status follows old completion");
        h.controller.toggle("god");h.controller.stop();h.scheduler.drain();require(h.backend.commands.isEmpty(),"queued command canceled before JNI");
        require(!h.control("god").pending,"detach invalidates pending controls");require(h.scheduler.scheduled()==0,"detach removes timer callbacks");
        int renders=h.renders;h.scheduler.advance(10000);require(h.renders==renders,"detach clears listener");
    }
    static void inFlightCompletionAfterPause(){
        Harness h=new Harness();h.controller.toggle("god");h.scheduler.oneWorker();require(!h.scheduler.ui.isEmpty(),"accepted delivery queued");Runnable oldReply=h.scheduler.ui.remove(0);
        h.controller.stop();h.controller.start(h);h.scheduler.drain();oldReply.run();require(!h.control("god").pending&&!h.control("god").applied,"old accepted callback cannot restart polling");
        require(h.scheduler.scheduled()==1,"only status timer survives new generation");
    }
    static void epochChange(){
        Harness h=new Harness();h.controller.toggle("god");h.scheduler.drain();h.backend.epoch=8;h.controller.refreshStatus();h.scheduler.drain();require(!h.control("god").pending&&h.control("god").phase.equals("stale"),"epoch cancels pending feature");
        h.backend.outcomes.put(1L,"applied");h.scheduler.advance(1000);require(!h.control("god").applied,"old token cannot apply in new epoch");
        Harness r=new Harness();r.controller.toggle("god");r.scheduler.drain();r.backend.epoch=8;r.backend.outcomes.put(1L,"applied");r.scheduler.advance(100);require(!r.control("god").applied&&r.control("god").phase.equals("stale"),"completion epoch mismatch rejected");
    }
    static void staleSnapshotCannotUndoCompletion(){
        Harness h=new Harness();h.controller.toggle("god");h.scheduler.drain();h.controller.refreshStatus();h.scheduler.oneWorker();Runnable stale=h.scheduler.ui.remove(0);
        h.backend.outcomes.put(1L,"applied");h.scheduler.advance(100);require(h.control("god").applied,"new completion applied");stale.run();require(h.control("god").applied,"earlier captured status cannot undo new completion");
    }
    static void deadlineAndPressure(){
        Harness h=new Harness();h.controller.toggle("god");h.scheduler.advanceTimersOnly(35000);require(!h.control("god").pending&&h.control("god").phase.equals("timeout"),"deadline independent of worker progress");h.scheduler.drain();require(h.backend.commands.isEmpty(),"deadline cancels queued command");
        Harness p=new Harness();p.scheduler.capacity=0;p.controller.toggle("god");require(!p.control("god").pending&&p.control("god").phase.equals("busy"),"executor saturation releases pending state");
        Harness q=new Harness();q.scheduler.capacity=100;for(int i=0;i<21;i++)q.controller.action("selftest");require(q.scheduler.queuedWorkers()==20,"controller bounds outstanding commands");require(q.controller.snapshot().notice.contains("Antrean penuh"),"queue pressure is visible");q.controller.panic();require(q.backend.panicCalls==1,"panic bypasses saturated queue");require(q.scheduler.queuedWorkers()<=1,"panic cancels old work before refreshing status");q.controller.stop();
        Harness pending=new Harness();pending.controller.toggle("god");pending.scheduler.drain();pending.scheduler.advance(35000);require(pending.control("god").phase.equals("timeout")&&!pending.control("god").pending,"perpetually accepted results expire");
    }
    static MenuProfile profile(boolean god,boolean hp){Map<String,MenuProfile.Entry> entries=new LinkedHashMap<String,MenuProfile.Entry>();entries.put("god",new MenuProfile.Entry(god,1));entries.put("hp",new MenuProfile.Entry(hp,1));return new MenuProfile(entries);}
    static void profiles(){
        Harness h=new Harness();MenuProfile stored=profile(true,true);require(h.controller.loadProfile(stored),"profile begins manually");h.scheduler.drain();require(h.backend.nextId==1&&h.controller.snapshot().profileBusy,"profile submits only one request");
        h.controller.toggle("stam");h.scheduler.drain();require(h.backend.nextId==1,"manual toggles blocked during profile");require(h.controller.captureProfile()==null,"pending profile cannot be captured");
        h.backend.outcomes.put(1L,"applied");h.scheduler.advance(100);require(h.backend.nextId==2&&h.control("god").applied&&h.control("hp").pending,"profile advances after terminal success");
        h.backend.outcomes.put(2L,"rejected");h.scheduler.advance(100);require(!h.controller.snapshot().profileBusy&&!h.control("hp").applied&&h.control("god").applied,"profile failure stops without fake rollback");require(h.controller.snapshot().profileProgress.contains("1/2"),"partial profile reports progress");
        Harness complete=new Harness();complete.controller.loadProfile(stored);complete.scheduler.drain();complete.backend.outcomes.put(1L,"applied");complete.scheduler.advance(100);complete.backend.outcomes.put(2L,"applied");complete.scheduler.advance(100);require(!complete.controller.snapshot().profileBusy&&complete.controller.captureProfile().enabledCount()==2,"successful profile snapshot captures applied controls");
        Harness interrupted=new Harness();interrupted.controller.loadProfile(stored);interrupted.scheduler.drain();interrupted.controller.panic();require(!interrupted.controller.snapshot().profileBusy,"panic cancels profile continuation");interrupted.backend.outcomes.put(1L,"applied");interrupted.scheduler.advance(100);require(interrupted.backend.nextId==2&&!interrupted.control("hp").pending,"canceled profile never submits second control");
        Harness scene=new Harness();scene.controller.loadProfile(stored);scene.scheduler.drain();scene.backend.epoch++;scene.controller.refreshStatus();scene.scheduler.drain();require(!scene.controller.snapshot().profileBusy,"epoch interrupts profile");
        Harness empty=new Harness();require(!empty.controller.loadProfile(new MenuProfile(Collections.<String,MenuProfile.Entry>emptyMap())),"empty profile is not applied");
    }
    static void valuesAndSnapshots(){
        Harness h=new Harness();MenuSnapshot before=h.controller.snapshot();h.controller.setValue("speed",3.24f,true);require(h.control("speed").value==3.25f&&h.savedValues.get("speed")==3.25f,"draft value constrained and persisted");require(before.controls.get("speed").value==2,"snapshots detached from model");require(h.backend.commands.isEmpty(),"editing inactive slider never activates it");h.controller.resetInactiveValues();require(h.control("speed").value==2&&h.savedValues.get("speed")==2,"reset persists defaults");
        try{before.controls.clear();throw new AssertionError("snapshot mutable");}catch(UnsupportedOperationException expected){}
        MenuProfile capture=h.controller.captureProfile();try{capture.entries.clear();throw new AssertionError("profile mutable");}catch(UnsupportedOperationException expected){}
        h.backend.failStatus=true;h.controller.refreshStatus();h.scheduler.drain();require(!h.controller.snapshot().ready,"status error blocks stale session actions");h.controller.toggle("god");require(!h.control("god").pending,"unready session cannot enqueue toggle");
    }
    static void stalledStatusAndSliderDraft(){
        Harness stalled=new Harness();stalled.controller.refreshStatus();stalled.scheduler.advanceTimersOnly(5000);
        require(!stalled.controller.snapshot().ready&&stalled.controller.snapshot().session.equals("Status terlambat"),"stalled status disables stale session");
        stalled.controller.refreshStatus();require(stalled.scheduler.queuedWorkers()==1,"status watchdog never creates overlapping native requests");
        stalled.scheduler.drain();stalled.scheduler.advance(0);require(stalled.controller.snapshot().ready,"late status discarded then fresh status recovers");
        Harness slider=new Harness();slider.backend.features.put("speed",new MenuBackend.Value(true,2));slider.controller.refreshStatus();slider.scheduler.drain();
        slider.controller.setValue("speed",3.25f,false);slider.controller.refreshStatus();slider.scheduler.drain();require(slider.control("speed").value==3.25f,"status polling does not move an active slider draft");
        slider.controller.setValue("speed",3.25f,true);slider.scheduler.drain();slider.backend.outcomes.put(1L,"rejected");slider.scheduler.advance(100);
        require(slider.control("speed").value==2&&slider.control("speed").applied,"rejected active value returns to last confirmed value");
        slider.backend.failStatus=true;slider.controller.refreshStatus();slider.scheduler.drain();slider.controller.setValue("speed",4,true);require(slider.control("speed").value==2,"unready active slider cannot change confirmed display");
        Harness crowded=new Harness();crowded.backend.features.put("speed",new MenuBackend.Value(true,2));crowded.controller.refreshStatus();crowded.scheduler.drain();crowded.scheduler.capacity=100;for(int i=0;i<20;i++)crowded.controller.action("selftest");crowded.controller.setValue("speed",4,true);require(crowded.control("speed").value==2,"queue rejection rolls active slider back to confirmed value");
    }
    static void collapsedPolling(){
        Harness h=new Harness();int before=h.backend.statusCalls;h.controller.setVisible(false);h.scheduler.advance(4999);require(h.backend.statusCalls==before,"collapsed polling backs off to five seconds");h.scheduler.advance(1);require(h.backend.statusCalls==before+1,"collapsed snapshot still refreshes");
        h.controller.setVisible(true);h.scheduler.drain();require(h.backend.statusCalls==before+2,"expansion fetches fresh status immediately");
        h.controller.toggle("god");h.scheduler.drain();h.controller.setVisible(false);h.backend.outcomes.put(1L,"applied");h.scheduler.advance(100);require(h.control("god").applied,"collapse does not delay command completion");
        h.controller.refreshStatus();h.scheduler.oneWorker();h.controller.setVisible(true);h.scheduler.drain();int captured=h.backend.statusCalls;h.scheduler.advance(0);require(h.backend.statusCalls==captured+1,"expand during in-flight status requests one fresh follow-up");
    }
    static void mismatchedReplyInvalidatesPeers(){
        Harness h=new Harness();h.controller.toggle("god");h.controller.toggle("hp");h.scheduler.drain();
        h.backend.outcomes.put(1L,"applied");h.backend.outcomes.put(2L,"applied");
        h.scheduler.advanceTimersOnly(100);h.scheduler.oneWorker(); // god result captured under epoch 7
        Runnable oldGod=h.scheduler.ui.remove(0);h.backend.epoch=8;h.scheduler.oneWorker(); // hp result reports changed epoch
        h.scheduler.ui.remove(0).run();oldGod.run();
        require(!h.control("god").applied&&!h.control("hp").applied,"one mismatched reply invalidates every peer token");
        require(h.control("god").phase.equals("stale")&&h.control("hp").phase.equals("stale"),"both requests surface stale state");
    }
    public static void main(String[] args){definitions();delayedCompletion();boundedPollAndLifecycle();inFlightCompletionAfterPause();epochChange();staleSnapshotCannotUndoCompletion();deadlineAndPressure();profiles();valuesAndSnapshots();stalledStatusAndSliderDraft();collapsedPolling();mismatchedReplyInvalidatesPeers();System.out.println("PASS MenuController: definitions, delayed/missing-epoch replies, cancellation, lifecycle/status coalescing, epochs, stale snapshots, independent deadlines, pressure/PANIC, sequential profiles, immutable state, draft values");}
}
