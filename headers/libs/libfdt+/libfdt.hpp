// SPDX-License-Identifier: (GPL-2.0-or-later OR BSD-2-Clause)
//
// libfdt++ - Flat Device Tree manipulation (C++ rewrite)
// Based on libfdt by David Gibson, IBM Corporation.
//
// Public API header.
// Freestanding-compatible: no C++ stdlib dependency.

#ifndef LIBFDT_HPP
#define LIBFDT_HPP

// Constants safe for inclusion from assembly (.S) files.
#define FDT_MAGIC   0xd00dfeed
#define FDT_VERSION 17

#ifndef __ASSEMBLER__

#include <stdint.h>
#include <stddef.h>

// Error codes (negated in return values, e.g. -kFdtErrNotFound)

enum FdtError : int {
    kFdtErrNone          =  0,
    kFdtErrNotFound      =  1,
    kFdtErrExists        =  2,
    kFdtErrNoSpace       =  3,
    kFdtErrBadOffset     =  4,
    kFdtErrBadPath       =  5,
    kFdtErrBadPhandle    =  6,
    kFdtErrBadState      =  7,
    kFdtErrTruncated     =  8,
    kFdtErrBadMagic      =  9,
    kFdtErrBadVersion    = 10,
    kFdtErrBadStructure  = 11,
    kFdtErrBadLayout     = 12,
    kFdtErrInternal      = 13,
    kFdtErrBadNCells     = 14,
    kFdtErrBadValue      = 15,
    kFdtErrBadOverlay    = 16,
    kFdtErrNoPhandles    = 17,
    kFdtErrBadFlags      = 18,
    kFdtErrAlignment     = 19,
    kFdtErrMax           = 19,
};

// Property descriptor returned by property iteration

struct FdtPropDesc {
    const char* name;
    const void* data;
    int         len;
    int         offset;
};

// FdtSw::Create flags

constexpr uint32_t kFdtCreateFlagNoNameDedup = 0x1;

// Implementation access (defined in libfdt.cpp)

struct FdtAccess;

// Forward declarations

class FdtRw;
class FdtSw;


//
// Fdt - read-only, non-owning view over an FDT blob
//
class Fdt {
public:
    explicit Fdt(const void* blob)
        : blob_(const_cast<void*>(blob)) {}

    const void* Data() const { return blob_; }
    void*       Data()       { return blob_; }

    // Header accessors
    uint32_t Magic() const;
    uint32_t TotalSize() const;
    uint32_t OffDtStruct() const;
    uint32_t OffDtStrings() const;
    uint32_t OffMemRsvmap() const;
    uint32_t Version() const;
    uint32_t LastCompVer() const;
    uint32_t BootCpuidPhys() const;
    uint32_t SizeDtStrings() const;
    uint32_t SizeDtStruct() const;

    // Validation
    int CheckHeader() const;
    int CheckFull(size_t bufsize) const;
    static size_t HeaderSize(uint32_t version);
    size_t HeaderSize() const;

    // Node navigation
    int NextNode(int offset, int* depth) const;
    int FirstSubnode(int offset) const;
    int NextSubnode(int offset) const;

    // Node lookup
    int PathOffset(const char* path) const;
    int PathOffsetNamelen(const char* path, int namelen) const;
    int SubnodeOffset(int parentoffset, const char* name) const;
    int SubnodeOffsetNamelen(int parentoffset, const char* name,
                             int namelen) const;
    const char* GetName(int nodeoffset, int* lenp = nullptr) const;
    int GetPath(int nodeoffset, char* buf, int buflen) const;

    // Hierarchy
    int ParentOffset(int nodeoffset) const;
    int NodeDepth(int nodeoffset) const;
    int SupernodeAtDepthOffset(int nodeoffset, int supernodedepth,
                               int* nodedepth = nullptr) const;

    // Properties
    const void* GetProp(int nodeoffset, const char* name,
                        int* lenp = nullptr) const;
    const void* GetPropNamelen(int nodeoffset, const char* name,
                               int namelen, int* lenp = nullptr) const;
    const void* GetPropByOffset(int offset, const char** namep = nullptr,
                                int* lenp = nullptr) const;
    int FirstPropertyOffset(int nodeoffset) const;
    int NextPropertyOffset(int offset) const;
    uint32_t GetPhandle(int nodeoffset) const;

