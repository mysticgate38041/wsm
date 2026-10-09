package wsm;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.DialogInterface;
import android.content.res.ColorStateList;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.text.Editable;
import android.text.TextWatcher;
import android.text.TextUtils;
import android.view.Gravity;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewGroup;
import android.widget.EditText;
import android.widget.HorizontalScrollView;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.SeekBar;
import android.widget.TextView;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;

/** Android rendering only. Cards are allocated once and updated by state/visibility differences. */
public final class MenuView implements MenuController.Listener {
    private static final int BG=0xFA10151F,PANEL=0xFF1A2230,RAISED=0xFF222D3E,LINE=0xFF354156,
        TEXT=0xFFF1F4F9,DIM=0xFFACB8CC,ACCENT=0xFFFFCC7A,TEAL=0xFF83E4CD,RED=0xFFFF9CA9;
    private static final String[] SECTIONS={"Semua","Karakter","Pertempuran","Dunia","Profil","Sistem","Katalog"};
    private static final String[] LABELS={"Semua · 18","Karakter · 7","Pertempuran · 8","Dunia · 3","Profil","Sistem","Katalog · 47"};
    private Activity activity;
    private final MenuController controller;
    private final MenuPreferences preferences;
    private ViewGroup parent;
    private LinearLayout root,body,main,rail,controls,catalog,system,profiles,badge;
    private ScrollView railScroll,scroll;
    private HorizontalScrollView tabsScroll;
    private TextView session,summary,sectionTitle,notice,diagnostics,empty,badgeLabel,profileStatus;
    private EditText search;
    private TextWatcher searchWatcher;
    private AlertDialog dialog;
    private boolean released,collapsed,wide,columns,catalogBuilt;
    private String category="Karakter",query="";
    private MenuSnapshot latest;
    private final Map<String,Card> cards=new LinkedHashMap<String,Card>();
    private final List<LinearLayout> pairs=new ArrayList<LinearLayout>();
    private final List<TextView> navigation=new ArrayList<TextView>();
    private final List<CatalogCard> catalogCards=new ArrayList<CatalogCard>();
    private final List<TextView> actions=new ArrayList<TextView>();
    private final List<TextView> profileButtons=new ArrayList<TextView>();
    private LinearLayout worldActions,battleActions;
    private View.OnLayoutChangeListener layoutListener;
    private int lastWidth,lastHeight;

