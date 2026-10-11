#include "nearby_candidates.h"
#include <cassert>
int main() {
    nearby_candidates::Cache c;c.context(1,2);
    for(unsigned i=0;i<nearby_candidates::kCapacity;++i) c.offer(100+i,1+i*0.1f,false,100);
    c.offer(999,4,true,101);auto s=c.snapshot(1,2,101);assert(s.count==nearby_candidates::kCapacity && s.entries[0].actor==999);
    c.offer(888,0.5f,true,102);s=c.snapshot(1,2,102);assert(s.entries[0].actor==888 && s.entries[1].actor==999);
    c.offer(888,3,true,103);s=c.snapshot(1,2,103);unsigned seen=0;for(unsigned i=0;i<s.count;++i) seen+=s.entries[i].actor==888;assert(seen==1);
    assert(c.snapshot(1,2,5104).count==0);assert(c.snapshot(3,2,104).count==0);assert(c.snapshot(1,4,104).count==0);
    c.context(3,4);assert(c.snapshot(3,4,104).count==0);c.offer(4,0,true,104);c.offer(777,145,true,104);assert(c.snapshot(3,4,104).count==0);
    c.offer(777,1,true,UINT32_MAX-10);assert(c.snapshot(3,4,10).count==1);
}
