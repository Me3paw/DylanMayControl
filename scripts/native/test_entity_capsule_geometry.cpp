#include "../../src/native/entity_capsule_geometry.h"
#include <cassert>
#include <cmath>
#include <cstdio>
int main() {
    using namespace entity_capsule_geometry;
    const float p[3]={3,4,5},q[4]={0,0,0,1};Capsule c{};Pose capsule_pose{};
    assert(capsule(p,q,0.4f,1.75f,c,capsule_pose));
    assert(std::fabs(c.half_height-0.475f)<1e-6f && capsule_pose.p[1]==4.875f);
    const float x[3]={1,0,0};float axis[3]{};rotate(capsule_pose.q,x,axis);
    assert(std::fabs(axis[0])<1e-6f && std::fabs(axis[1]-1)<1e-6f);
    Pose actor{},local{},world{};actor.p[0]=10;local.p[2]=2;
    assert(compose(actor,local,world) && world.p[0]==10 && world.p[2]==2);
    assert(entity_shape(3,2) && entity_shape(1,3));
    assert(!entity_shape(2,3) && !entity_shape(6,3) && !entity_shape(3,1));
    actor.q[3]=0;assert(!compose(actor,local,world));
    assert(!capsule(p,q,-1,2,c,capsule_pose));
    Capsule expanded=c;Pose nearby=capsule_pose;nearby.p[0]+=2.1f;double gap=0;
    assert(surface_gap(c,capsule_pose,c,nearby,gap)&&gap>1.29&&gap<1.31);
    const auto half=expanded.half_height;assert(inflate_query(expanded,1.5));
    assert(expanded.half_height==half&&c.radius==0.4f);
    assert(surface_gap(expanded,capsule_pose,c,nearby,gap)&&gap<0);
    nearby.p[0]+=0.3f;assert(surface_gap(expanded,capsule_pose,c,nearby,gap)&&gap>0);
    assert(!inflate_query(expanded,-1)&&!inflate_query(expanded,INFINITY));
    puts("PASS: native capsule placement/axis, actor-shape pose composition, physical/trigger/plane filtering");
}
