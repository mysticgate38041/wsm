package wsm;

/** Mutable only inside the UI-thread controller; published state is immutable. */
public final class ControlState {
    public boolean applied, desired, pending;
    public String phase="off";
    private long revision;
    public long begin(boolean value) { desired=value; pending=true; phase="pending"; return ++revision; }
    public boolean finish(long token,String outcome) {
        if(token!=revision||!pending)return false;
        finish(outcome); return true;
    }
    public void finish(String outcome) {
        ++revision; pending=false;
        if("applied".equals(outcome)){applied=desired;phase=applied?"on":"off";}
        else{desired=applied;phase=outcome;}
    }
    public void synchronize(boolean value) {
        applied=value;
        if(!pending){desired=value;if("on".equals(phase)||"off".equals(phase))phase=value?"on":"off";}
    }
}
