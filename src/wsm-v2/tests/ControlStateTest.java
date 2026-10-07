import wsm.ControlState;
public final class ControlStateTest {
    static void require(boolean ok){if(!ok)throw new AssertionError();}
    public static void main(String[] args){
        ControlState s=new ControlState();
        s.begin(true);require(s.pending&&!s.applied&&s.desired);
        s.finish("rejected");require(!s.pending&&!s.applied&&!s.desired);
        s.begin(true);s.finish("applied");require(s.applied&&s.desired);
        s.begin(false);s.finish("timeout");require(s.applied&&s.desired);
        s.begin(false);s.finish("applied");require(!s.applied&&!s.pending);
        s.begin(true);s.synchronize(false);require(s.pending&&s.desired&&!s.applied);
        s.finish("stale");require(!s.pending&&!s.desired&&!s.applied);
        s.synchronize(true);require(s.applied&&s.desired);
        s.begin(false);s.finish("fault");require(s.applied&&s.desired);
        long old=s.begin(false), current=s.begin(true);
        require(!s.finish(old,"applied")&&s.pending&&s.desired);
        require(s.finish(current,"applied")&&s.applied&&!s.pending);
        require(!s.finish(current,"rejected")&&s.applied);
        old=s.begin(false);s.finish("stale");require(!s.finish(old,"applied")&&s.applied);
        System.out.println("PASS ControlState: accepted vs applied, rejection, timeout, stale epoch, fault, authoritative snapshot");
    }
}
