package wsm;

import android.content.SharedPreferences;
import java.util.LinkedHashMap;
import java.util.Map;
import org.json.JSONObject;

/** Persists draft values and three manual profiles; never auto-activates controls. */
public final class MenuPreferences implements MenuController.ValueSink {
    private final SharedPreferences preferences;
    public MenuPreferences(SharedPreferences preferences){this.preferences=preferences;}
    public Map<String,Float> values(){
        LinkedHashMap<String,Float> result=new LinkedHashMap<String,Float>();
        for(FeatureDefinition f:FeatureDefinition.ALL){float value=f.initial;try{value=preferences.getFloat("value."+f.id,value);}catch(ClassCastException ignored){}
            result.put(f.id,f.constrain(value));}
        return result;
    }
    public void save(String id,float value){preferences.edit().putFloat("value."+id,value).apply();}
    public void saveProfile(int slot,MenuProfile profile) throws Exception {
        JSONObject json=new JSONObject();
        for(Map.Entry<String,MenuProfile.Entry> entry:profile.entries.entrySet()){
            JSONObject value=new JSONObject();value.put("on",entry.getValue().on);value.put("value",entry.getValue().value);json.put(entry.getKey(),value);}
        preferences.edit().putString("profile."+slot,json.toString()).apply();
    }
    public MenuProfile loadProfile(int slot) throws Exception {
        JSONObject json=new JSONObject(preferences.getString("profile."+slot,"{}"));
        LinkedHashMap<String,MenuProfile.Entry> entries=new LinkedHashMap<String,MenuProfile.Entry>();
        for(FeatureDefinition f:FeatureDefinition.ALL){JSONObject value=json.optJSONObject(f.id);if(value!=null)entries.put(f.id,new MenuProfile.Entry(value.optBoolean("on"),(float)value.optDouble("value",f.initial)));}
        return new MenuProfile(entries);
    }
}
