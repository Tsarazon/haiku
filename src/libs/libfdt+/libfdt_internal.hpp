// SPDX-License-Identifier: (GPL-2.0-or-later OR BSD-2-Clause)
//
// libfdt++ internal header. Not for direct inclusion by consumers.

#ifndef LIBFDT_INTERNAL_HPP
#define LIBFDT_INTERNAL_HPP

#include <libfdt.hpp>
#include <string.h>
#include <limits.h>

// FDT binary format constants

constexpr uint32_t kFdtMagic        = 0xd00dfeed;
constexpr uint32_t kFdtSwMagic      = ~kFdtMagic;
constexpr uint32_t kFdtTagSize      = sizeof(uint32_t);

constexpr uint32_t kFdtBeginNode    = 0x1;
constexpr uint32_t kFdtEndNode      = 0x2;
constexpr uint32_t kFdtProp         = 0x3;
constexpr uint32_t kFdtNop          = 0x4;
constexpr uint32_t kFdtEnd          = 0x9;

constexpr int kFdtFirstSupportedVersion = 0x02;
constexpr int kFdtLastCompatibleVersion = 0x10;
constexpr int kFdtLastSupportedVersion  = 0x11;

constexpr uint32_t kFdtMaxPhandle   = 0xfffffffe;
constexpr int      kFdtMaxNCells    = 4;

constexpr uint32_t kFdtCreateFlagsAll = kFdtCreateFlagNoNameDedup;

// Binary format structures

struct FdtHeader {
    uint32_t magic;
    uint32_t totalsize;
    uint32_t off_dt_struct;
    uint32_t off_dt_strings;
    uint32_t off_mem_rsvmap;
    uint32_t version;
    uint32_t last_comp_version;
    uint32_t boot_cpuid_phys;
    uint32_t size_dt_strings;
    uint32_t size_dt_struct;
};

struct FdtReserveEntry {
    uint64_t address;
    uint64_t size;
};

struct FdtNodeHeader {
    uint32_t tag;
    char name[];
};

struct FdtProperty {
    uint32_t tag;
    uint32_t len;
    uint32_t nameoff;
    char data[];
};

constexpr size_t kFdtV1Size  = 7 * sizeof(uint32_t);
constexpr size_t kFdtV2Size  = kFdtV1Size + sizeof(uint32_t);
constexpr size_t kFdtV3Size  = kFdtV2Size + sizeof(uint32_t);
constexpr size_t kFdtV16Size = kFdtV3Size;
constexpr size_t kFdtV17Size = kFdtV16Size + sizeof(uint32_t);

// Byte-order helpers

namespace fdt_detail {

// strnlen fallback for freestanding environments
inline size_t Strnlen(const char* s, size_t maxlen) {
    const char* p = static_cast<const char*>(memchr(s, '\0', maxlen));
    return p ? size_t(p - s) : maxlen;
}

inline uint32_t Align(uint32_t x, uint32_t a) { return (x + a - 1) & ~(a - 1); }
inline uint32_t TagAlign(uint32_t x) { return Align(x, kFdtTagSize); }

inline uint16_t Load16(const void* p) {
    auto b = static_cast<const uint8_t*>(p);
    return (uint16_t(b[0]) << 8) | b[1];
}
inline uint32_t Load32(const void* p) {
    auto b = static_cast<const uint8_t*>(p);
    return (uint32_t(b[0]) << 24) | (uint32_t(b[1]) << 16) | (uint32_t(b[2]) << 8) | b[3];
}
inline void Store32(void* p, uint32_t v) {
    auto b = static_cast<uint8_t*>(p);
    b[0] = v >> 24; b[1] = (v >> 16) & 0xff; b[2] = (v >> 8) & 0xff; b[3] = v & 0xff;
}
inline uint64_t Load64(const void* p) {
    auto b = static_cast<const uint8_t*>(p);
    return (uint64_t(b[0]) << 56) | (uint64_t(b[1]) << 48) | (uint64_t(b[2]) << 40) | (uint64_t(b[3]) << 32)
         | (uint64_t(b[4]) << 24) | (uint64_t(b[5]) << 16) | (uint64_t(b[6]) << 8) | b[7];
}
inline void Store64(void* p, uint64_t v) {
    auto b = static_cast<uint8_t*>(p);
    b[0] = v >> 56; b[1] = (v >> 48) & 0xff; b[2] = (v >> 40) & 0xff; b[3] = (v >> 32) & 0xff;
    b[4] = (v >> 24) & 0xff; b[5] = (v >> 16) & 0xff; b[6] = (v >> 8) & 0xff; b[7] = v & 0xff;
}

inline const char* FindStringLen(const char* strtab, int tabsize, const char* s, int slen) {
    const char* last = strtab + tabsize - (slen + 1);
    for (const char* p = strtab; p <= last; p++)
        if (memcmp(p, s, slen) == 0 && p[slen] == '\0') return p;
    return nullptr;
}

} // namespace fdt_detail

