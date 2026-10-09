package wsm;

import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/** Activity-free controller. All public methods and completions belong to the UI owner thread. */
public final class MenuController {
    public interface Listener { void render(MenuSnapshot snapshot); }
    public interface ValueSink { void save(String id,float value); }
    private static final int MAX_PENDING=20;
    private static final long TIMEOUT_MS=35000, STATUS_MS=1000;
    private static final class Model {
        final FeatureDefinition definition;
        final ControlState state=new ControlState();
        float value,confirmedValue; boolean editing;
        String detail="";
        Model(FeatureDefinition f,float value){definition=f;this.value=f.constrain(value);this.confirmedValue=this.value;}
    }
    private static final class Request {
        final long localId,generation,epoch,deadline,token;
        final String command;
        final Model model;
        final boolean profile;
        long nativeId;
        int attempts;
        MenuScheduler.Task work,timer,timeout;
        Request(long id,long generation,long epoch,long deadline,long token,String command,Model model,boolean profile){
            localId=id;this.generation=generation;this.epoch=epoch;this.deadline=deadline;
            this.token=token;this.command=command;this.model=model;this.profile=profile;
        }
        void cancel(){if(work!=null)work.cancel();if(timer!=null)timer.cancel();if(timeout!=null)timeout.cancel();}
    }
    private final MenuBackend backend;
    private final MenuScheduler scheduler;
    private final ValueSink values;
    private final LinkedHashMap<String,Model> models=new LinkedHashMap<String,Model>();
    private final LinkedHashMap<Long,Request> requests=new LinkedHashMap<Long,Request>();
    private Listener listener;
    private volatile long generation;
    private volatile boolean attached;
    private boolean ready,statusInFlight,visible=true,refreshAfterStatus;
    private long epoch=-1,serial,mutation;
    private String session="Menunggu sesi",diagnostics="Belum ada snapshot",notice="18 kontrol · aktivasi manual";
    private MenuScheduler.Task statusTimer,statusTimeout;
    private boolean statusExpired;
    private List<Map.Entry<String,MenuProfile.Entry>> profile;
    private int profileIndex,profileTotal;
    private String profileProgress="";

