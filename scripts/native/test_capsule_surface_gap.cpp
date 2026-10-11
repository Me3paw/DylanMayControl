#include "entity_capsule_geometry.h"
#include <cassert>
#include <cmath>
using namespace entity_capsule_geometry;
int main() {
    Capsule a{},b{};a.radius=0.4f;b.radius=0.5f;a.half_height=b.half_height=0.5f;
    Pose pa{},pb{};const float up[4]={0,0,0.7071067812f,0.7071067812f};
    for(unsigned i=0;i<4;++i) pa.q[i]=pb.q[i]=up[i];
    double gap=0;pb.p[0]=1;assert(surface_gap(a,pa,b,pb,gap) && std::abs(gap-0.1)<1e-6);
    pb.p[0]=0.9f;assert(surface_gap(a,pa,b,pb,gap) && std::abs(gap)<1e-6);
    pb.p[0]=0.915f;assert(surface_gap(a,pa,b,pb,gap) && gap>0 && gap<0.02);
    pb.p[0]=0.93f;assert(surface_gap(a,pa,b,pb,gap) && gap>0.02 && gap<0.05);
    pb.p[0]=0.8f;assert(surface_gap(a,pa,b,pb,gap) && gap<0);
    pb.p[0]=0;pb.p[1]=3;assert(surface_gap(a,pa,b,pb,gap) && std::abs(gap-1.1)<1e-6);
    pb={};assert(surface_gap(a,pa,b,pb,gap) && std::abs(gap+0.9)<1e-6);
    a.half_height=b.half_height=0;pb.p[0]=1;assert(surface_gap(a,pa,b,pb,gap) && std::abs(gap-0.1)<1e-6);
    a.radius=-1;assert(!surface_gap(a,pa,b,pb,gap));a.radius=0.4f;pb.q[3]=10;assert(!surface_gap(a,pa,b,pb,gap));
}
