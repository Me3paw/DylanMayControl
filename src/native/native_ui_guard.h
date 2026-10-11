// Startup-worker validation only; no SDK or View calls.
#pragma once
#include "native_ui.h"
#include "native_ui_pins.h"
#include <wincrypt.h>

namespace native_ui_guard {
inline bool bytes_match(std::uintptr_t address, const unsigned char* expected, std::size_t size) {
    unsigned char copied[64]{};
    SIZE_T read = 0;
    return size <= sizeof(copied) && ReadProcessMemory(GetCurrentProcess(),
        reinterpret_cast<const void*>(address), copied, size, &read) && read == size &&
        std::memcmp(copied, expected, size) == 0;
}
inline bool cohtml_file_matches(HMODULE module) {
    static constexpr BYTE expected[32] = {
        0xc0,0x26,0xd8,0x1a,0xfd,0x76,0x8a,0x17,0x34,0x13,0x9f,0xbf,0xc0,0x0b,0x3d,0xfa,
        0xaa,0xc7,0x2f,0xba,0x48,0x64,0x2b,0x0a,0xbe,0x5f,0x08,0x18,0xa6,0xdf,0xd2,0xa6};
    wchar_t path[32768];
    DWORD length = GetModuleFileNameW(module, path, 32768);
    if (!length || length >= 32768) return false;
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    HCRYPTPROV provider = 0; HCRYPTHASH hash = 0;
    LARGE_INTEGER size{};
    bool ok = GetFileSizeEx(file, &size) && size.QuadPart == cohtml_file_size &&
        CryptAcquireContextW(&provider, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT) &&
        CryptCreateHash(provider, CALG_SHA_256, 0, 0, &hash);
    BYTE buffer[65536], digest[32]; DWORD bytes = 0, digest_size = sizeof(digest);
    while (ok) {
        if (!ReadFile(file, buffer, sizeof(buffer), &bytes, nullptr)) { ok = false; break; }
        if (!bytes) break;
        ok = CryptHashData(hash, buffer, bytes, 0);
    }
    ok = ok && CryptGetHashParam(hash, HP_HASHVAL, digest, &digest_size, 0) &&
        digest_size == sizeof(digest) && std::memcmp(digest, expected, sizeof(digest)) == 0;
    if (hash) CryptDestroyHash(hash);
    if (provider) CryptReleaseContext(provider, 0);
    CloseHandle(file);
    return ok;
}
inline bool unique_lifecycle_pins(std::uintptr_t base) {
    IMAGE_DOS_HEADER dos{}; IMAGE_NT_HEADERS64 nt{}; SIZE_T read = 0;
    HANDLE process = GetCurrentProcess();
    if (!ReadProcessMemory(process, reinterpret_cast<void*>(base), &dos, sizeof(dos), &read) ||
        read != sizeof(dos) || dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew < 0 || dos.e_lfanew > 0x100000 ||
        !ReadProcessMemory(process, reinterpret_cast<void*>(base + dos.e_lfanew), &nt, sizeof(nt), &read) ||
        read != sizeof(nt) || nt.Signature != IMAGE_NT_SIGNATURE || nt.FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
        nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
        nt.FileHeader.SizeOfOptionalHeader != sizeof(IMAGE_OPTIONAL_HEADER64) || nt.FileHeader.NumberOfSections > 96) return false;
    const auto headers = base + dos.e_lfanew + offsetof(IMAGE_NT_HEADERS64, OptionalHeader) + nt.FileHeader.SizeOfOptionalHeader;
    for (unsigned index = 0; index < nt.FileHeader.NumberOfSections; ++index) {
        IMAGE_SECTION_HEADER section{};
        if (!ReadProcessMemory(process, reinterpret_cast<void*>(headers + index * sizeof(section)), &section, sizeof(section), &read) || read != sizeof(section)) return false;
        if (std::memcmp(section.Name, ".text\0\0\0", 8) != 0) continue;
        const SIZE_T length = section.Misc.VirtualSize;
        if (!(section.Characteristics & IMAGE_SCN_MEM_EXECUTE) || !length ||
            section.VirtualAddress >= nt.OptionalHeader.SizeOfImage || length > nt.OptionalHeader.SizeOfImage - section.VirtualAddress) return false;
        const auto* text = reinterpret_cast<const BYTE*>(base + section.VirtualAddress);
        for (SIZE_T offset = 0; offset < length;) {
            MEMORY_BASIC_INFORMATION memory{};
            if (!VirtualQuery(text + offset, &memory, sizeof(memory)) || memory.State != MEM_COMMIT ||
                memory.AllocationBase != reinterpret_cast<void*>(base) || (memory.Protect & (PAGE_GUARD | PAGE_NOACCESS)) ||
                !(memory.Protect & (PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY))) return false;
            SIZE_T available = static_cast<const BYTE*>(memory.BaseAddress) + memory.RegionSize - (text + offset);
            if (!available) return false;
            offset += available < length - offset ? available : length - offset;
        }
        unsigned dom = 0, release = 0, destroy = 0;
        for (SIZE_T i = 0; i < length; ++i) {
            if (i + sizeof(dom_bytes) <= length && std::memcmp(text + i, dom_bytes, sizeof(dom_bytes)) == 0) ++dom;
            if (i + sizeof(release_bytes) <= length && std::memcmp(text + i, release_bytes, sizeof(release_bytes)) == 0) ++release;
            if (i + sizeof(destroy_bytes) <= length && std::memcmp(text + i, destroy_bytes, sizeof(destroy_bytes)) == 0) ++destroy;
        }
        return dom == 1 && release == 1 && destroy == 1 &&
            bytes_match(base + dom_rva, dom_bytes, sizeof(dom_bytes)) &&
            bytes_match(base + release_rva, release_bytes, sizeof(release_bytes)) &&
            bytes_match(base + destroy_rva, destroy_bytes, sizeof(destroy_bytes));
    }
    return false;
}
inline native_ui::sdk::Guard verify(bool main_executable_hash_verified) {
    native_ui::sdk::Guard result{};
    HMODULE exe = GetModuleHandleW(nullptr), cohtml = GetModuleHandleW(L"cohtml.WindowsDesktop.dll");
    if (!main_executable_hash_verified || !exe || !cohtml || !cohtml_file_matches(cohtml)) return result;
    const auto eb = reinterpret_cast<std::uintptr_t>(exe), cb = reinterpret_cast<std::uintptr_t>(cohtml);
    std::uintptr_t dom_iat = 0, release_iat = 0; SIZE_T read = 0;
    if (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(eb + 0x3accf58), &dom_iat, sizeof(dom_iat), &read) || read != sizeof(dom_iat) ||
        !ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(eb + 0x3accf68), &release_iat, sizeof(release_iat), &read) || read != sizeof(release_iat) ||
        dom_iat != cb + empty_rva || release_iat != cb + empty_rva ||
        !bytes_match(cb + empty_rva, empty_bytes, sizeof(empty_bytes)) ||
        !bytes_match(cb + ready_rva, ready_bytes, sizeof(ready_bytes)) ||
        !bytes_match(cb + script_rva, script_bytes, sizeof(script_bytes)) || !unique_lifecycle_pins(eb)) return result;
    result = {eb, cb, true, true};
    return result;
}
inline bool verify_registration(const native_ui::sdk::Guard& verified) {
    return verified.executable_and_dll_hashes_verified &&
        verified.lifecycle_signatures_verified && verified.cohtml_base &&
        bytes_match(verified.cohtml_base + register_call_rva,
                    register_call_bytes, sizeof(register_call_bytes));
}
} // namespace native_ui_guard
