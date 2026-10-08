#include <assert.h>
#include <stdio.h>
#include "../include/NetworkWork.h"
int main() {
    NetworkWork::Gate gate;
    assert(gate.idle()&&!gate.paused());
    for(int attempt=0;attempt<10000;attempt++) {
        {
            NetworkWork::Lease startup(gate);
            assert(startup&&!gate.idle());
            {
                NetworkWork::Lease https(gate);
                assert(https);
                gate.pause();
                assert(gate.paused()&&!gate.idle());
                NetworkWork::Lease late(gate);
                assert(!late);
            } // Socket worker complete, but startup still owns the network.
            assert(!gate.idle()&&!gate.enter());
        }
        assert(gate.idle()&&gate.paused());
        assert(!gate.enter()); // Radio may now be disabled, no new requests.
        gate.resume();
        assert(gate.idle()&&!gate.paused());
    }
    puts("Network handoff PASS: blocks new owners, drains every lease, resumes on exit");
}
