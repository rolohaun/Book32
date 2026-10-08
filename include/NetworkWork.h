#pragma once
#include <atomic>
#include <stdint.h>

// LILYGO's offline emulator must not deinitialize Wi-Fi while a background
// TLS/startup worker still owns network buffers. One atomic word closes the
// admission gate and counts existing owners without an acquire/pause race.
namespace NetworkWork {
class Gate {
    static constexpr uint32_t CLOSED = uint32_t(1) << 31;
    std::atomic<uint32_t> state{0};
public:
    bool enter() {
        uint32_t value=state.load();
        while(!(value&CLOSED)) {
            if(state.compare_exchange_weak(value,value+1))return true;
        }
        return false;
    }
    void leave(){state.fetch_sub(1);}
    void pause(){state.fetch_or(CLOSED);}
    bool paused()const{return (state.load()&CLOSED)!=0;}
    bool idle()const{return (state.load()&~CLOSED)==0;}
    void resume(){state.fetch_and(~CLOSED);}
};
inline Gate& gate(){static Gate instance;return instance;}
class Lease {
    Gate& owner;
    bool held;
public:
    explicit Lease(Gate& value=gate()):owner(value),held(owner.enter()){}
    ~Lease(){if(held)owner.leave();}
    explicit operator bool()const{return held;}
    Lease(const Lease&)=delete;
    Lease& operator=(const Lease&)=delete;
};
}
