#pragma once
#include "loadout_lookup_probe.h"
#ifdef _WIN32
namespace loadout_actor_resolver {
// Native metadata counts observed up to 550 tags / 530 data keys.
constexpr unsigned kCapacity=1024;
constexpr std::uint32_t kPlayer=0xf714ec8e,kDead=0x6e63a89c;
struct Metadata {
    std::uint64_t tags=0,keys=0,offsets=0;
    std::uint32_t tag_count=0,key_count=0;
};
static_assert(sizeof(Metadata)==32);
struct Location {
    std::uint64_t world=0,actor=0,packed=0,chunk=0;
    std::uint32_t row=0,row_count=0;
    std::uint16_t archetype=0;
};
struct Rows {
    Location location{}; Metadata metadata{};
    std::uint64_t loadout_base=0,cfg_base=0,mud_base=0;
    std::uintptr_t combo=0,logic=0,attack=0,execution=0;
};
enum class Reason : unsigned {
    None,InvalidIdentity,EntityCountRead,EntityIndexRange,GenerationPointerRead,
    GenerationEntryRead,GenerationMismatch,LocationPointerRead,LocationEntryRead,
    ArchetypeSentinel,ArchetypeInfoRead,ArchetypeCountRead,ArchetypeRange,ChunkRead,
    RowCountRead,RowRange,ActorColumnRead,ActorMismatch,MetadataRead,MetadataCount,
    TagRead,KeyRead,OffsetRead,DeadTag,PlayerTagMissing,ColumnMissing,ColumnZeroOffset,
    ColumnAddress,ColumnUnreadable,CurrentLocationChanged,CurrentMetadataChanged
};
struct Diagnostic {
    Reason reason=Reason::None;
    std::uint64_t world=0,actor=0,entity_count=0,generation_entry=0,packed=0;
    std::uint64_t actual_actor=0,archetype_count=0,column_row=0;
    std::uint32_t hash=0,column_offset=0,stride=0,row=0,row_count=0,tag_count=0,key_count=0;
    std::uint16_t archetype=0;
    bool player=false,dead=false;
};
inline const char* reason_name(Reason reason) {
    constexpr const char* labels[]={"none","invalid_identity","entity_count_read","entity_index_range",
        "generation_pointer_read","generation_entry_read","generation_mismatch","location_pointer_read",
        "location_entry_read","archetype_sentinel","archetype_info_read","archetype_count_read",
        "archetype_range","chunk_read","row_count_read","row_range","actor_column_read","actor_mismatch",
        "metadata_read","metadata_count","tag_read","key_read","offset_read","dead_tag","player_tag_missing",
        "column_missing","column_zero_offset","column_address","column_unreadable","current_location_changed",
        "current_metadata_changed"};
    const auto index=static_cast<unsigned>(reason);
    return index<sizeof(labels)/sizeof(labels[0]) ? labels[index] : "unknown";
}
inline bool copy(std::uintptr_t address,void* out,std::size_t size) {
    return loadout_attack_probe::copy(address,out,size);
}
template<class T> inline bool field(std::uintptr_t base,std::uint64_t offset,T& out) {
    std::uintptr_t address=0; T temporary{};
    if (!loadout_attack_probe::add(base,offset,address) || !copy(address,&temporary,sizeof(temporary))) return false;
    std::memcpy(&out,&temporary,sizeof(out)); return true;
}
inline bool locate(std::uintptr_t world,std::uint64_t actor,Location& out,Diagnostic* diagnostic=nullptr) {
    out={}; Diagnostic local{}; auto& d=diagnostic ? *diagnostic : local;
    d={}; d.world=world; d.actor=actor;
    const auto fail=[&](Reason r) { d.reason=r; return false; };
    const auto index=static_cast<std::uint32_t>(actor),generation=static_cast<std::uint32_t>(actor>>32);
    if (!world || index==UINT32_MAX) return fail(Reason::InvalidIdentity);
    std::uint64_t generations=0,locations=0,archetype_info=0,chunk=0; std::uintptr_t address=0;
    if (!field(world,0x58510,d.entity_count)) return fail(Reason::EntityCountRead);
    if (index>=d.entity_count) return fail(Reason::EntityIndexRange);
    if (!field(world,0x584e8,generations)) return fail(Reason::GenerationPointerRead);
    if (!loadout_attack_probe::row_address(generations,index,8,address) || !copy(address,&d.generation_entry,sizeof(d.generation_entry))) return fail(Reason::GenerationEntryRead);
    if (static_cast<std::uint32_t>(d.generation_entry)!=generation) return fail(Reason::GenerationMismatch);
    if (!field(world,0x58530,locations)) return fail(Reason::LocationPointerRead);
    if (!loadout_attack_probe::row_address(locations,index,8,address) || !copy(address,&d.packed,sizeof(d.packed))) return fail(Reason::LocationEntryRead);
    d.archetype=static_cast<std::uint16_t>(d.packed); d.row=static_cast<std::uint32_t>(d.packed>>32);
    if (d.archetype==UINT16_MAX) return fail(Reason::ArchetypeSentinel);
    if (!field(world,0x58478,archetype_info)) return fail(Reason::ArchetypeInfoRead);
    if (!field(static_cast<std::uintptr_t>(archetype_info),8,d.archetype_count)) return fail(Reason::ArchetypeCountRead);
    if (d.archetype>=d.archetype_count || d.archetype_count>65536) return fail(Reason::ArchetypeRange);
    if (!field(world,0x50+static_cast<std::uint64_t>(d.archetype)*8,chunk) || !chunk) return fail(Reason::ChunkRead);
    if (!field(world,0x10448+static_cast<std::uint64_t>(d.archetype)*4,d.row_count)) return fail(Reason::RowCountRead);
    if (d.row>=d.row_count) return fail(Reason::RowRange);
    if (!loadout_attack_probe::row_address(chunk,d.row,8,address) || !loadout_attack_probe::add(address,0x10,address) || !copy(address,&d.actual_actor,sizeof(d.actual_actor))) return fail(Reason::ActorColumnRead);
    if (d.actual_actor!=actor) return fail(Reason::ActorMismatch);
    out={world,actor,d.packed,chunk,d.row,d.row_count,d.archetype}; return true;
}
inline bool same(const Location& a,const Location& b) {
    return a.world==b.world && a.actor==b.actor && a.packed==b.packed && a.chunk==b.chunk && a.row_count==b.row_count;
}
inline bool current(const Rows& rows,Diagnostic* diagnostic=nullptr) {
    Location now{}; Metadata m{};
    if (!locate(static_cast<std::uintptr_t>(rows.location.world),rows.location.actor,now,diagnostic)) return false;
    if (!same(now,rows.location)) { if (diagnostic) diagnostic->reason=Reason::CurrentLocationChanged; return false; }
    if (!field(static_cast<std::uintptr_t>(now.world),0x18448+static_cast<std::uint64_t>(now.archetype)*0x20,m)) { if (diagnostic) diagnostic->reason=Reason::MetadataRead; return false; }
    if (diagnostic) { diagnostic->tag_count=m.tag_count; diagnostic->key_count=m.key_count; }
    if (m.tags!=rows.metadata.tags || m.keys!=rows.metadata.keys || m.offsets!=rows.metadata.offsets || m.tag_count!=rows.metadata.tag_count || m.key_count!=rows.metadata.key_count) { if (diagnostic) diagnostic->reason=Reason::CurrentMetadataChanged; return false; }
    return true;
}
inline bool resolve(std::uintptr_t world,std::uint64_t actor,Rows& out,Diagnostic* diagnostic=nullptr) {
    out={}; Rows result{}; Diagnostic local{}; auto& d=diagnostic ? *diagnostic : local;
    const auto fail=[&](Reason r,std::uint32_t hash=0) { d.reason=r; d.hash=hash; return false; };
    if (!locate(world,actor,result.location,&d)) return false;
    if (!field(world,0x18448+static_cast<std::uint64_t>(result.location.archetype)*0x20,result.metadata)) return fail(Reason::MetadataRead);
    const auto& m=result.metadata; d.tag_count=m.tag_count; d.key_count=m.key_count;
    if (m.tag_count>kCapacity || m.key_count>kCapacity) return fail(Reason::MetadataCount);
    std::array<std::uint32_t,kCapacity> tags{},keys{},offsets{};
    if (m.tag_count && !copy(static_cast<std::uintptr_t>(m.tags),tags.data(),static_cast<std::size_t>(m.tag_count)*4)) return fail(Reason::TagRead);
    if (m.key_count && !copy(static_cast<std::uintptr_t>(m.keys),keys.data(),static_cast<std::size_t>(m.key_count)*4)) return fail(Reason::KeyRead);
    if (m.key_count && !copy(static_cast<std::uintptr_t>(m.offsets),offsets.data(),static_cast<std::size_t>(m.key_count)*4)) return fail(Reason::OffsetRead);
    for (unsigned i=0;i<m.tag_count;++i) { if (tags[i]==kDead) d.dead=true; if (tags[i]==kPlayer) d.player=true; }
    if (d.dead) return fail(Reason::DeadTag,kDead);
    if (!d.player) return fail(Reason::PlayerTagMissing,kPlayer);
    const auto column=[&](std::uint32_t hash,unsigned stride,bool required,std::uint64_t& base,std::uintptr_t& row) {
        base=0; row=0;
        for (unsigned i=0;i<m.key_count;++i) {
            if (keys[i]!=hash) continue;
            d.column_offset=offsets[i]; d.stride=stride;
            if (!offsets[i]) return required ? fail(Reason::ColumnZeroOffset,hash) : true;
            std::uintptr_t address=0;
            if (!loadout_attack_probe::add(static_cast<std::uintptr_t>(result.location.chunk),offsets[i],address)) return fail(Reason::ColumnAddress,hash);
            base=address;
            if (!loadout_attack_probe::row_address(base,result.location.row,stride,row)) return fail(Reason::ColumnAddress,hash);
            d.column_row=row;
            if (!loadout_lookup_probe::readable_span(row,stride)) return fail(Reason::ColumnUnreadable,hash);
            return true;
        }
        return required ? fail(Reason::ColumnMissing,hash) : true;
    };
    std::uint64_t unused_base=0; std::uintptr_t unused_row=0;
    if (!column(0x692a9998,0x10,true,result.loadout_base,unused_row) || !column(0xfaf95524,0xa0,true,result.cfg_base,unused_row) ||
        !column(0x760edfc8,8,true,result.mud_base,unused_row) || !column(0x3bfcf661,0x48,true,unused_base,result.combo) ||
        !column(0x9988f341,0x810,true,unused_base,result.logic) || !column(0x546690f3,0xb0,false,unused_base,result.attack) ||
        !column(0x7703cd01,0x10,false,unused_base,result.execution) || !current(result,&d)) return false;
    d.reason=Reason::None; d.hash=0; out=result; return true;
}
struct Idle {
    std::uint32_t current=0,phase=0;
    unsigned char input[4]{};
    unsigned char logic=0,attack_a0=0,attack_ae=0,pending=0,execution=0;
};
inline bool sample(const Rows& rows,Idle& out) {
    out={}; Idle result{};
    if (!field(rows.combo,0x20,result.current) || !field(rows.combo,0x2c,result.phase) ||
        !field(rows.combo,0x28,result.input) || !field(rows.logic,0x1c,result.logic)) return false;
    if (rows.attack && (!field(rows.attack,0xa0,result.attack_a0) || !field(rows.attack,0xae,result.attack_ae) ||
                       !field(rows.attack,0x30,result.pending))) return false;
    if (rows.execution && !field(rows.execution,0,result.execution)) return false;
    out=result; return current(rows);
}
inline bool idle(const Rows& rows) {
    Idle a{},b{};
    if (!sample(rows,a) || !sample(rows,b) || a.current!=b.current || a.phase!=b.phase ||
        std::memcmp(a.input,b.input,sizeof(a.input)) || a.logic!=b.logic || a.attack_a0!=b.attack_a0 ||
        a.attack_ae!=b.attack_ae || a.pending!=b.pending || a.execution!=b.execution) return false;
    // Mask1 is the melee active mask (cb8 and native getter69).
    // Cached flags and held input do not indicate a live swing. Keep native
    // fresh-chain state, inactive melee mask and no pending attack/execution.
    return a.current==UINT32_MAX && a.phase==0 && !(a.logic&4) && !a.pending && !a.execution;
}
}
#endif
