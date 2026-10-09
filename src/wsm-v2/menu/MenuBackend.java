package wsm;

import java.util.Collections;
import java.util.LinkedHashMap;
import java.util.Map;

/** Android-free backend seam: parsing belongs to the backend worker, never the view. */
public interface MenuBackend {
    final class Reply {
        public final long id, epoch;
        public final String state, detail;
        public Reply(long id,long epoch,String state,String detail) {
            this.id=id;this.epoch=epoch;this.state=state;this.detail=detail;
        }
    }
    final class Value {
        public final boolean on;
        public final float value;
        public Value(boolean on,float value){this.on=on;this.value=value;}
    }
    final class Status {
        public final long epoch;
        public final boolean ready;
        public final String state, diagnostics;
        public final Map<String,Value> features;
        public Status(long epoch,boolean ready,String state,String diagnostics,Map<String,Value> features) {
            this.epoch=epoch;this.ready=ready;this.state=state;this.diagnostics=diagnostics;
            this.features=Collections.unmodifiableMap(new LinkedHashMap<String,Value>(features));
        }
    }
    Status status() throws Exception;
    Reply execute(String command) throws Exception;
    /** Only the native urgent PANIC path; bypasses the worker queue. */
    Reply panic() throws Exception;
}
