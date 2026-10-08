package wsm;

import android.app.*;
import android.content.*;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.os.*;
import android.view.*;
import android.widget.*;
import android.text.*;
import org.json.JSONObject;
import java.lang.ref.WeakReference;
import java.util.*;
import java.util.concurrent.*;

/** Activity-owned UI. Only native completion results change a control's applied state. */
public final class WsmMenu {
    static final int BG=0xF2121110,PANEL=0xFF22211E,LINE=0xFF3C3830,TEXT=0xFFECE6DA,
        DIM=0xFFAAA294,AMBER=0xFFFFB000,GREEN=0xFF7BD88F;
    static final Handler UI=new Handler(Looper.getMainLooper());
    static final ThreadPoolExecutor IO=new ThreadPoolExecutor(2,2,0,TimeUnit.SECONDS,new ArrayBlockingQueue<Runnable>(32));
    static final ArrayList<String> LOG=new ArrayList<String>();
    static final class F {
        final String id,name,desc,category; final float min,max,step,initial;
        float value; TextView control,number; SeekBar slider; final ControlState state=new ControlState();
        F(String i,String n,String d,String c,float lo,float hi,float st,float v){id=i;name=n;desc=d;category=c;min=lo;max=hi;step=st;value=initial=v;}
    }
    static F f(String i,String n,String d,String c){return new F(i,n,d,c,0,0,0,1);}
    static F s(String i,String n,String d,String c,float lo,float hi,float st,float v){return new F(i,n,d,c,lo,hi,st,v);}
    static final F[] FEATURES={
        f("god","God Mode / Opsi","Immortal + Invincible pemain","Karakter"),
        f("hp","HP Protection","Immortal; tidak otomatis mengisi HP","Karakter"),
        f("stam","Stamina Refill","Isi ulang selama scene aktif","Karakter"),
        f("mana","Mana Refill","Isi ulang meter skill","Karakter"),
        f("poise","Super Armor","Anti knockback, stun, knockdown","Karakter"),
        f("immune","Status Immunity","Opsi status karakter","Karakter"),
        f("godmode","Damage Guard [eksperimental]","Hook hero-only; perlu payload","Karakter"),
        f("ohk","Auto-Kill Pulse","Damage pipeline di dalam radius","Pertempuran"),
        f("onehp","Enemy 1 HP","Ledger per-scene; OHK punya prioritas","Pertempuran"),
        s("aura","Radius Pulse","Meter dari hero","Pertempuran",5,40,1,20),
        s("dmg","Pulse Power [eksperimental]","ON memulai pulse berkala: nilai ×100.000; ONEHP menonaktifkan modifier","Pertempuran",1,99,1,10),
        f("crit","Critical Pulse [eksperimental]","ON memulai pulse 1 juta (atau Pulse Power); hasil hit belum terukur","Pertempuran"),
        s("critdmg","Critical Damage Scale [eksperimental]","Getter hero ×1–5; hasil hit belum terukur","Pertempuran",1,5,.25f,2),
        f("nocd","Cooldown Gates [eksperimental]","Tiga patch; semua wajib berhasil","Pertempuran"),
        f("stunall","Freeze AI [eksperimental]","Gate AI; StunCommand lama dihentikan","Pertempuran"),
        s("speed","Movement Scale [eksperimental]","Walk / dash / soft-dash hero","Dunia",1,5,.25f,2),
        f("loot","Auto-Loot [eksperimental]","OFF stop permintaan baru; pickup yang sudah diminta tidak dibatalkan","Dunia"),
        s("timescale","Time Scale","OFF menghapus modifier wsm saja","Dunia",.1f,5,.05f,1)
    };
    static Application app; static SharedPreferences prefs;
    static WeakReference<Activity> owner=new WeakReference<Activity>(null);
    static LinearLayout root,content,badge; static TextView status,diagnostics,logs;
    static String category="Semua",query=""; static boolean registered,collapsed;
    static volatile int uiGeneration; static long lastEpoch=-1; static boolean uncertain;
    public static native String exec(String command);
    public static void show(Context c){if(c==null)return;initialize(c);if(c instanceof Activity)showInActivity((Activity)c);}
    public static void showInActivity(final Activity a){if(a==null)return;initialize(a);UI.post(new Runnable(){public void run(){attach(a);}});}
    static synchronized void initialize(Context c){
        if(registered)return;app=(Application)c.getApplicationContext();prefs=app.getSharedPreferences("wsm_v6",0);
        try{android.content.pm.PackageInfo p=app.getPackageManager().getPackageInfo(app.getPackageName(),0);long code=Build.VERSION.SDK_INT>=28?p.getLongVersionCode():p.versionCode;exec("__identity "+app.getPackageName()+" "+p.versionName+" "+code);}catch(Exception e){log("Identity gagal: "+e);}
        for(F f:FEATURES){float v=prefs.getFloat("value."+f.id,f.value);if(f.max>0&&!Float.isNaN(v))f.value=Math.max(f.min,Math.min(f.max,v));}
        app.registerActivityLifecycleCallbacks(new Application.ActivityLifecycleCallbacks(){
            public void onActivityResumed(Activity a){attach(a);}
            public void onActivityPaused(Activity a){if(owner.get()==a){exec("__activity paused");detach();}}
            public void onActivityDestroyed(Activity a){if(owner.get()==a){exec("__activity paused");detach();}}
            public void onActivityCreated(Activity a,Bundle b){} public void onActivityStarted(Activity a){}
            public void onActivityStopped(Activity a){} public void onActivitySaveInstanceState(Activity a,Bundle b){}
        });registered=true;
    }
    static int dp(float n){return Math.round(n*app.getResources().getDisplayMetrics().density);}
    static GradientDrawable bg(int color,int border){GradientDrawable d=new GradientDrawable();d.setColor(color);d.setCornerRadius(dp(10));d.setStroke(dp(1),border);return d;}
    static LinearLayout col(Context c){LinearLayout l=new LinearLayout(c);l.setOrientation(1);return l;}
    static LinearLayout row(Context c){LinearLayout l=new LinearLayout(c);l.setGravity(Gravity.CENTER_VERTICAL);return l;}
    static TextView text(Context c,String s,int size,int color){TextView t=new TextView(c);t.setText(s);t.setTextSize(size);t.setTextColor(color);return t;}
    static TextView button(Context c,String s,final Runnable action){TextView t=text(c,s,12,AMBER);t.setGravity(Gravity.CENTER);t.setPadding(dp(10),dp(8),dp(10),dp(8));t.setBackground(bg(PANEL,LINE));t.setOnClickListener(new View.OnClickListener(){public void onClick(View v){action.run();}});return t;}
    static void detach(){uiGeneration++;UI.removeCallbacks(POLL);for(F f:FEATURES)if(f.state.pending)f.state.finish("stale");for(View v:new View[]{root,badge})if(v!=null&&v.getParent() instanceof ViewGroup)((ViewGroup)v.getParent()).removeView(v);
        root=content=badge=null;status=diagnostics=logs=null;for(F f:FEATURES){f.control=f.number=null;f.slider=null;}owner.clear();}
    static void attach(final Activity a){
        if(a.isFinishing()||a.isDestroyed()||owner.get()==a&&root!=null)return;
        ViewGroup parent=(ViewGroup)a.findViewById(android.R.id.content);if(parent==null)return;
        detach();owner=new WeakReference<Activity>(a);exec("__activity resumed");
        int sw=a.getResources().getDisplayMetrics().widthPixels,sh=a.getResources().getDisplayMetrics().heightPixels;
        root=col(a);root.setPadding(dp(10),dp(8),dp(10),dp(8));root.setBackground(bg(BG,LINE));
        LinearLayout head=row(a);TextView title=text(a,"Ω WSM 6.2 RC1",18,TEXT);title.setTypeface(Typeface.MONOSPACE);
        head.addView(title,new LinearLayout.LayoutParams(0,-2,1));head.addView(button(a,"PANIC",new Runnable(){public void run(){execute("panic",null,false);}}));
        head.addView(button(a,"−",new Runnable(){public void run(){collapse();}}));root.addView(head);drag(head,root);
        status=text(a,"Menunggu sesi…",12,AMBER);root.addView(status);
        EditText search=new EditText(a);search.setSingleLine();search.setTextColor(TEXT);search.setHintTextColor(DIM);search.setTextSize(13);search.setHint("Cari fitur / search");search.setText(query);
        search.addTextChangedListener(new TextWatcher(){public void beforeTextChanged(CharSequence s,int st,int count,int after){}public void afterTextChanged(Editable e){}
            public void onTextChanged(CharSequence s,int st,int before,int count){query=s.toString().toLowerCase(Locale.ROOT);render();}});root.addView(search);
        LinearLayout tabs=row(a);for(final String c:new String[]{"Semua","Karakter","Pertempuran","Dunia","Sistem","47 Fitur"})tabs.addView(button(a,c,new Runnable(){public void run(){category=c;render();}}),new LinearLayout.LayoutParams(0,-2,1));root.addView(tabs);
        ScrollView scroll=new ScrollView(a);content=col(a);scroll.addView(content);root.addView(scroll,new LinearLayout.LayoutParams(-1,0,1));
        LinearLayout profiles=row(a);profiles.addView(button(a,"SIMPAN PROFIL",new Runnable(){public void run(){profile(true);}}),new LinearLayout.LayoutParams(0,-2,1));
        profiles.addView(button(a,"MUAT PROFIL",new Runnable(){public void run(){profile(false);}}),new LinearLayout.LayoutParams(0,-2,1));
        profiles.addView(button(a,"RESET NILAI",new Runnable(){public void run(){if(profileBusy())return;for(F f:FEATURES)if(!f.state.applied)f.value=f.initial;render();}}),new LinearLayout.LayoutParams(0,-2,1));root.addView(profiles);
        logs=text(a,"",11,DIM);logs.setMaxLines(3);root.addView(logs);updateLogs();
        parent.addView(root,new ViewGroup.LayoutParams(Math.min(dp(780),sw-dp(24)),Math.min(dp(580),sh-dp(32))));root.setX(dp(12));root.setY(dp(12));
        badge=col(a);badge.addView(button(a,"Ω WSM 6",new Runnable(){public void run(){collapse();}}));badge.setBackground(bg(BG,AMBER));parent.addView(badge,new ViewGroup.LayoutParams(-2,-2));badge.setX(dp(12));badge.setY(dp(12));
        root.setVisibility(collapsed?View.GONE:View.VISIBLE);badge.setVisibility(collapsed?View.VISIBLE:View.GONE);
        root.setFocusableInTouchMode(true);root.setOnKeyListener(new View.OnKeyListener(){public boolean onKey(View v,int code,KeyEvent e){if(e.getAction()!=KeyEvent.ACTION_UP)return false;
            if(code==KeyEvent.KEYCODE_MOVE_END){execute("panic",null,false);return true;}if(code==KeyEvent.KEYCODE_INSERT){collapse();return true;}
            if(code>=KeyEvent.KEYCODE_F1&&code<=KeyEvent.KEYCODE_F6){toggle(FEATURES[code-KeyEvent.KEYCODE_F1]);return true;}return false;}});
        render();UI.post(POLL);
    }
    static void drag(View handle,final View target){handle.setOnTouchListener(new View.OnTouchListener(){float x,y,sx,sy;public boolean onTouch(View v,MotionEvent e){
        if(e.getAction()==0){x=e.getRawX();y=e.getRawY();sx=target.getX();sy=target.getY();return true;}
        if(e.getAction()==2){View p=(View)target.getParent();if(p!=null){target.setX(Math.max(0,Math.min(sx+e.getRawX()-x,p.getWidth()-target.getWidth())));target.setY(Math.max(0,Math.min(sy+e.getRawY()-y,p.getHeight()-target.getHeight())));}return true;}return false;}});}
    static void collapse(){collapsed=!collapsed;if(root!=null)root.setVisibility(collapsed?View.GONE:View.VISIBLE);if(badge!=null)badge.setVisibility(collapsed?View.VISIBLE:View.GONE);}
    static String command(F f,boolean on){float v=on?(f.max>0?f.value:1):0;
        String prefix=f.id.equals("speed")||f.id.equals("critdmg")?f.id+" ":f.id.equals("godmode")||f.id.equals("nocd")||f.id.equals("loot")||f.id.equals("stunall")?f.id+" ":"feat "+f.id+" ";return prefix+Float.toString(v);}
    static void toggle(F f){if(!f.state.pending)execute(command(f,!f.state.applied),f,!f.state.applied);}
    static void refresh(F f){if(f.control!=null){f.control.setText(uncertain?"?":f.state.pending?"…":f.state.applied?"ON":"OFF");f.control.setTextColor(f.state.pending?AMBER:f.state.applied?GREEN:DIM);f.control.setEnabled(!f.state.pending&&!uncertain);}if(f.slider!=null)f.slider.setEnabled(!f.state.pending&&!uncertain);}
    static void render(){Activity a=owner.get();if(a==null||content==null)return;content.removeAllViews();diagnostics=null;
        if(category.equals("47 Fitur")){renderCatalog(a);return;}
        for(final F f:FEATURES){f.control=f.number=null;f.slider=null;if(category.equals("Sistem")||!category.equals("Semua")&&!category.equals(f.category))continue;
            if(!query.isEmpty()&&!(f.name+" "+f.id+" "+f.desc).toLowerCase(Locale.ROOT).contains(query))continue;
            LinearLayout card=col(a);card.setPadding(dp(10),dp(7),dp(10),dp(7));card.setBackground(bg(PANEL,LINE));LinearLayout line=row(a);
            line.addView(text(a,f.name,14,TEXT),new LinearLayout.LayoutParams(0,-2,1));f.control=button(a,"OFF",new Runnable(){public void run(){toggle(f);}});line.addView(f.control);card.addView(line);card.addView(text(a,f.desc,11,DIM));refresh(f);
            if(f.max>0){LinearLayout values=row(a);f.number=text(a,String.format(Locale.ROOT,"%.2f",f.value),12,AMBER);values.addView(f.number);
                SeekBar bar=new SeekBar(a);f.slider=bar;bar.setMax(Math.round((f.max-f.min)/f.step));bar.setProgress(Math.round((f.value-f.min)/f.step));bar.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener(){
                    public void onProgressChanged(SeekBar b,int p,boolean user){if(user){f.value=f.min+p*f.step;if(f.number!=null)f.number.setText(String.format(Locale.ROOT,"%.2f",f.value));}}
                    public void onStartTrackingTouch(SeekBar b){}public void onStopTrackingTouch(SeekBar b){prefs.edit().putFloat("value."+f.id,f.value).apply();if(f.state.applied&&!f.state.pending)execute(command(f,true),f,true);}});
                values.addView(bar,new LinearLayout.LayoutParams(0,-2,1));card.addView(values);refresh(f);}
            LinearLayout.LayoutParams lp=new LinearLayout.LayoutParams(-1,-2);lp.bottomMargin=dp(6);content.addView(card,lp);
        }
        if(category.equals("Dunia")||category.equals("Semua")){LinearLayout warp=row(a);String[] labels={"← 15m","→ 15m","↑ 15m","↓ 15m"};final String[] cmds={"tpr -15 0","tpr 15 0","tpr 0 15","tpr 0 -15"};
            for(int i=0;i<4;i++){final String cmd=cmds[i];warp.addView(button(a,labels[i],new Runnable(){public void run(){execute(cmd,null,false);}}),new LinearLayout.LayoutParams(0,-2,1));}content.addView(warp);}
        if(category.equals("Pertempuran")||category.equals("Semua"))content.addView(button(a,"SAPU STAGE — sekali",new Runnable(){public void run(){execute("sweep",null,false);}}));
        if(category.equals("Sistem")||category.equals("Semua")){content.addView(button(a,"SELF-TEST PAYLOAD",new Runnable(){public void run(){execute("selftest",null,false);}}));
            diagnostics=text(a,"",12,DIM);diagnostics.setTypeface(Typeface.MONOSPACE);content.addView(diagnostics);
            content.addView(text(a,"Gold/gem, inventori, progres server, ESP dan freecam belum tersedia.\nON = konfigurasi diterapkan; efek game belum otomatis terobservasi.\nProfil tidak aktif otomatis setelah restart atau ganti scene.",12,DIM));}
    }
    static void renderCatalog(final Activity a){
        for(F f:FEATURES){f.control=f.number=null;f.slider=null;}
        content.addView(text(a,"47 fitur desain · lihat cakupan sebelum mengaktifkan kontrol",13,AMBER));
        for(final String[] item:FeatureCatalog.ROWS){
            if(!query.isEmpty()&&!Arrays.toString(item).toLowerCase(Locale.ROOT).contains(query))continue;
            LinearLayout card=col(a);card.setPadding(dp(10),dp(7),dp(10),dp(7));card.setBackground(bg(PANEL,LINE));
            card.addView(text(a,item[1],14,TEXT));
            String label=item[3].equals("partial")?"Cakupan sebagian":item[3].equals("prototype")?"Eksperimental":item[3].equals("target_absent")?"Mekanik belum ditemukan":item[3].equals("authority_unverified")?"Transaksi belum terverifikasi":"Belum diimplementasikan";
            card.addView(text(a,label+" · "+item[2],11,AMBER));card.addView(text(a,item[4],12,DIM));
            if(!item[5].isEmpty())card.addView(button(a,"BUKA KONTROL",new Runnable(){public void run(){for(F f:FEATURES)if(f.id.equals(item[5])){category=f.category;query=f.id;render();break;}}}));
            LinearLayout.LayoutParams lp=new LinearLayout.LayoutParams(-1,-2);lp.bottomMargin=dp(6);content.addView(card,lp);
        }
    }
    static void execute(final String cmd,final F f,final boolean desired){
        if(f!=null&&f.state.pending){log("Kontrol masih menunggu hasil: "+f.id);return;}
        if(cmd.equals("panic")){++uiGeneration;for(F item:FEATURES)if(item.state.pending){item.state.finish("stale");refresh(item);}}
        final int generation=uiGeneration;final long epoch=lastEpoch;
        final String urgent=cmd.equals("panic")?exec(cmd):null;
        final long token=f!=null?f.state.begin(desired):0;if(f!=null)refresh(f);try{IO.execute(new Runnable(){public void run(){String phase="fault",detail="";
            try{JSONObject r=new JSONObject(urgent!=null?urgent:generation!=uiGeneration?"{\"state\":\"stale\"}":exec("epoch "+epoch+" "+cmd));phase=r.optString("state","fault");long id=r.optLong("id",0),deadline=SystemClock.elapsedRealtime()+35000;
                while(phase.equals("accepted")&&SystemClock.elapsedRealtime()<deadline){Thread.sleep(100);r=new JSONObject(exec("result "+id));phase=r.optString("state","fault");}
                if(phase.equals("accepted"))phase="timeout";detail=r.optString("detail",phase);
            }catch(Throwable e){detail=e.getClass().getSimpleName()+": "+e.getMessage();}
            final String result=phase,message=detail;UI.post(new Runnable(){public void run(){if(f!=null&&generation==uiGeneration){f.state.finish(token,result);refresh(f);}log(cmd+" → "+result+" · "+message);}});
        }});}catch(RuntimeException e){if(f!=null){f.state.finish(token,"busy");refresh(f);}log("Antrian UI penuh");}
    }
    static final Runnable POLL=new Runnable(){public void run(){if(root==null)return;try{JSONObject s=new JSONObject(exec("status"));
        long epoch=s.optLong("epoch",-1);if(lastEpoch!=-1&&epoch!=lastEpoch){++uiGeneration;for(F f:FEATURES)if(f.state.pending)f.state.finish("stale");}lastEpoch=epoch;uncertain=!s.optBoolean("ready");
        if(status!=null){status.setText("● "+s.optString("state")+" · PID "+s.optInt("pid")+" · epoch "+s.optLong("epoch"));status.setTextColor(s.optBoolean("ready")?GREEN:AMBER);}
        JSONObject fs=s.optJSONObject("features");if(fs!=null)for(F f:FEATURES){JSONObject v=fs.optJSONObject(f.id);if(v!=null){f.state.synchronize(v.optBoolean("on"));refresh(f);}}
        if(diagnostics!=null)diagnostics.setText("Build "+s.optString("build")+"\nPayload "+s.optBoolean("payload")+" · players "+s.optInt("players")+"\nGuard faults "+s.optInt("faults")+"\nLoot "+s.optInt("lootCandidates")+" candidates / "+s.optInt("lootRequested")+" requests");
        }catch(Throwable e){if(status!=null)status.setText("Diagnostik gagal: "+e.getClass().getSimpleName());}UI.postDelayed(this,1000);}};
    static boolean profileBusy(){if(uncertain){log("Sesi belum siap");return true;}for(F f:FEATURES)if(f.state.pending){log("Tunggu seluruh hasil sebelum mengubah profil");return true;}return false;}
    static void profile(final boolean save){final Activity a=owner.get();if(a==null||profileBusy())return;new AlertDialog.Builder(a).setTitle(save?"Simpan profil":"Muat profil (manual)").setItems(new String[]{"Solo","Battle","Explore"},new DialogInterface.OnClickListener(){
        public void onClick(DialogInterface dialog,int slot){if(profileBusy())return;String key="profile."+slot;try{
            if(save){JSONObject all=new JSONObject();for(F f:FEATURES){JSONObject v=new JSONObject();v.put("on",f.state.applied);v.put("value",f.value);all.put(f.id,v);}prefs.edit().putString(key,all.toString()).apply();log("Profil disimpan");}
            else{JSONObject all=new JSONObject(prefs.getString(key,"{}"));for(F f:FEATURES){JSONObject v=all.optJSONObject(f.id);if(v!=null){float value=(float)v.optDouble("value",f.value);if(f.max>0&&!Float.isNaN(value))f.value=Math.max(f.min,Math.min(f.max,value));execute(command(f,v.optBoolean("on")),f,v.optBoolean("on"));}}render();}
        }catch(Exception e){log("Profil gagal: "+e.getClass().getSimpleName());}}}).setNegativeButton("Batal",null).show();}
    static void log(final String s){if(Looper.myLooper()!=Looper.getMainLooper()){UI.post(new Runnable(){public void run(){log(s);}});return;}LOG.add(s);while(LOG.size()>64)LOG.remove(0);updateLogs();}
    static void updateLogs(){if(logs==null)return;StringBuilder b=new StringBuilder();for(int i=Math.max(0,LOG.size()-3);i<LOG.size();i++)b.append(LOG.get(i)).append('\n');logs.setText(b.toString().trim());}
}
