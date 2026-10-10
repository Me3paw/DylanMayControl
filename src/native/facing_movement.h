#pragma once
#include <cmath>
#include <cstdint>
namespace facing_movement {
struct Vec3 { float x=0,y=0,z=0; };
inline Vec3 add(Vec3 a,Vec3 b) { return {a.x+b.x,a.y+b.y,a.z+b.z}; }
inline Vec3 mul(Vec3 a,float s) { return {a.x*s,a.y*s,a.z*s}; }
inline float dot(Vec3 a,Vec3 b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
inline Vec3 cross(Vec3 a,Vec3 b) { return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x}; }
inline bool finite(Vec3 v) { return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z); }
inline bool unit(Vec3 v,Vec3& out) {
    if (!finite(v)) return false;
    const float n=std::sqrt(dot(v,v));
    if (!std::isfinite(n)||n<1e-6f) return false;
    out=mul(v,1.0f/n); return finite(out);
}
inline bool reverse_movement(Vec3 movement,Vec3 native_up,Vec3& reversed) {
    Vec3 up{};
    return unit(native_up,up) && unit(mul(add(movement,mul(up,-dot(movement,up))),-1),reversed);
}
// AttackTargeting rotates its copied movement through MovementPlane. Convert
// the current world-space target direction back through that same quaternion.
inline bool plane_local(Vec3 world,const float plane[4],Vec3& local) {
    float norm=0;
    for(unsigned i=0;i<4;++i){if(!std::isfinite(plane[i]))return false;norm+=plane[i]*plane[i];}
    if(!finite(world) || norm<0.5f || norm>1.5f)return false;
    const float scale=1/std::sqrt(norm);
    const Vec3 conjugate{-plane[0]*scale,-plane[1]*scale,-plane[2]*scale};
    const auto twice=mul(cross(conjugate,world),2);
    return unit(add(world,add(mul(twice,plane[3]*scale),cross(conjugate,twice))),local);
}
struct Basis { Vec3 forward{},right{},up{}; };
inline bool basis(Vec3 current_character_forward,Vec3 native_up,Basis& out) {
    Basis b{};
    if (!unit(native_up,b.up)) return false;
    if (!unit(add(current_character_forward,mul(b.up,-dot(current_character_forward,b.up))),b.forward)||
        !unit(cross(b.up,b.forward),b.right)) return false;
    out=b; return true;
}
inline bool back_turn_ready(float alignment,std::uint32_t age_ms) {
    return std::isfinite(alignment) && alignment<0 && age_ms<=250;
}
struct Output { Vec3 translation{}; float raw_forward=0,raw_right=0; };
inline bool filter(const Basis& b,Vec3 raw_movement,Vec3 native_translation,Output& out) {
    if (!finite(raw_movement)||!finite(native_translation)||!finite(b.forward)||!finite(b.right)||!finite(b.up)) return false;
    Output r{};
    r.raw_forward=dot(raw_movement,b.forward); r.raw_right=dot(raw_movement,b.right);
    const float forward=dot(native_translation,b.forward),right=dot(native_translation,b.right),vertical=dot(native_translation,b.up);
    r.translation=add(add(mul(b.forward,forward*(forward<0?0.5f:1.0f)),mul(b.right,right*0.5f)),mul(b.up,vertical));
    if (!finite(r.translation)||!std::isfinite(r.raw_forward)||!std::isfinite(r.raw_right)) return false;
    out=r; return true;
}
}
