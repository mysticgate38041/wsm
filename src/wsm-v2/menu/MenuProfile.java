package wsm;

import java.util.Collections;
import java.util.LinkedHashMap;
import java.util.Map;

/** A detached, immutable profile; loading is explicit and applies commands sequentially. */
public final class MenuProfile {
    public static final class Entry {
        public final boolean on;
        public final float value;
        public Entry(boolean on,float value){this.on=on;this.value=value;}
    }
    public final Map<String,Entry> entries;
    public MenuProfile(Map<String,Entry> entries) {
        LinkedHashMap<String,Entry> safe=new LinkedHashMap<String,Entry>();
        for(FeatureDefinition f:FeatureDefinition.ALL){Entry e=entries.get(f.id);if(e!=null)safe.put(f.id,new Entry(e.on,f.constrain(e.value)));}
        this.entries=Collections.unmodifiableMap(safe);
    }
    public int enabledCount(){int count=0;for(Entry e:entries.values())if(e.on)count++;return count;}
}