    public MenuController(MenuBackend backend,MenuScheduler scheduler,Map<String,Float> initial,ValueSink values){
        this.backend=backend;this.scheduler=scheduler;this.values=values;
        for(FeatureDefinition f:FeatureDefinition.ALL){Float v=initial.get(f.id);models.put(f.id,new Model(f,v==null?f.initial:v));}
    }
    public void start(Listener listener){
        stop();this.listener=listener;attached=true;ready=false;session="Menghubungkan";
        emit();requestStatus();
    }
    /** Invalidates first; a worker already inside JNI may finish but cannot update this session. */
    public void stop(){
        attached=false;++generation;ready=false;refreshAfterStatus=false;
        if(statusTimer!=null){statusTimer.cancel();statusTimer=null;}
        if(statusTimeout!=null){statusTimeout.cancel();statusTimeout=null;}
        cancelRequests("stale");profile=null;profileProgress="";listener=null;
        for(Model m:models.values())m.editing=false;
        // Never reset statusInFlight here: an old JNI status call must finish before another starts.
    }
    public MenuSnapshot snapshot(){
        LinkedHashMap<String,MenuSnapshot.Control> copy=new LinkedHashMap<String,MenuSnapshot.Control>();
        for(Model m:models.values())copy.put(m.definition.id,new MenuSnapshot.Control(m.state,m.value,m.detail));
        return new MenuSnapshot(copy,attached,ready,epoch,session,diagnostics,notice,profile!=null,profileProgress,requests.size());
    }
    private void emit(){if(listener!=null&&attached)listener.render(snapshot());}
    public void toggle(String id){Model m=models.get(id);if(m!=null)submit(m.definition.command(!m.state.applied,m.value),m,!m.state.applied,false);}
    public void setValue(String id,float value,boolean commit){
        Model m=models.get(id);if(m==null||!m.definition.hasValue()||m.state.pending||profile!=null)return;
        if(m.state.applied&&!ready){notice="Tunggu sesi siap sebelum mengubah nilai kontrol aktif";emit();return;}
        m.editing=!commit;m.value=m.definition.constrain(value);if(commit){saveValue(m);if(m.state.applied&&!submit(m.definition.command(true,m.value),m,true,false)){m.value=m.confirmedValue;saveValue(m);}}
        emit();
    }
    private void saveValue(Model m){if(values!=null)values.save(m.definition.id,m.value);}
    public void resetInactiveValues(){
        if(profile!=null||!requests.isEmpty()){notice="Tunggu perintah selesai sebelum mereset nilai";emit();return;}
        for(Model m:models.values())if(!m.state.applied){m.editing=false;m.value=m.definition.initial;saveValue(m);}
        notice="Nilai kontrol nonaktif dikembalikan ke default";emit();
    }
    public void action(String command){
        if(!"sweep".equals(command)&&!"selftest".equals(command)&&!"tpr -15 0".equals(command)&&!"tpr 15 0".equals(command)&&!"tpr 0 15".equals(command)&&!"tpr 0 -15".equals(command))return;
        submit(command,null,false,false);
    }
    private boolean submit(String command,Model model,boolean desired,boolean fromProfile){
        if(!attached||!ready||epoch<=0){notice="Sesi belum siap; tunggu snapshot terbaru";emit();return false;}
        if((profile!=null&&!fromProfile)||(model!=null&&model.state.pending)){notice="Perintah sebelumnya masih menunggu hasil";emit();return false;}
        if(requests.size()>=MAX_PENDING){notice="Antrean penuh; coba lagi setelah hasil diterima";emit();return false;}
        long token=model==null?0:model.state.begin(desired);
        if(model!=null)model.detail="Menunggu konfirmasi backend";
        Request request=new Request(++serial,generation,epoch,scheduler.now()+TIMEOUT_MS,token,command,model,fromProfile);
        requests.put(request.localId,request);armTimeout(request);++mutation;emit();run(request,false);return true;
    }
    private void armTimeout(final Request request){request.timeout=scheduler.later(TIMEOUT_MS,new Runnable(){public void run(){finish(request,"timeout","Batas waktu tercapai; periksa status terbaru");}});}
    private boolean current(Request r){return attached&&r.generation==generation&&requests.get(r.localId)==r;}
    private void run(final Request request,final boolean result){
        if(!current(request))return;
        if(scheduler.now()>=request.deadline){finish(request,"timeout","Batas waktu tercapai; periksa status terbaru");return;}
        try{
            request.work=scheduler.worker(new Runnable(){public void run(){
                if(!attached||request.generation!=generation)return;
                MenuBackend.Reply reply=null;String failure=null;
                try{reply=backend.execute(result?"result "+request.nativeId:"epoch "+request.epoch+" "+request.command);}
                catch(Exception error){failure=error.getClass().getSimpleName();}
                final MenuBackend.Reply response=reply;final String fault=failure;
                scheduler.dispatch(new Runnable(){public void run(){
                    if(!current(request))return;
                    if(fault!=null||response==null){finish(request,"fault",fault==null?"Respons kosong":fault);return;}
                    receive(request,response);
                }});
            }});
        }catch(RuntimeException busy){finish(request,"busy","Antrean backend penuh; coba lagi");}
    }
    private void receive(final Request request,MenuBackend.Reply reply){
        if(!current(request))return;
        // Accepted responses currently omit epoch. A supplied epoch must still match.
        if(request.epoch>0&&reply.epoch>0&&reply.epoch!=request.epoch){
            invalidateSession(reply.epoch,"Scene berubah sebelum hasil diterima");return;
        }
        if("stale".equals(reply.state)){invalidateSession(reply.epoch,"Hasil berasal dari sesi yang sudah berubah");return;}
        if("accepted".equals(reply.state)){
            if(reply.id<=0||(request.nativeId>0&&reply.id!=request.nativeId)){finish(request,"fault","ID hasil tidak valid");return;}
            request.nativeId=reply.id;
            if(scheduler.now()>=request.deadline){finish(request,"timeout","Batas waktu tercapai; periksa status terbaru");return;}
            long delay=Math.min(750,100L<<Math.min(request.attempts++,3));
            delay=Math.min(delay,request.deadline-scheduler.now());
            request.timer=scheduler.later(delay,new Runnable(){public void run(){request.timer=null;MenuController.this.run(request,true);}});
        }else{
            if(request.nativeId>0&&reply.id>0&&reply.id!=request.nativeId){finish(request,"fault","ID hasil tidak cocok");return;}
            String phase=reply.state;
            if(!"applied".equals(phase)&&!"rejected".equals(phase)&&!"stale".equals(phase)&&!"fault".equals(phase)&&!"timeout".equals(phase))phase="fault";
            finish(request,phase,reply.detail);
        }
    }
    private void invalidateSession(long observedEpoch,String message){
        ++generation;cancelRequests("stale");profile=null;profileProgress="";ready=false;
        if(observedEpoch>epoch)epoch=observedEpoch;
        session="Scene berubah";notice=message;requestStatus();emit();
    }
    private void finish(Request request,String phase,String detail){
        if(!current(request))return;
        requests.remove(request.localId);request.cancel();++mutation;
        if(request.model!=null){Model m=request.model;m.state.finish(request.token,phase);m.detail=detail==null?phase:detail;
            if("applied".equals(phase))m.confirmedValue=m.value;else if(m.state.applied&&m.definition.hasValue()){m.value=m.confirmedValue;saveValue(m);}}
        notice=(request.model==null?request.command:request.model.definition.name)+" · "+phase+(detail==null||detail.isEmpty()?"":" · "+detail);
        if(request.profile&&profile!=null){
            if("applied".equals(phase)){profileIndex++;nextProfile();}
            else{profileProgress="Profil berhenti · "+profileIndex+"/"+profileTotal+" diterapkan";profile=null;}
        }
        emit();
    }
    private void cancelRequests(String outcome){
        for(Request r:requests.values()){r.cancel();if(r.model!=null){r.model.state.finish(r.token,outcome);r.model.detail="Permintaan dibatalkan saat sesi berubah";}}
        requests.clear();++mutation;
    }
    public void panic(){
        if(!attached)return;
        ++generation;cancelRequests("stale");profile=null;profileProgress="";ready=false;
        if(statusTimer!=null){statusTimer.cancel();statusTimer=null;}
        session="PANIC · menunggu reset";notice="PANIC dikirim; menunggu konfirmasi backend";emit();
        // Native PANIC invalidates the epoch synchronously; it must never wait behind IO work.
        try{
            MenuBackend.Reply reply=backend.panic();
            Request request=new Request(++serial,generation,0,scheduler.now()+TIMEOUT_MS,0,"panic",null,false);
            requests.put(request.localId,request);armTimeout(request);receive(request,reply);
        }catch(Exception error){notice="PANIC · fault · "+error.getClass().getSimpleName();}
        requestStatus();emit();
    }
    public void refreshStatus(){if(attached)requestStatus();}
    /** Hidden panels poll less often; request completion/deadline timers are unaffected. */
    public void setVisible(boolean value){
        if(visible==value)return;visible=value;
        if(!attached)return;
        if(value){if(statusInFlight)refreshAfterStatus=true;else requestStatus();}
        else if(!statusInFlight)scheduleStatus(statusInterval());
    }
    private long statusInterval(){return visible?STATUS_MS:5000;}
    private void requestStatus(){
        if(!attached||statusInFlight)return;
        if(statusTimer!=null){statusTimer.cancel();statusTimer=null;}
        statusInFlight=true;
        final long expectedGeneration=generation,expectedMutation=mutation;
        statusExpired=false;
        statusTimeout=scheduler.later(5000,new Runnable(){public void run(){if(attached&&generation==expectedGeneration&&statusInFlight){statusExpired=true;ready=false;session="Status terlambat";notice="Snapshot belum diterima; kontrol menunggu koneksi backend";emit();}}});
        try{scheduler.worker(new Runnable(){public void run(){
            MenuBackend.Status status=null;String failure=null;
            try{if(attached&&generation==expectedGeneration)status=backend.status();}
            catch(Exception error){failure=error.getClass().getSimpleName();}
            final MenuBackend.Status response=status;final String fault=failure;
            scheduler.dispatch(new Runnable(){public void run(){
                statusInFlight=false;if(statusTimeout!=null){statusTimeout.cancel();statusTimeout=null;}
                if(!attached)return;
                if(expectedGeneration!=generation||statusExpired){refreshAfterStatus=false;scheduleStatus(0);return;}
                if(response==null){ready=false;session="Status tidak tersedia";notice="Diagnostik gagal · "+(fault==null?"sesi berubah":fault);}
                else applyStatus(response,expectedMutation);
                emit();long delay=refreshAfterStatus?0:statusInterval();refreshAfterStatus=false;scheduleStatus(delay);
            }});
        }});}catch(RuntimeException busy){statusInFlight=false;if(statusTimeout!=null){statusTimeout.cancel();statusTimeout=null;}ready=false;session="Backend sibuk";emit();long delay=refreshAfterStatus?0:statusInterval();refreshAfterStatus=false;scheduleStatus(delay);}
    }
    private void scheduleStatus(long delay){
        if(!attached)return;
        if(statusTimer!=null)statusTimer.cancel();
        statusTimer=scheduler.later(delay,new Runnable(){public void run(){statusTimer=null;requestStatus();}});
    }
    private void applyStatus(MenuBackend.Status status,long expectedMutation){
        boolean changed=epoch>0&&epoch!=status.epoch;
        if(changed){++generation;cancelRequests("stale");profile=null;profileProgress="";notice="Scene berubah · kontrol mengikuti snapshot terbaru";}
        epoch=status.epoch;ready=status.ready&&epoch>0;session=status.state;diagnostics=status.diagnostics;
        if(changed||expectedMutation==mutation){
            for(Model m:models.values()){
                MenuBackend.Value value=status.features.get(m.definition.id);
                if(value!=null){
                    m.state.synchronize(value.on);
                    if(value.on&&m.definition.hasValue()&&Float.isFinite(value.value)){m.confirmedValue=m.definition.constrain(value.value);if(!m.state.pending&&!m.editing)m.value=m.confirmedValue;}
                }
            }
        }
        if(!ready&&!requests.isEmpty()){++generation;cancelRequests("stale");profile=null;profileProgress="";}
    }
    public boolean canUseProfile(){return attached&&ready&&requests.isEmpty()&&profile==null;}
    public MenuProfile captureProfile(){
        if(!canUseProfile()){notice="Tunggu sesi siap dan semua perintah selesai";emit();return null;}
        LinkedHashMap<String,MenuProfile.Entry> copy=new LinkedHashMap<String,MenuProfile.Entry>();
        for(Model m:models.values())copy.put(m.definition.id,new MenuProfile.Entry(m.state.applied,m.value));
        return new MenuProfile(copy);
    }
    public boolean loadProfile(MenuProfile saved){
        if(saved==null||saved.entries.isEmpty()){notice="Slot profil masih kosong";emit();return false;}
        if(!canUseProfile()){notice="Tunggu sesi siap dan semua perintah selesai";emit();return false;}
        profile=new ArrayList<Map.Entry<String,MenuProfile.Entry>>(saved.entries.entrySet());profileIndex=0;profileTotal=profile.size();
        nextProfile();emit();return true;
    }
    private void nextProfile(){
        if(profile==null)return;
        if(profileIndex>=profileTotal){profile=null;profileProgress="Profil selesai · "+profileTotal+" kontrol";notice=profileProgress;return;}
        Map.Entry<String,MenuProfile.Entry> entry=profile.get(profileIndex);
        Model m=models.get(entry.getKey());MenuProfile.Entry value=entry.getValue();m.value=m.definition.constrain(value.value);saveValue(m);
        profileProgress="Menerapkan profil · "+(profileIndex+1)+"/"+profileTotal;
        if(!submit(m.definition.command(value.on,m.value),m,value.on,true)){profile=null;profileProgress="Profil berhenti · "+profileIndex+"/"+profileTotal+" diterapkan";}
    }
    public void notifyUser(String message){notice=message;emit();}
}
