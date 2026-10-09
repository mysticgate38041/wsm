package wsm;

import java.util.Collections;
import java.util.LinkedHashMap;
import java.util.Map;

/** The view sees value objects only, never mutable control state or backend JSON. */
public final class MenuSnapshot {
    public static final class Control {
        public final boolean applied,desired,pending;
        public final float value;
        public final String phase,detail;
        Control(ControlState state,float value,String detail) {
            applied=state.applied;desired=state.desired;pending=state.pending;
            phase=state.phase;this.value=value;this.detail=detail;
        }
        public boolean error(){return !pending&&!"on".equals(phase)&&!"off".equals(phase);}
    }
    public final Map<String,Control> controls;
    public final boolean attached,ready,profileBusy;
    public final long epoch;
    public final int activeCount,pendingCount;
    public final String session,diagnostics,notice,profileProgress;
    MenuSnapshot(Map<String,Control> controls,boolean attached,boolean ready,long epoch,String session,
                 String diagnostics,String notice,boolean profileBusy,String profileProgress,int pendingRequests) {
        this.controls=Collections.unmodifiableMap(new LinkedHashMap<String,Control>(controls));
        this.attached=attached;this.ready=ready;this.epoch=epoch;this.session=session;
        this.diagnostics=diagnostics;this.notice=notice;this.profileBusy=profileBusy;this.profileProgress=profileProgress;
        int active=0,pending=0;for(Control c:controls.values()){if(c.applied)active++;if(c.pending)pending++;}
        activeCount=active;pendingCount=Math.max(pending,pendingRequests);
    }
}
