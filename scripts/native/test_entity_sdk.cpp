#include <windows.h>
#include "../../src/native/entity_capsule_geometry.h"
#include <cassert>
#include <cstdio>
int main() {
    using namespace entity_capsule_geometry;
    auto dll=LoadLibraryExW(L"Z:\\media\\E\\CONTROL-Resonant-SteamRIP.comm\\CONTROL-Resonant-SteamRIP\\CONTROL Resonant\\PhysXCommon_64.dll",nullptr,LOAD_WITH_ALTERED_SEARCH_PATH);
    assert(dll);
    using Overlap=unsigned char (*)(const void*,const void*,const void*,const void*,const unsigned*,void*);
    using Distance=float (*)(const float*,const void*,const void*,float*,unsigned*,const unsigned*);
    auto overlap=reinterpret_cast<Overlap>(GetProcAddress(dll,"?overlap@PxGeometryQuery@physx@@SA_NAEBVPxGeometry@2@AEBV?$PxTransformT@M@2@01V?$PxFlags@W4Enum@PxGeometryQueryFlag@physx@@I@2@PEAUPxQueryThreadContext@2@@Z"));
    auto distance=reinterpret_cast<Distance>(GetProcAddress(dll,"?pointDistance@PxGeometryQuery@physx@@SAMAEBV?$PxVec3T@M@2@AEBVPxGeometry@2@AEBV?$PxTransformT@M@2@PEAV32@PEAIV?$PxFlags@W4Enum@PxGeometryQueryFlag@physx@@I@2@@Z"));
    assert(overlap && distance);
    struct Box { unsigned type=3,flags=0; float extents[3]{1,1,1}; } box;
    Capsule cap;cap.radius=.4f;cap.half_height=.4f;
    alignas(16) Pose a{},b{}; const unsigned flags=1;
    a.p[0]=10;
    assert(!overlap(&cap,&a,&box,&b,&flags,nullptr));
    assert(distance(a.p,&box,&b,nullptr,nullptr,&flags)>0);
    a.p[0]=0;
    assert(overlap(&cap,&a,&box,&b,&flags,nullptr));
    assert(distance(a.p,&box,&b,nullptr,nullptr,&flags)==0);
    a.p[0]=1.35f;cap.half_height=.0001f;
    assert(overlap(&cap,&a,&box,&b,&flags,nullptr));
    assert(distance(a.p,&box,&b,nullptr,nullptr,&flags)>0);
    FreeLibrary(dll);
    puts("PASS: shipped PhysX query ABI: separated, contained, and touching capsule/box controls");
}
