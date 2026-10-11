#include "backward_pause.h"
#include <cassert>
#include <cstdint>
int main() {
    backward_pause::Window w;
    assert(!w.update(false,true,100,1,2,3));
    assert(w.update(true,true,110,1,2,3));
    assert(w.update(true,true,159,1,2,3));
    assert(!w.update(true,true,160,1,2,3));
    assert(!w.update(true,true,300,1,2,3));
    assert(!w.update(true,false,301,1,2,3));
    assert(w.update(true,true,302,1,2,3));
    assert(!w.update(false,true,303,1,2,3));
    assert(w.update(true,true,304,1,2,3));
    assert(w.update(true,true,500,1,2,4));
    assert(w.update(true,true,700,1,5,4));
    assert(w.update(true,true,UINT32_MAX-20,6,5,4));
    assert(w.update(true,true,28,6,5,4));
    assert(!w.update(true,true,29,6,5,4));
    backward_pause::Window longer;
    assert(longer.update(true,true,100,1,2,3,500));
    assert(longer.update(true,true,599,1,2,3,500));
    assert(!longer.update(true,true,600,1,2,3,500));
    backward_pause::Window shorter;
    assert(shorter.update(true,true,100,1,2,3,10));
    assert(shorter.update(true,true,109,1,2,3,10));
    assert(!shorter.update(true,true,110,1,2,3,10));
}