    // Search
    int NodeOffsetByPropValue(int startoffset, const char* propname,
                              const void* propval, int proplen) const;
    int NodeOffsetByPhandle(uint32_t phandle) const;
    int NodeCheckCompatible(int nodeoffset, const char* compatible) const;
    int NodeOffsetByCompatible(int startoffset,
                               const char* compatible) const;

    // Aliases / symbols
    const char* GetAlias(const char* name) const;
    const char* GetAliasNamelen(const char* name, int namelen) const;
    const char* GetSymbol(const char* name) const;
    const char* GetSymbolNamelen(const char* name, int namelen) const;

    // Memory reservations
    int NumMemRsv() const;
    int GetMemRsv(int n, uint64_t* address, uint64_t* size) const;

    // Phandle generation
    int FindMaxPhandle(uint32_t* phandle) const;
    int GeneratePhandle(uint32_t* phandle) const;

    // String lists
    static int StringlistContains(const char* strlist, int listlen,
                                  const char* str);
    int StringlistCount(int nodeoffset, const char* property) const;
    int StringlistSearch(int nodeoffset, const char* property,
                         const char* string) const;
    const char* StringlistGet(int nodeoffset, const char* property,
                              int idx, int* lenp = nullptr) const;

    // Address / size cells
    int AddressCells(int nodeoffset) const;
    int SizeCells(int nodeoffset) const;

    // Copy blob to another buffer
    int Move(void* buf, int bufsize) const;

    // Error code to string
    static const char* StrError(int errval);

    // --- Tree traversal (range-based for) ---

    class NodeIterator {
    public:
        struct Value { int offset; int depth; };
        NodeIterator() : fdt_(nullptr), val_{-1, -1} {}
        NodeIterator(const Fdt* f, int o, int d) : fdt_(f), val_{o, d} {}
        const Value& operator*() const  { return val_; }
        const Value* operator->() const { return &val_; }
        NodeIterator& operator++();
        bool operator==(const NodeIterator& o) const { return val_.offset == o.val_.offset; }
        bool operator!=(const NodeIterator& o) const { return !(*this == o); }
    private:
        const Fdt* fdt_;
        Value val_;
    };

    class NodeRange {
    public:
        explicit NodeRange(const Fdt* f) : fdt_(f) {}
        NodeIterator begin() const { return {fdt_, 0, 0}; }
        NodeIterator end() const   { return {}; }
    private:
        const Fdt* fdt_;
    };

    NodeRange Nodes() const { return NodeRange(this); }

    class ChildIterator {
    public:
        ChildIterator() : fdt_(nullptr), offset_(-1) {}
        ChildIterator(const Fdt* f, int o) : fdt_(f), offset_(o) {}
        int operator*() const { return offset_; }
        ChildIterator& operator++();
        bool operator==(const ChildIterator& o) const { return offset_ == o.offset_; }
        bool operator!=(const ChildIterator& o) const { return !(*this == o); }
    private:
        const Fdt* fdt_;
        int offset_;
    };

    class ChildRange {
    public:
        ChildRange(const Fdt* f, int p) : fdt_(f), parent_(p) {}
        ChildIterator begin() const;
        ChildIterator end() const { return {}; }
    private:
        const Fdt* fdt_;
        int parent_;
    };

    ChildRange Children(int nodeoffset) const { return {this, nodeoffset}; }

    class PropertyIterator {
    public:
        PropertyIterator() : fdt_(nullptr), offset_(-1) {}
        PropertyIterator(const Fdt* f, int o) : fdt_(f), offset_(o) {}
        FdtPropDesc operator*() const;
        PropertyIterator& operator++();
        bool operator==(const PropertyIterator& o) const { return offset_ == o.offset_; }
        bool operator!=(const PropertyIterator& o) const { return !(*this == o); }
    private:
        const Fdt* fdt_;
        int offset_;
    };

    class PropertyRange {
    public:
        PropertyRange(const Fdt* f, int n) : fdt_(f), node_(n) {}
        PropertyIterator begin() const;
        PropertyIterator end() const { return {}; }
    private:
        const Fdt* fdt_;
        int node_;
    };

    PropertyRange Properties(int nodeoffset) const { return {this, nodeoffset}; }

