package com.wsm.preview;
import android.app.Activity;
import android.os.Bundle;
import android.os.SystemClock;
import android.content.pm.ActivityInfo;
import android.view.ViewGroup;
import android.widget.TextView;
import java.util.LinkedHashMap;
import java.util.Map;
import wsm.*;
/** Isolated visual fixture. It never invokes JNI, loads a module or contacts a game. */
public final class PreviewActivity extends Activity {
    AndroidMenuScheduler scheduler;MenuController controller;MenuView view;
    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        boolean portrait="portrait".equals(getIntent().getStringExtra("orientation"));
        setRequestedOrientation(portrait?ActivityInfo.SCREEN_ORIENTATION_PORTRAIT:ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE);
        TextView label=new TextView(this);label.setText("WSM UI PREVIEW · SYNTHETIC BACKEND\nVisual and interaction fixture only");label.setTextColor(0xFF73829A);label.setGravity(17);label.setBackgroundColor(0xFF080C13);setContentView(label);
        scheduler=new AndroidMenuScheduler();MenuPreferences preferences=new MenuPreferences(getSharedPreferences("wsm_preview",0));
        controller=new MenuController(new FakeBackend(),scheduler,preferences.values(),preferences);
        view=new MenuView(this,controller,preferences);view.attach((ViewGroup)findViewById(android.R.id.content));controller.start(view);
    }
    @Override public void onDestroy(){if(controller!=null)controller.stop();if(view!=null)view.release();if(scheduler!=null)scheduler.close();super.onDestroy();}
    static final class FakeBackend implements MenuBackend {
        final Map<String,Value> values=new LinkedHashMap<String,Value>();
        final Map<Long,Pending> requests=new LinkedHashMap<Long,Pending>();long epoch=1,next=0;
        static final class Pending {long epoch,due;String command;Reply result;Pending(long e,long d,String c){epoch=e;due=d;command=c;}}
        FakeBackend(){for(FeatureDefinition f:FeatureDefinition.ALL)values.put(f.id,new Value(false,f.initial));}
        public synchronized Status status(){return new Status(epoch,true,"ready","SIMULASI UI · Tidak terhubung ke game\nRespons command fixture: 600 ms",values);}
        public synchronized Reply panic(){epoch++;for(FeatureDefinition f:FeatureDefinition.ALL)values.put(f.id,new Value(false,f.initial));return new Reply(++next,epoch,"applied","Fixture reset");}
        public synchronized Reply execute(String command){
            if(command.startsWith("result ")){
                long id=Long.parseLong(command.substring(7));Pending p=requests.get(id);if(p==null)return new Reply(id,epoch,"expired","Fixture result expired");
                if(p.result!=null)return p.result;
                if(p.epoch!=epoch)return p.result=new Reply(id,epoch,"stale","Fixture epoch changed");
                if(SystemClock.elapsedRealtime()<p.due)return new Reply(id,epoch,"accepted","Fixture pending");
                String[] parts=p.command.split(" ");int at=parts[0].equals("feat")?1:0;
                if(parts.length>at+1&&values.containsKey(parts[at])){float value=Float.parseFloat(parts[at+1]);Value old=values.get(parts[at]);values.put(parts[at],new Value(value!=0,value==0?old.value:value));}
                return p.result=new Reply(id,epoch,"applied","Simulated completion; no game effect");
            }
            long expected=epoch;
            if(command.startsWith("epoch ")){String[] parts=command.split(" ",3);expected=Long.parseLong(parts[1]);command=parts[2];}
            if(expected!=epoch)return new Reply(0,epoch,"stale","Fixture epoch mismatch");
            long id=++next;requests.put(id,new Pending(epoch,SystemClock.elapsedRealtime()+600,command));return new Reply(id,0,"accepted","queued");
        }
    }
}