// FdtAccess: friend struct providing implementation access to private members

struct FdtAccess {
    // blob access
    static void*& Blob(Fdt& f) { return f.blob_; }
    static void* Blob(const Fdt& f) { return f.blob_; }
    static void*& SwBuf(FdtSw& s) { return s.buf_; }
    static void* SwBuf(const FdtSw& s) { return s.buf_; }

    // Header helpers
    static uint32_t GetHeader(const Fdt& f, uint32_t FdtHeader::*field);
    static void SetHeader(Fdt& f, uint32_t FdtHeader::*field, uint32_t val);
    static int32_t RoProbe(const Fdt& f);

    // Pointer helpers
    static const void* OffsetPtr(const Fdt& f, int offset);
    static void* OffsetPtrW(Fdt& f, int offset);
    static const FdtReserveEntry* MemRsv(const Fdt& f, int n);
    static FdtReserveEntry* MemRsvW(Fdt& f, int n);

    // Structure
    static const void* OffsetPtrChecked(const Fdt& f, int offset, unsigned len);
    static uint32_t NextTag(const Fdt& f, int startoffset, int* nextoffset);
    static int CheckNodeOffset(const Fdt& f, int offset);
    static int CheckPropOffset(const Fdt& f, int offset);

    // String helpers
    static const char* GetString(const Fdt& f, int stroffset, int* lenp);
    static bool StringEq(const Fdt& f, int stroffset, const char* s, int len);
    static bool NodeNameEq(const Fdt& f, int offset, const char* s, int len);

    // Property internals
    static const FdtProperty* GetPropertyByOffset_(const Fdt& f, int offset, int* lenp);
    static const FdtProperty* GetPropertyNamelen_(const Fdt& f, int offset,
        const char* name, int namelen, int* lenp, int* poffset);

    // Path internals
    static const void* PathGetPropNamelen(const Fdt& f, const char* path,
        const char* propname, int propnamelen, int* lenp);

    // FdtRw internals
    static int RwProbe(FdtRw& rw);
    static unsigned DataSize(FdtRw& rw);
    static int Splice(FdtRw& rw, void* sp, int oldlen, int newlen);
    static int SpliceMemRsv(FdtRw& rw, FdtReserveEntry* p, int oldn, int newn);
    static int SpliceStruct(FdtRw& rw, void* p, int oldlen, int newlen);
    static int SpliceString(FdtRw& rw, int newlen);
    static void DelLastString(FdtRw& rw, const char* s);
    static int FindAddString(FdtRw& rw, const char* s, int slen, int* alloc);
    static int ResizeProperty(FdtRw& rw, int nodeoff, const char* name,
        int namelen, int len, FdtProperty** prop);
    static int AddProperty(FdtRw& rw, int nodeoff, const char* name,
        int namelen, int len, FdtProperty** prop);
    static void NopRegion(void* start, int len);
    static int NodeEndOffset(FdtRw& rw, int offset);
    static void PackBlocks(const char* old, char* dst, int mrs, int ss, int sts);
    static bool BlocksMisordered(FdtRw& rw, int mrs, int ss);
    static int GraftNode(FdtRw& rw, const Fdt& src, int sn, int dp, const char* name);

    // FdtSw internals
    static int SwProbe(FdtSw& sw);
    static int SwProbeMemrsv(FdtSw& sw);
    static int SwProbeStruct(FdtSw& sw);
    static uint32_t SwFlags(FdtSw& sw);
    static void* GrabSpace(FdtSw& sw, size_t len);
    static int SwAddString(FdtSw& sw, const char* s);
    static void SwDelLastString(FdtSw& sw, const char* s);
    static int SwFindAddString(FdtSw& sw, const char* s, int* alloc);
};

#endif // LIBFDT_INTERNAL_HPP
