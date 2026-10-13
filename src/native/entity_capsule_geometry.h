#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
namespace entity_capsule_geometry {
struct Pose { float q[4]{0,0,0,1},p[3]{}; };
struct Capsule { std::uint32_t type=2,flags=0; float radius=0,half_height=0; };
inline void multiply(const float* a,const float* b,float* q) {
    q[0]=a[3]*b[0]+a[0]*b[3]+a[1]*b[2]-a[2]*b[1];
    q[1]=a[3]*b[1]-a[0]*b[2]+a[1]*b[3]+a[2]*b[0];
    q[2]=a[3]*b[2]+a[0]*b[1]-a[1]*b[0]+a[2]*b[3];
    q[3]=a[3]*b[3]-a[0]*b[0]-a[1]*b[1]-a[2]*b[2];
}
inline void rotate(const float* q,const float* v,float* out) {
    const float t[3]={2*(q[1]*v[2]-q[2]*v[1]),2*(q[2]*v[0]-q[0]*v[2]),2*(q[0]*v[1]-q[1]*v[0])};
    out[0]=v[0]+q[3]*t[0]+q[1]*t[2]-q[2]*t[1];
    out[1]=v[1]+q[3]*t[1]+q[2]*t[0]-q[0]*t[2];
    out[2]=v[2]+q[3]*t[2]+q[0]*t[1]-q[1]*t[0];
}
inline bool valid(const Pose& p) {
    float n=0; for (float v:p.q) { if(!std::isfinite(v)) return false; n+=v*v; }
    for (float v:p.p) if(!std::isfinite(v)) return false;
    return n>0.99f && n<1.01f;
}
inline bool physical(std::uint8_t flags) { return (flags&1)!=0 && (flags&4)==0; }
// Infinite world planes overlap an entire half-space; that does not mean
// touching an entity body. Leave ordinary world-plane landing to the game.
inline bool entity_shape(std::uint8_t flags,std::uint32_t type) { return physical(flags) && type!=1 && type<=10; }
inline bool capsule(const float* position,const float* plane,float radius,float height,Capsule& g,Pose& pose) {
    if (!std::isfinite(radius) || !std::isfinite(height) || radius<=0 || height<=0) return false;
    g.radius=std::max(radius,0.001f); // exact142d05b00 constants
    g.half_height=std::max((height-2*g.radius)*0.5f,0.0001f);
    const float z90[4]={0,0,0.7071067812f,0.7071067812f},center[3]={0,height*0.5f,0};
    float offset[3]{}; rotate(plane,center,offset); multiply(plane,z90,pose.q);
    for (unsigned i=0;i<3;++i) pose.p[i]=position[i]+offset[i];
    return valid(pose);
}
// Expands only a local query capsule; its segment and pose stay native.
inline bool inflate_query(Capsule& cap,double margin) {
    if(!std::isfinite(margin)||margin<0||margin>2||!std::isfinite(cap.radius)||cap.radius<=0)return false;
    cap.radius+=static_cast<float>(margin);return true;
}
inline bool compose(const Pose& actor,const Pose& local,Pose& world) {
    if(!valid(actor) || !valid(local)) return false;
    multiply(actor.q,local.q,world.q); float offset[3]{}; rotate(actor.q,local.p,offset);
    for(unsigned i=0;i<3;++i) world.p[i]=actor.p[i]+offset[i];
    return valid(world);
}
// Exact distance between two finite capsule surfaces; negative means overlap.
// A caller supplies the tested margin; this helper grants no reset authority.
inline bool surface_gap(const Capsule& a,const Pose& pa,const Capsule& b,const Pose& pb,double& gap) {
    if(a.type!=2 || b.type!=2 || !valid(pa) || !valid(pb) ||
        !std::isfinite(a.radius) || !std::isfinite(b.radius) || a.radius<=0 || b.radius<=0 ||
        !std::isfinite(a.half_height) || !std::isfinite(b.half_height) || a.half_height<0 || b.half_height<0) return false;
    const float local_axis[3]={1,0,0};float fa[3]{},fb[3]{};
    rotate(pa.q,local_axis,fa);rotate(pb.q,local_axis,fb);
    double ax[3]{},bx[3]{},d[3]{},na=0,nb=0;
    for(unsigned i=0;i<3;++i) {na+=double(fa[i])*fa[i];nb+=double(fb[i])*fb[i];d[i]=double(pa.p[i])-pb.p[i];}
    if(na<=0 || nb<=0 || !std::isfinite(na) || !std::isfinite(nb)) return false;
    na=std::sqrt(na);nb=std::sqrt(nb);double da=0,db=0,c=0;
    for(unsigned i=0;i<3;++i) {ax[i]=fa[i]/na;bx[i]=fb[i]/nb;da+=d[i]*ax[i];db+=d[i]*bx[i];c+=ax[i]*bx[i];}
    const double ha=a.half_height,hb=b.half_height;
    double minimum=INFINITY;
    const auto candidate=[&](double u,double v) {double squared=0;for(unsigned i=0;i<3;++i) {const double delta=d[i]+u*ax[i]-v*bx[i];squared+=delta*delta;}minimum=std::min(minimum,squared);};
    const double determinant=1-c*c;
    if(determinant>1e-12) {
        const double v=(db-c*da)/determinant,u=-da+c*v;
        if(std::abs(u)<=ha && std::abs(v)<=hb) candidate(u,v);
    }
    for(const double u:{-ha,ha}) candidate(u,std::clamp(db+c*u,-hb,hb));
    for(const double v:{-hb,hb}) candidate(std::clamp(c*v-da,-ha,ha),v);
    gap=std::sqrt(minimum)-a.radius-b.radius;return std::isfinite(gap);
}
static_assert(sizeof(Pose)==28 && sizeof(Capsule)==16);
}
