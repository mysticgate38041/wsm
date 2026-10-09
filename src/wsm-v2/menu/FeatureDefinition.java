package wsm;

import java.util.Arrays;
import java.util.Collections;
import java.util.List;
import java.util.Locale;

/** Immutable UI and command contract. The catalog is deliberately separate. */
public final class FeatureDefinition {
    public final String id, name, description, category;
    public final boolean experimental;
    private final String searchText;
    public final float min, max, step, initial;
    private FeatureDefinition(String id, String name, String description, String category,
            boolean experimental, float min, float max, float step, float initial) {
        this.id=id; this.name=name; this.description=description; this.category=category;
        this.searchText=(name+" "+id+" "+description).toLowerCase(Locale.ROOT);
        this.experimental=experimental; this.min=min; this.max=max; this.step=step; this.initial=initial;
    }
    private static FeatureDefinition toggle(String id,String name,String description,String category,boolean experimental) {
        return new FeatureDefinition(id,name,description,category,experimental,0,0,0,1);
    }
    private static FeatureDefinition slider(String id,String name,String description,String category,boolean experimental,float min,float max,float step,float initial) {
        return new FeatureDefinition(id,name,description,category,experimental,min,max,step,initial);
    }
    public boolean hasValue() { return max>min; }
    public float constrain(float value) {
        if(!hasValue())return initial;
        if(Float.isNaN(value)||Float.isInfinite(value))return initial;
        float clamped=Math.max(min,Math.min(max,value));
        return Math.max(min,Math.min(max,min+Math.round((clamped-min)/step)*step));
    }
    public String format(float value) {
        if(!hasValue()||step>=1)return String.format(Locale.ROOT,"%.0f",value);
        return String.format(Locale.ROOT,"%.2f",value);
    }
    public String command(boolean on,float value) {
        String prefix=id.equals("speed")||id.equals("critdmg")||id.equals("godmode")||id.equals("nocd")||id.equals("loot")||id.equals("stunall")?id+" ":"feat "+id+" ";
        return prefix+Float.toString(on?(hasValue()?constrain(value):1):0);
    }
    public boolean matches(String query) {
        return searchText.contains(query);
    }
    public static final List<FeatureDefinition> ALL=Collections.unmodifiableList(Arrays.asList(
        toggle("god","God Mode / Opsi","Opsi Immortal + Invincible pada pemain.","Karakter",false),
        toggle("hp","HP Protection","Opsi Immortal; tidak mengisi ulang HP.","Karakter",false),
        toggle("stam","Stamina Refill","Isi ulang stamina selama scene aktif.","Karakter",false),
        toggle("mana","Mana Refill","Isi ulang meter skill.","Karakter",false),
        toggle("poise","Super Armor","Opsi anti knockback, stun, dan knockdown.","Karakter",false),
        toggle("immune","Status Immunity","Opsi status karakter; cakupan belum terukur.","Karakter",false),
        toggle("godmode","Damage Guard","Perlindungan damage khusus hero; cakupan damage belum terukur.","Karakter",true),
        toggle("ohk","Auto-Kill Pulse","Pulse serangan pada target di dalam radius.","Pertempuran",false),
        toggle("onehp","Enemy 1 HP","Berlaku per scene; Auto-Kill punya prioritas.","Pertempuran",false),
        slider("aura","Radius Pulse","Jarak dari hero, dalam meter.","Pertempuran",false,5,40,1,20),
        slider("dmg","Pulse Power","ON memulai pulse berkala ×100.000; Enemy 1 HP menonaktifkan modifier.","Pertempuran",true,1,99,1,10),
        toggle("crit","Critical Pulse","Pulse 1 juta atau Pulse Power. Hasil hit belum terukur.","Pertempuran",true),
        slider("critdmg","Critical Damage Scale","Nilai damage kritis hero ×1–5; hasil hit belum terukur.","Pertempuran",true,1,5,.25f,2),
        toggle("nocd","Cooldown Gates","Opsi cooldown skill; semua bagian wajib berhasil diterapkan.","Pertempuran",true),
        toggle("stunall","Freeze AI","Membatasi pembaruan AI; cakupan perilaku belum terukur.","Pertempuran",true),
        slider("speed","Movement Scale","Skala gerak berjalan dan dash hero.","Dunia",true,1,5,.25f,2),
        toggle("loot","Auto-Loot","OFF menghentikan permintaan baru; pickup yang telah diminta tetap berjalan.","Dunia",true),
        slider("timescale","Time Scale","OFF menghapus modifier WSM saja.","Dunia",false,.1f,5,.05f,1)
    ));
    public static FeatureDefinition find(String id) {
        for(FeatureDefinition f:ALL)if(f.id.equals(id))return f;
        return null;
    }
}