    // Callback-based traversal
    using WalkCallback = bool (*)(void* ctx, int offset, int depth,
                                  const char* name);
    int Walk(WalkCallback cb, void* ctx) const;

    using NodePredicate = bool (*)(void* ctx, const Fdt& fdt,
                                   int offset, int depth);
    int FindNode(NodePredicate pred, void* ctx) const;

private:
    friend class FdtRw;
    friend class FdtSw;
    friend struct FdtAccess;
    void* blob_;
};


//
// FdtRw - read-write operations on an existing FDT blob
//
class FdtRw : public Fdt {
public:
    explicit FdtRw(void* blob) : Fdt(blob) {}

    // Write-in-place (same-size property replacement)
    int SetPropInplace(int nodeoffset, const char* name,
                       const void* val, int len);
    int SetPropInplaceNamelenPartial(int nodeoffset, const char* name,
                                     int namelen, uint32_t idx,
                                     const void* val, int len);
    int SetPropInplaceU32(int nodeoffset, const char* name, uint32_t val);
    int SetPropInplaceU64(int nodeoffset, const char* name, uint64_t val);
    int NopProperty(int nodeoffset, const char* name);
    int NopNode(int nodeoffset);

    // Read-write (may resize blob within buffer)
    int OpenInto(const void* fdt, int bufsize);
    int Pack();
    int SetName(int nodeoffset, const char* name);
    int SetProp(int nodeoffset, const char* name,
                const void* val, int len);
    int SetPropNamelen(int nodeoffset, const char* name, int namelen,
                       const void* val, int len);
    int SetPropPlaceholder(int nodeoffset, const char* name, int len,
                           void** prop_data);
    int SetPropPlaceholderNamelen(int nodeoffset, const char* name,
                                 int namelen, int len, void** prop_data);
    int SetPropU32(int nodeoffset, const char* name, uint32_t val);
    int SetPropU64(int nodeoffset, const char* name, uint64_t val);
    int SetPropString(int nodeoffset, const char* name, const char* str);
    int AppendProp(int nodeoffset, const char* name,
                   const void* val, int len);
    int AppendPropU32(int nodeoffset, const char* name, uint32_t val);
    int AppendPropU64(int nodeoffset, const char* name, uint64_t val);
    int AppendPropAddrRange(int parent, int nodeoffset, const char* name,
                            uint64_t addr, uint64_t size);
    int AppendPropString(int nodeoffset, const char* name, const char* str);
    int DelProp(int nodeoffset, const char* name);
    int AddSubnode(int parentoffset, const char* name);
    int AddSubnodeNamelen(int parentoffset, const char* name, int namelen);
    int DelNode(int nodeoffset);
    int AddMemRsv(uint64_t address, uint64_t size);
    int DelMemRsv(int n);
    int SetPropEmpty(int nodeoffset, const char* name);

    // Overlay
    int OverlayApply(void* fdto);

    // Graft subtree from another FDT
    int Graft(const Fdt& src, int src_node, int dst_parent,
              const char* name = nullptr);

private:
    friend struct FdtAccess;
};


//
// FdtSw - sequential-write builder (create FDT from scratch)
//
class FdtSw {
public:
    explicit FdtSw(void* buf) : buf_(buf) {}

    int Create(int bufsize, uint32_t flags = 0);
    int Resize(void* newbuf, int bufsize);
    int AddReservemapEntry(uint64_t addr, uint64_t size);
    int FinishReservemap();
    int BeginNode(const char* name);
    int EndNode();
    int Property(const char* name, const void* val, int len);
    int PropertyU32(const char* name, uint32_t val);
    int PropertyU64(const char* name, uint64_t val);
    int PropertyString(const char* name, const char* str);
    int PropertyPlaceholder(const char* name, int len, void** valp);
    int Finish();

    void* Data() { return buf_; }

private:
    friend struct FdtAccess;
    void* buf_;
};


// Create a minimal valid empty FDT in buf.
int FdtCreateEmptyTree(void* buf, int bufsize);

// Resolve overlay fragment target in base tree (by phandle or path).
int FdtOverlayTargetOffset(const Fdt& fdt, const Fdt& fdto,
                           int fragment_offset,
                           const char** pathp = nullptr);

#endif // __ASSEMBLER__

#endif // LIBFDT_HPP