    public MenuView(Activity activity,MenuController controller,MenuPreferences preferences){
        this.activity=activity;this.controller=controller;this.preferences=preferences;
        build();
    }
    private int dp(float value){return Math.round(value*activity.getResources().getDisplayMetrics().density);}
    private GradientDrawable background(int fill,int stroke,float radius){GradientDrawable shape=new GradientDrawable();shape.setColor(fill);shape.setCornerRadius(dp(radius));shape.setStroke(dp(1),stroke);return shape;}
    private LinearLayout column(){LinearLayout layout=new LinearLayout(activity);layout.setOrientation(LinearLayout.VERTICAL);return layout;}
    private LinearLayout row(){LinearLayout layout=new LinearLayout(activity);layout.setGravity(Gravity.CENTER_VERTICAL);return layout;}
    private TextView text(String value,int size,int color){TextView view=new TextView(activity);view.setText(value);view.setTextSize(size);view.setTextColor(color);view.setIncludeFontPadding(false);return view;}
    private void setText(TextView view,String value){if(!value.contentEquals(view.getText()))view.setText(value);}
    private TextView button(String label,int color,final Runnable action){
        TextView view=text(label,12,color);view.setTypeface(Typeface.DEFAULT,Typeface.BOLD);view.setGravity(Gravity.CENTER);
        view.setMinHeight(dp(48));view.setPadding(dp(12),dp(8),dp(12),dp(8));view.setBackground(background(RAISED,LINE,10));
        view.setContentDescription(label);view.setFocusable(true);view.setOnClickListener(new View.OnClickListener(){public void onClick(View ignored){if(!released)action.run();}});return view;
    }
    private LinearLayout.LayoutParams spaced(int width,int height){LinearLayout.LayoutParams params=new LinearLayout.LayoutParams(width,height);params.bottomMargin=dp(8);return params;}
    private void build(){
        root=column();root.setPadding(dp(12),dp(8),dp(12),dp(8));root.setBackground(background(BG,LINE,18));root.setElevation(dp(12));
        LinearLayout head=row();LinearLayout title=column();TextView name=text("WSM 6.3 RC1",18,TEXT);name.setTypeface(Typeface.DEFAULT,Typeface.BOLD);title.addView(name);
        TextView subtitle=text("CONTROL CENTER  /  18 KONTROL",10,DIM);subtitle.setPadding(0,dp(4),0,0);title.addView(subtitle);head.addView(title,new LinearLayout.LayoutParams(0,-2,1));
        TextView panic=button("PANIC",RED,new Runnable(){public void run(){controller.panic();}});panic.setBackground(background(0xFF402331,0xFF784253,10));panic.setContentDescription("PANIC: hentikan semua kontrol sekarang");head.addView(panic);
        TextView collapse=button("−",DIM,new Runnable(){public void run(){setCollapsed(true);}});collapse.setTextSize(22);collapse.setContentDescription("Perkecil menu");LinearLayout.LayoutParams compact=new LinearLayout.LayoutParams(dp(48),dp(48));compact.leftMargin=dp(6);head.addView(collapse,compact);root.addView(head);drag(title);
        LinearLayout statusRow=row();statusRow.setPadding(0,dp(6),0,dp(8));session=text("● Menghubungkan",12,ACCENT);session.setSingleLine();session.setEllipsize(TextUtils.TruncateAt.END);statusRow.addView(session,new LinearLayout.LayoutParams(0,-2,1));summary=text("0 aktif / 18",11,DIM);statusRow.addView(summary);root.addView(statusRow);
        body=row();body.setGravity(Gravity.TOP);root.addView(body,new LinearLayout.LayoutParams(-1,0,1));
        railScroll=new ScrollView(activity);railScroll.setFillViewport(false);railScroll.setVerticalScrollBarEnabled(false);rail=column();rail.setPadding(0,0,dp(10),0);railScroll.addView(rail);body.addView(railScroll,new LinearLayout.LayoutParams(dp(138),-1));
        TextView navHeading=text("KONTROL",10,DIM);navHeading.setPadding(dp(8),dp(8),0,dp(10));rail.addView(navHeading);
        for(int i=0;i<SECTIONS.length;i++){if(i==4){TextView extra=text("RUANG KERJA",10,DIM);extra.setPadding(dp(8),dp(18),0,dp(10));rail.addView(extra);}rail.addView(navButton(i),spaced(-1,-2));}
        main=column();body.addView(main,new LinearLayout.LayoutParams(0,-1,1));
        tabsScroll=new HorizontalScrollView(activity);tabsScroll.setHorizontalScrollBarEnabled(false);LinearLayout tabs=row();for(int i=0;i<SECTIONS.length;i++){LinearLayout.LayoutParams p=new LinearLayout.LayoutParams(-2,dp(48));p.rightMargin=dp(6);tabs.addView(navButton(i),p);}tabsScroll.addView(tabs);main.addView(tabsScroll,spaced(-1,dp(48)));
        search=new EditText(activity);search.setSingleLine();search.setTextSize(13);search.setTextColor(TEXT);search.setHintTextColor(DIM);search.setHint("Cari nama atau fungsi kontrol…");search.setContentDescription("Cari kontrol atau katalog");search.setPadding(dp(12),dp(6),dp(12),dp(6));search.setBackground(background(PANEL,LINE,10));main.addView(search,spaced(-1,dp(48)));
        searchWatcher=new TextWatcher(){public void beforeTextChanged(CharSequence value,int start,int count,int after){}public void afterTextChanged(Editable value){}public void onTextChanged(CharSequence value,int start,int before,int count){query=value.toString().trim().toLowerCase(Locale.ROOT);filter();}};search.addTextChangedListener(searchWatcher);
        sectionTitle=text("",12,DIM);sectionTitle.setPadding(dp(2),0,0,dp(8));main.addView(sectionTitle);
        scroll=new ScrollView(activity);scroll.setFillViewport(false);LinearLayout pages=column();scroll.addView(pages);main.addView(scroll,new LinearLayout.LayoutParams(-1,0,1));
        controls=column();pages.addView(controls);LinearLayout pair=null;
        for(FeatureDefinition feature:FeatureDefinition.ALL){if(cards.size()%2==0){pair=row();pair.setGravity(Gravity.TOP);pairs.add(pair);controls.addView(pair);}Card card=new Card(feature);cards.put(feature.id,card);pair.addView(card.view,spaced(-1,-2));}
        worldActions=column();TextView movement=text("PERPINDAHAN · AKSI SEKALI",10,DIM);movement.setPadding(0,dp(8),0,dp(8));worldActions.addView(movement);
        LinearLayout warp=row();String[] directions={"← 15 m","→ 15 m","↑ 15 m","↓ 15 m"};final String[] commands={"tpr -15 0","tpr 15 0","tpr 0 15","tpr 0 -15"};for(int i=0;i<directions.length;i++){final String command=commands[i];TextView action=actionButton(directions[i],command);LinearLayout.LayoutParams p=new LinearLayout.LayoutParams(0,-2,1);if(i>0)p.leftMargin=dp(4);warp.addView(action,p);}worldActions.addView(warp);controls.addView(worldActions,spaced(-1,-2));
        battleActions=column();battleActions.addView(actionButton("Sapu stage · sekali","sweep"));controls.addView(battleActions,spaced(-1,-2));
        empty=text("Tidak ada kontrol yang cocok. Coba kata lain atau kategori Semua.",14,DIM);empty.setPadding(dp(12),dp(20),dp(12),dp(20));pages.addView(empty);
        buildProfiles();pages.addView(profiles);
        buildSystem();pages.addView(system);
        catalog=column();pages.addView(catalog);
        notice=text("18 kontrol · aktivasi manual",11,DIM);notice.setMaxLines(2);notice.setPadding(dp(2),dp(8),dp(2),0);notice.setMinHeight(dp(34));notice.setAccessibilityLiveRegion(View.ACCESSIBILITY_LIVE_REGION_POLITE);root.addView(notice);
        badge=row();badge.setPadding(dp(6),dp(6),dp(6),dp(6));badge.setBackground(background(BG,LINE,14));badge.setElevation(dp(12));badgeLabel=button("WSM · BUKA",ACCENT,new Runnable(){public void run(){setCollapsed(false);}});badge.addView(badgeLabel);TextView badgePanic=button("PANIC",RED,new Runnable(){public void run(){controller.panic();}});LinearLayout.LayoutParams b=new LinearLayout.LayoutParams(-2,dp(48));b.leftMargin=dp(6);badge.addView(badgePanic,b);
        root.setFocusableInTouchMode(true);root.setOnKeyListener(new View.OnKeyListener(){public boolean onKey(View ignored,int key,KeyEvent event){if(event.getAction()!=KeyEvent.ACTION_UP)return false;if(key==KeyEvent.KEYCODE_MOVE_END){controller.panic();return true;}if(key==KeyEvent.KEYCODE_INSERT){setCollapsed(!collapsed);return true;}if(key>=KeyEvent.KEYCODE_F1&&key<=KeyEvent.KEYCODE_F6){controller.toggle(FeatureDefinition.ALL.get(key-KeyEvent.KEYCODE_F1).id);return true;}return false;}});
        filter();
    }
    private TextView navButton(final int index){TextView view=button(LABELS[index],DIM,new Runnable(){public void run(){select(SECTIONS[index]);}});view.setTag(SECTIONS[index]);navigation.add(view);return view;}
    private TextView actionButton(String title,final String command){TextView view=button(title,ACCENT,new Runnable(){public void run(){controller.action(command);}});actions.add(view);return view;}
    private void select(String section){category=section;if(search!=null)search.clearFocus();filter();scroll.scrollTo(0,0);}
    private void buildProfiles(){
        profiles=column();profiles.addView(info("Profil manual","Simpan konfigurasi yang sudah dikonfirmasi. Muat profil setelah memilih slot; tidak aktif otomatis setelah restart atau pergantian scene."),spaced(-1,-2));
        String[] names={"Solo","Battle","Explore"};for(int i=0;i<names.length;i++){final int slot=i;final String name=names[i];LinearLayout card=column();card.setPadding(dp(14),dp(12),dp(14),dp(12));card.setBackground(background(PANEL,LINE,12));TextView title=text(String.format(Locale.ROOT,"0%d  %s",i+1,name),16,TEXT);title.setTypeface(Typeface.DEFAULT,Typeface.BOLD);card.addView(title);LinearLayout buttons=row();buttons.setPadding(0,dp(12),0,0);TextView save=button("Simpan",DIM,new Runnable(){public void run(){saveProfile(slot,name);}});TextView load=button("Muat…",ACCENT,new Runnable(){public void run(){loadProfile(slot,name);}});profileButtons.add(save);profileButtons.add(load);buttons.addView(save,new LinearLayout.LayoutParams(0,-2,1));LinearLayout.LayoutParams p=new LinearLayout.LayoutParams(0,-2,1);p.leftMargin=dp(8);buttons.addView(load,p);card.addView(buttons);profiles.addView(card,spaced(-1,-2));}
        TextView reset=button("Reset nilai kontrol nonaktif",DIM,new Runnable(){public void run(){controller.resetInactiveValues();}});profileButtons.add(reset);profiles.addView(reset,spaced(-1,-2));profileStatus=text("Pilih slot untuk menyimpan atau memuat.",12,DIM);profiles.addView(profileStatus);
    }
    private LinearLayout info(String heading,String detail){LinearLayout card=column();card.setPadding(dp(14),dp(12),dp(14),dp(12));card.setBackground(background(PANEL,LINE,12));TextView title=text(heading,15,TEXT);title.setTypeface(Typeface.DEFAULT,Typeface.BOLD);card.addView(title);TextView description=text(detail,12,DIM);description.setLineSpacing(dp(3),1);description.setPadding(0,dp(8),0,0);card.addView(description);return card;}
    private void buildSystem(){
        system=column();system.addView(info("Membaca status dengan benar","ON berarti konfigurasi diterapkan oleh backend. Efek di dalam game belum otomatis terobservasi. MENUNGGU berarti perintah sedang dikirim atau masih menunggu hasil."),spaced(-1,-2));
        diagnostics=text("Menunggu snapshot…",12,DIM);diagnostics.setTypeface(Typeface.MONOSPACE);diagnostics.setLineSpacing(dp(4),1);diagnostics.setPadding(dp(14),dp(14),dp(14),dp(14));diagnostics.setBackground(background(PANEL,LINE,12));system.addView(diagnostics,spaced(-1,-2));
        system.addView(actionButton("Self-test payload","selftest"),spaced(-1,-2));system.addView(button("Perbarui status",ACCENT,new Runnable(){public void run(){controller.refreshStatus();}}),spaced(-1,-2));
        system.addView(info("Cakupan rilis","18 kontrol tersedia. Katalog 47 fitur adalah referensi cakupan desain; gold/gem, inventori, progres server, ESP, dan freecam belum tersedia.\n\nPintasan: END = PANIC · INSERT = perkecil · F1–F6 = enam kontrol karakter pertama."));
    }
    private void buildCatalog(){
        catalog.addView(info("Katalog desain · 47 entri","Referensi cakupan, bukan 47 kontrol aktif. Lihat status setiap entri; kontrol yang tersedia tetap berada di kategori utama."),spaced(-1,-2));
        for(final String[] item:FeatureCatalog.ROWS){String state=item[3].equals("partial")?"CAKUPAN SEBAGIAN":item[3].equals("prototype")?"EKSPERIMENTAL":item[3].equals("target_absent")?"MEKANIK BELUM DITEMUKAN":item[3].equals("authority_unverified")?"TRANSAKSI BELUM TERVERIFIKASI":"BELUM DIIMPLEMENTASIKAN";
            LinearLayout card=info(item[1],item[4]);TextView label=text(state+" · "+item[2],10,ACCENT);label.setPadding(0,dp(10),0,dp(6));card.addView(label,0);if(!item[5].isEmpty()){final FeatureDefinition feature=FeatureDefinition.find(item[5]);if(feature!=null){TextView link=button("Buka "+feature.name,ACCENT,new Runnable(){public void run(){category=feature.category;search.setText(feature.id);filter();scroll.scrollTo(0,0);}});LinearLayout.LayoutParams p=new LinearLayout.LayoutParams(-1,-2);p.topMargin=dp(10);card.addView(link,p);}}
            catalogCards.add(new CatalogCard(card,Arrays.toString(item).toLowerCase(Locale.ROOT)));catalog.addView(card,spaced(-1,-2));}
    }
    private void saveProfile(final int slot,final String name){
        final MenuProfile saved=controller.captureProfile();if(saved==null)return;
        dismissDialog();dialog=new AlertDialog.Builder(activity).setTitle("Simpan profil "+name+"?")
            .setMessage(saved.enabledCount()+" kontrol ON dan nilai slider akan disimpan. Slot ini akan diganti.")
            .setNegativeButton("Batal",null).setPositiveButton("Simpan",new DialogInterface.OnClickListener(){public void onClick(DialogInterface ignored,int which){
                if(!controller.canUseProfile()){controller.notifyUser("Tunggu sesi siap dan semua perintah selesai");return;}
                try{preferences.saveProfile(slot,saved);controller.notifyUser("Profil "+name+" disimpan · "+saved.enabledCount()+" ON");}catch(Exception error){controller.notifyUser("Profil gagal disimpan · "+error.getClass().getSimpleName());}
            }}).show();
    }
    private void loadProfile(final int slot,final String name){
        if(!controller.canUseProfile()){controller.notifyUser("Tunggu sesi siap dan semua perintah selesai");return;}
        try{final MenuProfile saved=preferences.loadProfile(slot);if(saved.entries.isEmpty()){controller.notifyUser("Slot "+name+" masih kosong");return;}
            dismissDialog();dialog=new AlertDialog.Builder(activity).setTitle("Terapkan profil "+name+"?")
                .setMessage(saved.enabledCount()+" ON dari "+saved.entries.size()+" kontrol tersimpan. Perintah diterapkan satu per satu; proses berhenti jika ada kegagalan. PANIC selalu tersedia.")
                .setNegativeButton("Batal",null).setPositiveButton("Terapkan",new DialogInterface.OnClickListener(){public void onClick(DialogInterface ignored,int which){controller.loadProfile(saved);}}).show();
        }catch(Exception error){controller.notifyUser("Profil tidak dapat dibaca · "+error.getClass().getSimpleName());}
    }
    public void attach(ViewGroup parent){
        this.parent=parent;parent.addView(root,new ViewGroup.LayoutParams(1,1));parent.addView(badge,new ViewGroup.LayoutParams(-2,-2));root.setX(dp(12));root.setY(dp(12));badge.setX(dp(12));badge.setY(dp(12));
        layoutListener=new View.OnLayoutChangeListener(){public void onLayoutChange(View view,int l,int t,int r,int b,int oldL,int oldT,int oldR,int oldB){resize(r-l,b-t);}};parent.addOnLayoutChangeListener(layoutListener);
        int width=parent.getWidth()>0?parent.getWidth():activity.getResources().getDisplayMetrics().widthPixels;
        int height=parent.getHeight()>0?parent.getHeight():activity.getResources().getDisplayMetrics().heightPixels;resize(width,height);setCollapsed(false);
    }
    private void resize(int width,int height){
        if(released||width<=0||height<=0||(width==lastWidth&&height==lastHeight))return;lastWidth=width;lastHeight=height;
        int panelWidth=Math.min(dp(860),Math.max(dp(240),width-dp(24))),panelHeight=Math.min(dp(720),Math.max(dp(180),height-dp(24)));
        ViewGroup.LayoutParams rootParams=root.getLayoutParams();rootParams.width=panelWidth;rootParams.height=panelHeight;root.setLayoutParams(rootParams);
        wide=panelWidth>=dp(620);columns=panelWidth>=dp(740);railScroll.setVisibility(wide?View.VISIBLE:View.GONE);tabsScroll.setVisibility(wide?View.GONE:View.VISIBLE);
        for(LinearLayout pair:pairs){pair.setOrientation(columns?LinearLayout.HORIZONTAL:LinearLayout.VERTICAL);for(int i=0;i<pair.getChildCount();i++){LinearLayout.LayoutParams params=spaced(columns?0:-1,-2);params.weight=columns?1:0;if(columns&&i==1)params.leftMargin=dp(8);pair.getChildAt(i).setLayoutParams(params);}}
        root.setX(Math.max(0,Math.min(root.getX(),width-panelWidth)));root.setY(Math.max(0,Math.min(root.getY(),height-panelHeight)));
    }
    private void filter(){
        if(released)return;boolean isCatalog=category.equals("Katalog"),isSystem=category.equals("Sistem"),isProfiles=category.equals("Profil"),isControls=!isCatalog&&!isSystem&&!isProfiles;
        if(isCatalog&&!catalogBuilt){buildCatalog();catalogBuilt=true;}
        controls.setVisibility(isControls?View.VISIBLE:View.GONE);catalog.setVisibility(isCatalog?View.VISIBLE:View.GONE);system.setVisibility(isSystem?View.VISIBLE:View.GONE);profiles.setVisibility(isProfiles?View.VISIBLE:View.GONE);search.setVisibility(isControls||isCatalog?View.VISIBLE:View.GONE);
        int count=0;for(Card card:cards.values()){boolean visible=isControls&&(category.equals("Semua")||category.equals(card.definition.category))&&card.definition.matches(query);card.view.setVisibility(visible?View.VISIBLE:View.GONE);if(visible)count++;}
        for(LinearLayout pair:pairs){boolean visible=false;for(int i=0;i<pair.getChildCount();i++)visible|=pair.getChildAt(i).getVisibility()==View.VISIBLE;pair.setVisibility(visible?View.VISIBLE:View.GONE);}
        worldActions.setVisibility(isControls&&query.isEmpty()&&(category.equals("Semua")||category.equals("Dunia"))?View.VISIBLE:View.GONE);
        battleActions.setVisibility(isControls&&query.isEmpty()&&(category.equals("Semua")||category.equals("Pertempuran"))?View.VISIBLE:View.GONE);
        if(isCatalog){count=0;for(CatalogCard card:catalogCards){boolean visible=card.search.contains(query);card.view.setVisibility(visible?View.VISIBLE:View.GONE);if(visible)count++;}}
        empty.setVisibility((isControls||isCatalog)&&count==0?View.VISIBLE:View.GONE);
        setText(sectionTitle,isControls?category.toUpperCase(Locale.ROOT)+"  /  "+count+" KONTROL":isCatalog?"KATALOG  /  "+count+" ENTRI INFORMATIF":category.toUpperCase(Locale.ROOT));
        for(TextView nav:navigation){boolean selected=category.equals(nav.getTag());nav.setSelected(selected);nav.setTextColor(selected?ACCENT:DIM);nav.setBackground(background(selected?0xFF37312B:BG,selected?0xFF6C5940:BG,10));}
    }
    public void render(MenuSnapshot snapshot){
        if(released)return;latest=snapshot;
        setText(session,(snapshot.ready?"● SIAP":"○ "+friendlySession(snapshot.session))+" · epoch "+(snapshot.epoch>0?snapshot.epoch:"—"));session.setTextColor(snapshot.ready?TEAL:ACCENT);
        setText(summary,snapshot.activeCount+" aktif / 18"+(snapshot.pendingCount>0?" · "+snapshot.pendingCount+" menunggu":""));
        setText(notice,snapshot.notice);setText(diagnostics,snapshot.diagnostics);setText(badgeLabel,snapshot.ready?"WSM · "+snapshot.activeCount+" ON"+(snapshot.pendingCount>0?" · …":""):"WSM · STATUS ?");
        setText(profileStatus,snapshot.profileProgress.isEmpty()?"Pilih slot untuk menyimpan atau memuat.":snapshot.profileProgress);
        for(Card card:cards.values()){MenuSnapshot.Control control=snapshot.controls.get(card.definition.id);if(control!=null)card.update(control,snapshot.ready&&!snapshot.profileBusy);}
        for(TextView action:actions){action.setEnabled(snapshot.ready&&!snapshot.profileBusy);action.setAlpha(action.isEnabled()?1:.45f);}
        for(TextView action:profileButtons){action.setEnabled(snapshot.ready&&!snapshot.profileBusy&&snapshot.pendingCount==0);action.setAlpha(action.isEnabled()?1:.45f);}
    }
    private String friendlySession(String state){if("identity_pending".equals(state))return "IDENTITAS BELUM SIAP";if("scene_pending".equals(state))return "MENUNGGU SCENE";if("reset_pending".equals(state))return "MENUNGGU RESET";if("fault".equals(state))return "FAULT";if("paused".equals(state))return "DIJEDA";return state;}
    private void setCollapsed(boolean value){collapsed=value;controller.setVisible(!value);root.setVisibility(value?View.GONE:View.VISIBLE);badge.setVisibility(value?View.VISIBLE:View.GONE);if(value)search.clearFocus();}
    private void drag(View handle){handle.setOnTouchListener(new View.OnTouchListener(){float x,y,startX,startY;public boolean onTouch(View view,MotionEvent event){if(event.getAction()==MotionEvent.ACTION_DOWN){x=event.getRawX();y=event.getRawY();startX=root.getX();startY=root.getY();return true;}if(event.getAction()==MotionEvent.ACTION_MOVE&&parent!=null){root.setX(Math.max(0,Math.min(startX+event.getRawX()-x,parent.getWidth()-root.getWidth())));root.setY(Math.max(0,Math.min(startY+event.getRawY()-y,parent.getHeight()-root.getHeight())));return true;}if(event.getAction()==MotionEvent.ACTION_UP){view.performClick();return true;}return false;}});}
    private void dismissDialog(){if(dialog!=null){dialog.dismiss();dialog=null;}}
    /** Releases every Activity-owned listener and view. No controller callback may retain this instance. */
    public void release(){
        if(released)return;released=true;dismissDialog();
        if(search!=null&&searchWatcher!=null)search.removeTextChangedListener(searchWatcher);
        for(Card card:cards.values())if(card.slider!=null)card.slider.setOnSeekBarChangeListener(null);
        if(parent!=null){if(layoutListener!=null)parent.removeOnLayoutChangeListener(layoutListener);parent.removeView(root);parent.removeView(badge);}
        clearListeners(root);clearListeners(badge);cards.clear();pairs.clear();navigation.clear();catalogCards.clear();actions.clear();profileButtons.clear();
        parent=null;activity=null;latest=null;searchWatcher=null;layoutListener=null;
        root=body=main=rail=controls=catalog=system=profiles=badge=worldActions=battleActions=null;
        railScroll=scroll=null;tabsScroll=null;search=null;session=summary=sectionTitle=notice=diagnostics=empty=badgeLabel=profileStatus=null;
    }
    private void clearListeners(View view){view.setOnClickListener(null);view.setOnTouchListener(null);view.setOnKeyListener(null);if(view instanceof ViewGroup){ViewGroup group=(ViewGroup)view;for(int i=0;i<group.getChildCount();i++)clearListeners(group.getChildAt(i));}}
    private static final class CatalogCard {final LinearLayout view;final String search;CatalogCard(LinearLayout view,String search){this.view=view;this.search=search;}}
    private final class Card {
        final FeatureDefinition definition;
        final LinearLayout view;
        final TextView toggle,stateLabel,valueLabel;
        final SeekBar slider;
        String lastPhase="",lastDetail="";boolean lastApplied,lastReady,dragging;boolean first=true;
        Card(final FeatureDefinition definition){
            this.definition=definition;view=column();view.setPadding(dp(12),dp(10),dp(12),dp(10));view.setBackground(background(PANEL,LINE,12));
            LinearLayout top=row();LinearLayout names=column();TextView title=text(definition.name,14,TEXT);title.setTypeface(Typeface.DEFAULT,Typeface.BOLD);names.addView(title);TextView type=text(definition.experimental?"EKSPERIMENTAL":"KONFIGURASI",9,definition.experimental?ACCENT:DIM);type.setPadding(0,dp(5),0,0);names.addView(type);top.addView(names,new LinearLayout.LayoutParams(0,-2,1));
            toggle=button("OFF",DIM,new Runnable(){public void run(){controller.toggle(definition.id);}});toggle.setMinWidth(dp(66));LinearLayout.LayoutParams tp=new LinearLayout.LayoutParams(-2,dp(48));tp.leftMargin=dp(8);top.addView(toggle,tp);view.addView(top);
            TextView description=text(definition.description,12,DIM);description.setLineSpacing(dp(2),1);description.setPadding(0,dp(7),0,0);view.addView(description);
            if(definition.hasValue()){
                LinearLayout values=row();valueLabel=text(definition.format(definition.initial),14,ACCENT);valueLabel.setTypeface(Typeface.MONOSPACE,Typeface.BOLD);valueLabel.setMinWidth(dp(46));values.addView(valueLabel);
                slider=new SeekBar(activity);slider.setMax(Math.round((definition.max-definition.min)/definition.step));slider.setMinHeight(dp(48));slider.setProgressTintList(ColorStateList.valueOf(ACCENT));slider.setThumbTintList(ColorStateList.valueOf(ACCENT));slider.setContentDescription(definition.name+" nilai");
                slider.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener(){public void onProgressChanged(SeekBar bar,int progress,boolean user){if(user)controller.setValue(definition.id,definition.min+progress*definition.step,!dragging);}public void onStartTrackingTouch(SeekBar bar){dragging=true;}public void onStopTrackingTouch(SeekBar bar){dragging=false;controller.setValue(definition.id,definition.min+bar.getProgress()*definition.step,true);}});values.addView(slider,new LinearLayout.LayoutParams(0,dp(48),1));view.addView(values);
            }else{slider=null;valueLabel=null;}
            stateLabel=text("OFF · konfigurasi nonaktif",10,DIM);stateLabel.setPadding(0,dp(8),0,0);stateLabel.setMaxLines(2);view.addView(stateLabel);
        }
        void update(MenuSnapshot.Control control,boolean ready){
            String label=control.pending?"…":control.applied?"ON":"OFF";setText(toggle,label);int color=control.pending?ACCENT:control.error()?RED:control.applied?TEAL:DIM;toggle.setTextColor(color);
            toggle.setEnabled(ready&&!control.pending);toggle.setAlpha(toggle.isEnabled()?1:.55f);toggle.setContentDescription(definition.name+", "+(control.pending?"menunggu hasil":control.applied?"ON, konfigurasi diterapkan":"OFF")+", ketuk untuk mengubah");
            if(first||!lastPhase.equals(control.phase)||lastApplied!=control.applied||lastReady!=latest.ready||!lastDetail.equals(control.detail)){
                view.setBackground(background(PANEL,control.pending?0xFF6C5940:control.error()?0xFF784253:control.applied?0xFF3B786C:LINE,12));
                String state=control.pending?"MENUNGGU · belum selesai":control.error()?control.phase.toUpperCase(Locale.ROOT)+" · "+control.detail:!latest.ready?"STATUS TERAKHIR · menunggu snapshot":control.applied?"ON · konfigurasi diterapkan":"OFF · konfigurasi nonaktif";setText(stateLabel,state);stateLabel.setTextColor(color);
                first=false;lastPhase=control.phase;lastApplied=control.applied;lastReady=latest.ready;lastDetail=control.detail;
            }
            if(slider!=null){slider.setEnabled(!control.pending&&!latest.profileBusy&&(!control.applied||latest.ready));int progress=Math.round((control.value-definition.min)/definition.step);if(slider.getProgress()!=progress)slider.setProgress(progress);setText(valueLabel,definition.format(control.value));}
        }
    }
}
