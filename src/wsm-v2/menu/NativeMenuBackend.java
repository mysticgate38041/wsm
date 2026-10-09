package wsm;

import java.util.LinkedHashMap;
import org.json.JSONObject;

/** JNI adapter; status fetching and JSON parsing are called only by scheduler workers. */
final class NativeMenuBackend implements MenuBackend {
    public Status status() throws Exception {
        JSONObject json=new JSONObject(WsmMenu.exec("status"));
        LinkedHashMap<String,Value> features=new LinkedHashMap<String,Value>();
        JSONObject controls=json.optJSONObject("features");
        if(controls!=null)for(FeatureDefinition f:FeatureDefinition.ALL){JSONObject value=controls.optJSONObject(f.id);
            if(value!=null)features.put(f.id,new Value(value.optBoolean("on"),(float)value.optDouble("value",f.initial)));}
        String diagnostics="Build "+json.optString("build","—")+"\nPID "+json.optInt("pid")+" · epoch "+json.optLong("epoch",-1)+" · revisi kontrol "+(json.has("featureRevision")?Long.toString(json.optLong("featureRevision")):"—")
            +"\nPayload "+(json.optBoolean("payload")?"tersedia":"belum tersedia")+" · pemain "+json.optInt("players")
            +"\nGuard faults "+json.optInt("faults")+" · restore "+(!json.has("restorePending")?"belum diketahui":json.optBoolean("restorePending")?"menunggu":"selesai")
            +"\nLoot "+json.optInt("lootCandidates")+" kandidat / "+json.optInt("lootRequested")+" permintaan";
        return new Status(json.optLong("epoch",-1),json.optBoolean("ready"),json.optString("state","unknown"),diagnostics,features);
    }
    private Reply parse(String raw) throws Exception {
        JSONObject json=new JSONObject(raw);
        return new Reply(json.optLong("id",0),json.optLong("epoch",0),json.optString("state","fault"),json.optString("detail",""));
    }
    public Reply execute(String command) throws Exception{return parse(WsmMenu.exec(command));}
    public Reply panic() throws Exception{return parse(WsmMenu.exec("panic"));}
}
