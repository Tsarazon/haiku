// SPDX-License-Identifier: (GPL-2.0-or-later OR BSD-2-Clause)
// libfdt++ implementation

#include "libfdt_internal.hpp"

using namespace fdt_detail;
using A = FdtAccess;


// FdtAccess: header and pointer helpers


uint32_t A::GetHeader(const Fdt& f, uint32_t FdtHeader::*field)
{
	return Load32(&(static_cast<const FdtHeader*>(f.blob_)->*field));
}

void A::SetHeader(Fdt& f, uint32_t FdtHeader::*field, uint32_t val)
{
	Store32(&(static_cast<FdtHeader*>(f.blob_)->*field), val);
}

const void* A::OffsetPtr(const Fdt& f, int offset)
{
	return static_cast<const char*>(f.blob_) + f.OffDtStruct() + offset;
}

void* A::OffsetPtrW(Fdt& f, int offset)
{
	return static_cast<char*>(f.blob_) + f.OffDtStruct() + offset;
}

const FdtReserveEntry* A::MemRsv(const Fdt& f, int n)
{
	auto base = static_cast<const char*>(f.blob_) + f.OffMemRsvmap();
	return reinterpret_cast<const FdtReserveEntry*>(base) + n;
}

FdtReserveEntry* A::MemRsvW(Fdt& f, int n)
{
	auto base = static_cast<char*>(f.blob_) + f.OffMemRsvmap();
	return reinterpret_cast<FdtReserveEntry*>(base) + n;
}

int32_t A::RoProbe(const Fdt& f)
{
	uint32_t totalsize = f.TotalSize();

	if (reinterpret_cast<uintptr_t>(f.blob_) & 7)
		return -kFdtErrAlignment;

	if (f.Magic() == kFdtMagic) {
		if (f.Version() < kFdtFirstSupportedVersion)
			return -kFdtErrBadVersion;
		if (f.LastCompVer() > kFdtLastSupportedVersion)
			return -kFdtErrBadVersion;
	} else if (f.Magic() == kFdtSwMagic) {
		if (f.SizeDtStruct() == 0)
			return -kFdtErrBadState;
	} else {
		return -kFdtErrBadMagic;
	}

	return totalsize < INT32_MAX ? int32_t(totalsize) : -kFdtErrTruncated;
}

const void* A::OffsetPtrChecked(const Fdt& f, int offset, unsigned len)
{
	unsigned uo = offset;
	unsigned ao = offset + f.OffDtStruct();

	if (offset < 0 || ao < uo || (ao + len) < ao
		|| (ao + len) > f.TotalSize())
		return nullptr;

	if (f.Version() >= 0x11
		&& ((uo + len) < uo || (uint32_t(offset) + len) > f.SizeDtStruct()))
		return nullptr;

	return OffsetPtr(f, offset);
}

uint32_t A::NextTag(const Fdt& f, int startoffset, int* nextoffset)
{
	int offset = startoffset;
	*nextoffset = -kFdtErrTruncated;

	auto tagp = static_cast<const uint32_t*>(
		OffsetPtrChecked(f, offset, kFdtTagSize));
	if (!tagp)
		return kFdtEnd;

	uint32_t tag = Load32(tagp);
	offset += kFdtTagSize;
	*nextoffset = -kFdtErrBadStructure;

	switch (tag) {
	case kFdtBeginNode: {
		const char* p;
		do {
			p = static_cast<const char*>(
				OffsetPtrChecked(f, offset++, 1));
		} while (p && *p != '\0');
		if (!p)
			return kFdtEnd;
		break;
	}
	case kFdtProp: {
		auto lp = static_cast<const uint32_t*>(
			OffsetPtrChecked(f, offset, sizeof(uint32_t)));
		if (!lp)
			return kFdtEnd;
		uint32_t len = Load32(lp);
		uint32_t sum = len + offset;
		if (INT_MAX <= sum || sum < uint32_t(offset))
			return kFdtEnd;
		offset += sizeof(FdtProperty) - kFdtTagSize + len;
		break;
	}
	case kFdtEnd: case kFdtEndNode: case kFdtNop:
		break;
	default:
		return kFdtEnd;
	}

	if (!OffsetPtrChecked(f, startoffset, offset - startoffset))
		return kFdtEnd;

	*nextoffset = TagAlign(offset);
	return tag;
}

int A::CheckNodeOffset(const Fdt& f, int offset)
{
	if (offset < 0 || (offset % kFdtTagSize))
		return -kFdtErrBadOffset;
	if (NextTag(f, offset, &offset) != kFdtBeginNode)
		return -kFdtErrBadOffset;
	return offset;
}

int A::CheckPropOffset(const Fdt& f, int offset)
{
	if (offset < 0 || (offset % kFdtTagSize))
		return -kFdtErrBadOffset;
	if (NextTag(f, offset, &offset) != kFdtProp)
		return -kFdtErrBadOffset;
	return offset;
}


// FdtAccess: string helpers


const char* A::GetString(const Fdt& f, int stroffset, int* lenp)
{
	int32_t totalsize = RoProbe(f);
	if (totalsize < 0) {
		if (lenp) *lenp = totalsize;
		return nullptr;
	}

	uint32_t absoffset = stroffset + f.OffDtStrings();
	if (absoffset >= uint32_t(totalsize)) {
		if (lenp) *lenp = -kFdtErrBadOffset;
		return nullptr;
	}

	size_t len = totalsize - absoffset;

	if (f.Magic() == kFdtMagic) {
		if (stroffset < 0) {
			if (lenp) *lenp = -kFdtErrBadOffset;
			return nullptr;
		}
		if (f.Version() >= 17) {
			if (uint32_t(stroffset) >= f.SizeDtStrings()) {
				if (lenp) *lenp = -kFdtErrBadOffset;
				return nullptr;
			}
			if (f.SizeDtStrings() - stroffset < len)
				len = f.SizeDtStrings() - stroffset;
		}
	} else if (f.Magic() == kFdtSwMagic) {
		unsigned sw = -stroffset;
		if (stroffset >= 0 || sw > f.SizeDtStrings()) {
			if (lenp) *lenp = -kFdtErrBadOffset;
			return nullptr;
		}
		if (sw < len) len = sw;
	} else {
		if (lenp) *lenp = -kFdtErrInternal;
		return nullptr;
	}

	auto s = static_cast<const char*>(f.blob_) + absoffset;
	auto n = static_cast<const char*>(memchr(s, '\0', len));
	if (!n) {
		if (lenp) *lenp = -kFdtErrTruncated;
		return nullptr;
	}
	if (lenp) *lenp = n - s;
	return s;
}

bool A::StringEq(const Fdt& f, int stroffset, const char* s, int len)
{
	int sl;
	auto p = GetString(f, stroffset, &sl);
	return p && sl == len && memcmp(p, s, len) == 0;
}

bool A::NodeNameEq(const Fdt& f, int offset, const char* s, int len)
{
	int olen;
	auto p = f.GetName(offset, &olen);
	if (!p || olen < len || memcmp(p, s, len) != 0)
		return false;
	return p[len] == '\0' || (!memchr(s, '@', len) && p[len] == '@');
}


// FdtAccess: property internals


const FdtProperty* A::GetPropertyByOffset_(const Fdt& f, int offset,
	int* lenp)
{
	int err = CheckPropOffset(f, offset);
	if (err < 0) {
		if (lenp) *lenp = err;
		return nullptr;
	}
	auto prop = static_cast<const FdtProperty*>(OffsetPtr(f, offset));
	if (lenp) *lenp = Load32(&prop->len);
	return prop;
}

const FdtProperty* A::GetPropertyNamelen_(const Fdt& f, int offset,
	const char* name, int namelen, int* lenp, int* poffset)
{
	for (offset = f.FirstPropertyOffset(offset);
		 offset >= 0;
		 offset = f.NextPropertyOffset(offset)) {
		auto prop = GetPropertyByOffset_(f, offset, lenp);
		if (!prop) {
			offset = -kFdtErrInternal;
			break;
		}
		if (StringEq(f, Load32(&prop->nameoff), name, namelen)) {
			if (poffset) *poffset = offset;
			return prop;
		}
	}
	if (lenp) *lenp = offset;
	return nullptr;
}

const void* A::PathGetPropNamelen(const Fdt& f, const char* path,
	const char* propname, int propnamelen, int* lenp)
{
	int offset = f.PathOffset(path);
	return offset < 0 ? nullptr
		: f.GetPropNamelen(offset, propname, propnamelen, lenp);
}


// Fdt: header accessors


uint32_t Fdt::Magic() const         { return A::GetHeader(*this, &FdtHeader::magic); }
uint32_t Fdt::TotalSize() const     { return A::GetHeader(*this, &FdtHeader::totalsize); }
uint32_t Fdt::OffDtStruct() const   { return A::GetHeader(*this, &FdtHeader::off_dt_struct); }
uint32_t Fdt::OffDtStrings() const  { return A::GetHeader(*this, &FdtHeader::off_dt_strings); }
uint32_t Fdt::OffMemRsvmap() const  { return A::GetHeader(*this, &FdtHeader::off_mem_rsvmap); }
uint32_t Fdt::Version() const       { return A::GetHeader(*this, &FdtHeader::version); }
uint32_t Fdt::LastCompVer() const   { return A::GetHeader(*this, &FdtHeader::last_comp_version); }
uint32_t Fdt::BootCpuidPhys() const { return A::GetHeader(*this, &FdtHeader::boot_cpuid_phys); }
uint32_t Fdt::SizeDtStrings() const { return A::GetHeader(*this, &FdtHeader::size_dt_strings); }
uint32_t Fdt::SizeDtStruct() const  { return A::GetHeader(*this, &FdtHeader::size_dt_struct); }


// Fdt: validation


size_t Fdt::HeaderSize(uint32_t version)
{
	if (version <= 1)  return kFdtV1Size;
	if (version <= 2)  return kFdtV2Size;
	if (version <= 3)  return kFdtV3Size;
	if (version <= 16) return kFdtV16Size;
	return kFdtV17Size;
}

size_t Fdt::HeaderSize() const { return HeaderSize(Version()); }

int Fdt::CheckHeader() const
{
	if (reinterpret_cast<uintptr_t>(blob_) & 7)
		return -kFdtErrAlignment;
	if (Magic() != kFdtMagic)
		return -kFdtErrBadMagic;
	if (Version() < kFdtFirstSupportedVersion
		|| LastCompVer() > kFdtLastSupportedVersion)
		return -kFdtErrBadVersion;
	if (Version() < LastCompVer())
		return -kFdtErrBadVersion;

	size_t hs = HeaderSize();
	if (TotalSize() < hs || TotalSize() > INT_MAX)
		return -kFdtErrTruncated;
	if (OffMemRsvmap() % sizeof(uint64_t))
		return -kFdtErrAlignment;
	if (OffDtStruct() % kFdtTagSize)
		return -kFdtErrAlignment;

	auto ckOff = [](uint32_t h, uint32_t t, uint32_t o) {
		return o >= h && o <= t;
	};
	auto ckBlk = [&](uint32_t h, uint32_t t, uint32_t b, uint32_t sz) {
		return ckOff(h, t, b) && b + sz >= b && ckOff(h, t, b + sz);
	};

	if (!ckOff(hs, TotalSize(), OffMemRsvmap()))
		return -kFdtErrTruncated;
	if (Version() < 17) {
		if (!ckOff(hs, TotalSize(), OffDtStruct()))
			return -kFdtErrTruncated;
	} else {
		if (!ckBlk(hs, TotalSize(), OffDtStruct(), SizeDtStruct()))
			return -kFdtErrTruncated;
	}
	if (!ckBlk(hs, TotalSize(), OffDtStrings(), SizeDtStrings()))
		return -kFdtErrTruncated;

	return 0;
}

int Fdt::CheckFull(size_t bufsize) const
{
	if (bufsize < kFdtV1Size || bufsize < HeaderSize())
		return -kFdtErrTruncated;
	int err = CheckHeader();
	if (err) return err;
	if (bufsize < TotalSize())
		return -kFdtErrTruncated;
	int num = NumMemRsv();
	if (num < 0) return num;

	int nextoffset = 0;
	unsigned depth = 0;
	bool expectEnd = false;

	while (true) {
		int offset = nextoffset;
		uint32_t tag = A::NextTag(*this, offset, &nextoffset);
		if (nextoffset < 0) return nextoffset;
		if (expectEnd && tag != kFdtEnd && tag != kFdtNop)
			return -kFdtErrBadStructure;

		switch (tag) {
		case kFdtNop: break;
		case kFdtEnd: return depth ? -kFdtErrBadStructure : 0;
		case kFdtBeginNode:
			depth++;
			if (depth == 1) {
				int l; auto n = GetName(offset, &l);
				if (!n) return l;
				if (*n || l) return -kFdtErrBadStructure;
			}
			break;
		case kFdtEndNode:
			if (!depth) return -kFdtErrBadStructure;
			if (--depth == 0) expectEnd = true;
			break;
		case kFdtProp: {
			const char* pn; int pl;
			if (!GetPropByOffset(offset, &pn, &pl)) return pl;
			break;
		}
		default: return -kFdtErrInternal;
		}
	}
}


// Fdt: node navigation


int Fdt::NextNode(int offset, int* depth) const
{
	int nextoffset = 0;
	uint32_t tag;

	if (offset >= 0) {
		nextoffset = A::CheckNodeOffset(*this, offset);
		if (nextoffset < 0) return nextoffset;
	}

	do {
		offset = nextoffset;
		tag = A::NextTag(*this, offset, &nextoffset);
		switch (tag) {
		case kFdtProp: case kFdtNop: break;
		case kFdtBeginNode: if (depth) (*depth)++; break;
		case kFdtEndNode:
			if (depth && ((--(*depth)) < 0)) return nextoffset;
			break;
		case kFdtEnd:
			return (nextoffset >= 0
				|| (nextoffset == -kFdtErrTruncated && !depth))
				? -kFdtErrNotFound : nextoffset;
		}
	} while (tag != kFdtBeginNode);

	return offset;
}

int Fdt::FirstSubnode(int offset) const
{
	int d = 0;
	offset = NextNode(offset, &d);
	return (offset < 0 || d != 1) ? -kFdtErrNotFound : offset;
}

int Fdt::NextSubnode(int offset) const
{
	int d = 1;
	do {
		offset = NextNode(offset, &d);
		if (offset < 0 || d < 1) return -kFdtErrNotFound;
	} while (d > 1);
	return offset;
}


// Fdt: node lookup


int Fdt::SubnodeOffsetNamelen(int po, const char* name, int nl) const
{
	int32_t probe = A::RoProbe(*this);
	if (probe < 0) return probe;
	int depth = 0;
	for (int o = po; o >= 0 && depth >= 0; o = NextNode(o, &depth))
		if (depth == 1 && A::NodeNameEq(*this, o, name, nl))
			return o;
	return -kFdtErrNotFound;
}

int Fdt::SubnodeOffset(int po, const char* name) const
{
	return SubnodeOffsetNamelen(po, name, strlen(name));
}

int Fdt::PathOffsetNamelen(const char* path, int namelen) const
{
	const char* end = path + namelen;
	const char* p = path;
	int offset = 0;

	int32_t probe = A::RoProbe(*this);
	if (probe < 0) return probe;
	if (namelen <= 0) return -kFdtErrBadPath;

	if (*path != '/') {
		auto q = static_cast<const char*>(memchr(path, '/', end - p));
		if (!q) q = end;
		p = GetAliasNamelen(p, q - p);
		if (!p) return -kFdtErrBadPath;
		offset = PathOffset(p);
		p = q;
	}

	while (p < end) {
		while (*p == '/') { p++; if (p == end) return offset; }
		auto q = static_cast<const char*>(memchr(p, '/', end - p));
		if (!q) q = end;
		offset = SubnodeOffsetNamelen(offset, p, q - p);
		if (offset < 0) return offset;
		p = q;
	}
	return offset;
}

int Fdt::PathOffset(const char* path) const
{
	return PathOffsetNamelen(path, strlen(path));
}

const char* Fdt::GetName(int nodeoffset, int* lenp) const
{
	int32_t probe = A::RoProbe(*this);
	if (probe < 0) { if (lenp) *lenp = probe; return nullptr; }
	int err = A::CheckNodeOffset(*this, nodeoffset);
	if (err < 0) { if (lenp) *lenp = err; return nullptr; }
	auto nh = static_cast<const FdtNodeHeader*>(
		A::OffsetPtr(*this, nodeoffset));
	if (lenp) *lenp = strlen(nh->name);
	return nh->name;
}

int Fdt::GetPath(int nodeoffset, char* buf, int buflen) const
{
	int32_t probe = A::RoProbe(*this);
	if (probe < 0) return probe;
	if (buflen < 2) return -kFdtErrNoSpace;

	int pdepth = 0, p = 0;
	for (int offset = 0, depth = 0;
		 offset >= 0 && offset <= nodeoffset;
		 offset = NextNode(offset, &depth)) {
		while (pdepth > depth) {
			do { p--; } while (buf[p - 1] != '/');
			pdepth--;
		}
		if (pdepth >= depth) {
			int nl; auto nm = GetName(offset, &nl);
			if (!nm) return nl;
			if (p + nl + 1 <= buflen) {
				memcpy(buf + p, nm, nl);
				p += nl;
				buf[p++] = '/';
				pdepth++;
			}
		}
		if (offset == nodeoffset) {
			if (pdepth < depth + 1) return -kFdtErrNoSpace;
			if (p > 1) p--;
			buf[p] = '\0';
			return 0;
		}
	}
	return -kFdtErrBadOffset;
}


// Fdt: hierarchy


int Fdt::SupernodeAtDepthOffset(int nodeoffset, int supernodedepth,
	int* nodedepth) const
{
	int32_t probe = A::RoProbe(*this);
	if (probe < 0) return probe;
	if (supernodedepth < 0) return -kFdtErrNotFound;

	int sno = -kFdtErrInternal;
	for (int offset = 0, depth = 0;
		 offset >= 0 && offset <= nodeoffset;
		 offset = NextNode(offset, &depth)) {
		if (depth == supernodedepth) sno = offset;
		if (offset == nodeoffset) {
			if (nodedepth) *nodedepth = depth;
			return supernodedepth > depth ? -kFdtErrNotFound : sno;
		}
	}
	return -kFdtErrBadOffset;
}

int Fdt::NodeDepth(int nodeoffset) const
{
	int nd;
	int err = SupernodeAtDepthOffset(nodeoffset, 0, &nd);
	if (err) return (err < 0) ? err : -kFdtErrInternal;
	return nd;
}

int Fdt::ParentOffset(int nodeoffset) const
{
	int nd = NodeDepth(nodeoffset);
	return nd < 0 ? nd : SupernodeAtDepthOffset(nodeoffset, nd - 1, nullptr);
}


// Fdt: properties


int Fdt::FirstPropertyOffset(int nodeoffset) const
{
	int offset = A::CheckNodeOffset(*this, nodeoffset);
	if (offset < 0) return offset;
	int no; uint32_t tag;
	do {
		tag = A::NextTag(*this, offset, &no);
		if (tag == kFdtEnd) return no >= 0 ? -kFdtErrBadStructure : no;
		if (tag == kFdtProp) return offset;
		offset = no;
	} while (tag == kFdtNop);
	return -kFdtErrNotFound;
}

int Fdt::NextPropertyOffset(int offset) const
{
	offset = A::CheckPropOffset(*this, offset);
	if (offset < 0) return offset;
	int no; uint32_t tag;
	do {
		tag = A::NextTag(*this, offset, &no);
		if (tag == kFdtEnd) return no >= 0 ? -kFdtErrBadStructure : no;
		if (tag == kFdtProp) return offset;
		offset = no;
	} while (tag == kFdtNop);
	return -kFdtErrNotFound;
}

const void* Fdt::GetPropNamelen(int no, const char* name, int nl,
	int* lenp) const
{
	int po;
	auto p = A::GetPropertyNamelen_(*this, no, name, nl, lenp, &po);
	return p ? p->data : nullptr;
}

const void* Fdt::GetProp(int no, const char* name, int* lenp) const
{
	return GetPropNamelen(no, name, strlen(name), lenp);
}

const void* Fdt::GetPropByOffset(int offset, const char** namep,
	int* lenp) const
{
	auto prop = A::GetPropertyByOffset_(*this, offset, lenp);
	if (!prop) return nullptr;
	if (namep) {
		int nl;
		auto n = A::GetString(*this, Load32(&prop->nameoff), &nl);
		*namep = n;
		if (!n) { if (lenp) *lenp = nl; return nullptr; }
	}
	return prop->data;
}

uint32_t Fdt::GetPhandle(int no) const
{
	int l;
	auto p = static_cast<const uint32_t*>(GetProp(no, "phandle", &l));
	if (!p || l != int(sizeof(uint32_t))) {
		p = static_cast<const uint32_t*>(
			GetProp(no, "linux,phandle", &l));
		if (!p || l != int(sizeof(uint32_t))) return 0;
	}
	return Load32(p);
}


// Fdt: search


int Fdt::NodeOffsetByPropValue(int so, const char* pn,
	const void* pv, int pl) const
{
	int32_t probe = A::RoProbe(*this);
	if (probe < 0) return probe;
	for (int o = NextNode(so, nullptr); o >= 0; o = NextNode(o, nullptr)) {
		int l; auto v = GetProp(o, pn, &l);
		if (v && l == pl && memcmp(v, pv, l) == 0) return o;
	}
	return -kFdtErrNotFound;
}

int Fdt::NodeOffsetByPhandle(uint32_t ph) const
{
	if (!ph || ph == ~0U) return -kFdtErrBadPhandle;
	int32_t probe = A::RoProbe(*this);
	if (probe < 0) return probe;
	for (int o = NextNode(-1, nullptr); o >= 0; o = NextNode(o, nullptr))
		if (GetPhandle(o) == ph) return o;
	return -kFdtErrNotFound;
}

int Fdt::NodeCheckCompatible(int no, const char* c) const
{
	int l;
	auto p = static_cast<const char*>(GetProp(no, "compatible", &l));
	return p ? !StringlistContains(p, l, c) : l;
}

int Fdt::NodeOffsetByCompatible(int so, const char* c) const
{
	int32_t probe = A::RoProbe(*this);
	if (probe < 0) return probe;
	for (int o = NextNode(so, nullptr); o >= 0; o = NextNode(o, nullptr)) {
		int e = NodeCheckCompatible(o, c);
		if (e < 0 && e != -kFdtErrNotFound) return e;
		if (e == 0) return o;
	}
	return -kFdtErrNotFound;
}


// Fdt: aliases, symbols, mem rsv, phandle, stringlist, cells, misc


const char* Fdt::GetAliasNamelen(const char* name, int nl) const
{
	int l;
	auto a = static_cast<const char*>(
		A::PathGetPropNamelen(*this, "/aliases", name, nl, &l));
	return (a && l > 0 && a[l - 1] == '\0' && *a == '/') ? a : nullptr;
}

const char* Fdt::GetAlias(const char* name) const
{ return GetAliasNamelen(name, strlen(name)); }

const char* Fdt::GetSymbolNamelen(const char* name, int nl) const
{
	return static_cast<const char*>(
		A::PathGetPropNamelen(*this, "/__symbols__", name, nl, nullptr));
}

const char* Fdt::GetSymbol(const char* name) const
{ return GetSymbolNamelen(name, strlen(name)); }

int Fdt::NumMemRsv() const
{
	for (int i = 0; ; i++) {
		auto re = A::MemRsv(*this, i);
		if (!re) return -kFdtErrTruncated;
		if (Load64(&re->size) == 0) return i;
	}
}

int Fdt::GetMemRsv(int n, uint64_t* addr, uint64_t* size) const
{
	int32_t p = A::RoProbe(*this);
	if (p < 0) return p;
	auto re = A::MemRsv(*this, n);
	if (!re) return -kFdtErrBadOffset;
	*addr = Load64(&re->address);
	*size = Load64(&re->size);
	return 0;
}

int Fdt::FindMaxPhandle(uint32_t* ph) const
{
	uint32_t mx = 0;
	for (int o = -1; ;) {
		o = NextNode(o, nullptr);
		if (o < 0) {
			if (o == -kFdtErrNotFound) break;
			return o;
		}
		uint32_t v = GetPhandle(o);
		if (v > mx) mx = v;
	}
	if (ph) *ph = mx;
	return 0;
}

int Fdt::GeneratePhandle(uint32_t* ph) const
{
	uint32_t mx;
	int e = FindMaxPhandle(&mx);
	if (e < 0) return e;
	if (mx == kFdtMaxPhandle) return -kFdtErrNoPhandles;
	if (ph) *ph = mx + 1;
	return 0;
}

int Fdt::StringlistContains(const char* sl, int ll, const char* str)
{
	int l = strlen(str);
	while (ll >= l) {
		if (memcmp(str, sl, l + 1) == 0) return 1;
		auto p = static_cast<const char*>(memchr(sl, '\0', ll));
		if (!p) return 0;
		ll -= (p - sl) + 1;
		sl = p + 1;
	}
	return 0;
}

int Fdt::StringlistCount(int no, const char* prop) const
{
	int length;
	auto list = static_cast<const char*>(GetProp(no, prop, &length));
	if (!list) return length;
	auto end = list + length;
	int cnt = 0;
	while (list < end) {
		int l = Strnlen(list, end - list) + 1;
		if (list + l > end) return -kFdtErrBadValue;
		list += l; cnt++;
	}
	return cnt;
}

int Fdt::StringlistSearch(int no, const char* prop,
	const char* string) const
{
	int length;
	auto list = static_cast<const char*>(GetProp(no, prop, &length));
	if (!list) return length;
	int len = strlen(string) + 1;
	auto end = list + length;
	int idx = 0;
	while (list < end) {
		int l = Strnlen(list, end - list) + 1;
		if (list + l > end) return -kFdtErrBadValue;
		if (l == len && memcmp(list, string, l) == 0) return idx;
		list += l; idx++;
	}
	return -kFdtErrNotFound;
}

const char* Fdt::StringlistGet(int no, const char* prop, int idx,
	int* lenp) const
{
	int length;
	auto list = static_cast<const char*>(GetProp(no, prop, &length));
	if (!list) { if (lenp) *lenp = length; return nullptr; }
	auto end = list + length;
	while (list < end) {
		int l = Strnlen(list, end - list) + 1;
		if (list + l > end) {
			if (lenp) *lenp = -kFdtErrBadValue;
			return nullptr;
		}
		if (idx == 0) { if (lenp) *lenp = l - 1; return list; }
		list += l; idx--;
	}
	if (lenp) *lenp = -kFdtErrNotFound;
	return nullptr;
}

int Fdt::AddressCells(int no) const
{
	int l;
	auto c = static_cast<const uint32_t*>(
		GetProp(no, "#address-cells", &l));
	if (!c) return l == -kFdtErrNotFound ? 2 : l;
	if (l != int(sizeof(uint32_t))) return -kFdtErrBadNCells;
	uint32_t v = Load32(c);
	return (!v || v > kFdtMaxNCells) ? -kFdtErrBadNCells : int(v);
}

int Fdt::SizeCells(int no) const
{
	int l;
	auto c = static_cast<const uint32_t*>(
		GetProp(no, "#size-cells", &l));
	if (!c) return l == -kFdtErrNotFound ? 1 : l;
	if (l != int(sizeof(uint32_t))) return -kFdtErrBadNCells;
	uint32_t v = Load32(c);
	return v > kFdtMaxNCells ? -kFdtErrBadNCells : int(v);
}

int Fdt::Move(void* buf, int bufsize) const
{
	if (bufsize < 0) return -kFdtErrNoSpace;
	int32_t p = A::RoProbe(*this);
	if (p < 0) return p;
	if (TotalSize() > uint32_t(bufsize)) return -kFdtErrNoSpace;
	memmove(buf, blob_, TotalSize());
	return 0;
}

static const char* const kFdtErrorStrings[] = {
	nullptr, "FDT_ERR_NOTFOUND", "FDT_ERR_EXISTS", "FDT_ERR_NOSPACE",
	"FDT_ERR_BADOFFSET", "FDT_ERR_BADPATH", "FDT_ERR_BADPHANDLE",
	"FDT_ERR_BADSTATE", "FDT_ERR_TRUNCATED", "FDT_ERR_BADMAGIC",
	"FDT_ERR_BADVERSION", "FDT_ERR_BADSTRUCTURE", "FDT_ERR_BADLAYOUT",
	"FDT_ERR_INTERNAL", "FDT_ERR_BADNCELLS", "FDT_ERR_BADVALUE",
	"FDT_ERR_BADOVERLAY", "FDT_ERR_NOPHANDLES", "FDT_ERR_BADFLAGS",
	"FDT_ERR_ALIGNMENT",
};

const char* Fdt::StrError(int errval)
{
	if (errval > 0) return "<valid offset/length>";
	if (errval == 0) return "<no error>";
	int i = -errval;
	if (i < int(sizeof(kFdtErrorStrings) / sizeof(kFdtErrorStrings[0]))
		&& kFdtErrorStrings[i])
		return kFdtErrorStrings[i];
	return "<unknown error>";
}


// Fdt: iterators


Fdt::NodeIterator& Fdt::NodeIterator::operator++()
{
	if (fdt_ && val_.offset >= 0) {
		val_.offset = fdt_->NextNode(val_.offset, &val_.depth);
		if (val_.offset < 0 || val_.depth < 0) val_.offset = -1;
	}
	return *this;
}

Fdt::ChildIterator& Fdt::ChildIterator::operator++()
{
	if (fdt_ && offset_ >= 0) offset_ = fdt_->NextSubnode(offset_);
	if (offset_ < 0) offset_ = -1;
	return *this;
}

Fdt::ChildIterator Fdt::ChildRange::begin() const
{
	int o = fdt_->FirstSubnode(parent_);
	return {fdt_, o >= 0 ? o : -1};
}

FdtPropDesc Fdt::PropertyIterator::operator*() const
{
	FdtPropDesc d{};
	if (fdt_ && offset_ >= 0) {
		d.offset = offset_;
		d.data = fdt_->GetPropByOffset(offset_, &d.name, &d.len);
	}
	return d;
}

Fdt::PropertyIterator& Fdt::PropertyIterator::operator++()
{
	if (fdt_ && offset_ >= 0)
		offset_ = fdt_->NextPropertyOffset(offset_);
	if (offset_ < 0) offset_ = -1;
	return *this;
}

Fdt::PropertyIterator Fdt::PropertyRange::begin() const
{
	int o = fdt_->FirstPropertyOffset(node_);
	return {fdt_, o >= 0 ? o : -1};
}

int Fdt::Walk(WalkCallback cb, void* ctx) const
{
	for (auto [offset, depth] : Nodes()) {
		int nl; auto n = GetName(offset, &nl);
		if (!n) return nl;
		if (!cb(ctx, offset, depth, n)) return 0;
	}
	return 0;
}

int Fdt::FindNode(NodePredicate pred, void* ctx) const
{
	for (auto [offset, depth] : Nodes())
		if (pred(ctx, *this, offset, depth)) return offset;
	return -kFdtErrNotFound;
}


// FdtAccess: FdtRw internals


int A::RwProbe(FdtRw& rw)
{
	int32_t p = RoProbe(rw);
	if (p < 0) return p;
	if (rw.Version() < 17) return -kFdtErrBadVersion;
	if (BlocksMisordered(rw, sizeof(FdtReserveEntry), rw.SizeDtStruct()))
		return -kFdtErrBadLayout;
	if (rw.Version() > 17) SetHeader(rw, &FdtHeader::version, 17);
	return 0;
}

bool A::BlocksMisordered(FdtRw& rw, int mrs, int ss)
{
	return rw.OffMemRsvmap() < Align(sizeof(FdtHeader), 8)
		|| rw.OffDtStruct() < rw.OffMemRsvmap() + mrs
		|| rw.OffDtStrings() < rw.OffDtStruct() + ss
		|| rw.TotalSize() < rw.OffDtStrings() + rw.SizeDtStrings();
}

unsigned A::DataSize(FdtRw& rw)
{ return rw.OffDtStrings() + rw.SizeDtStrings(); }

int A::Splice(FdtRw& rw, void* sp, int ol, int nl)
{
	auto p = static_cast<char*>(sp);
	unsigned ds = DataSize(rw);
	size_t so = p - static_cast<char*>(rw.blob_);

	if (ol < 0 || so + ol < so || so + ol > ds)
		return -kFdtErrBadOffset;
	if (p < static_cast<char*>(rw.blob_) || ds + nl < unsigned(ol))
		return -kFdtErrBadOffset;
	if (ds - ol + nl > rw.TotalSize())
		return -kFdtErrNoSpace;
	memmove(p + nl, p + ol,
		(static_cast<char*>(rw.blob_) + ds) - (p + ol));
	return 0;
}

int A::SpliceMemRsv(FdtRw& rw, FdtReserveEntry* p, int on, int nn)
{
	int d = (nn - on) * sizeof(*p);
	int e = Splice(rw, p, on * sizeof(*p), nn * sizeof(*p));
	if (e) return e;
	SetHeader(rw, &FdtHeader::off_dt_struct, rw.OffDtStruct() + d);
	SetHeader(rw, &FdtHeader::off_dt_strings, rw.OffDtStrings() + d);
	return 0;
}

int A::SpliceStruct(FdtRw& rw, void* p, int ol, int nl)
{
	int d = nl - ol;
	int e = Splice(rw, p, ol, nl);
	if (e) return e;
	SetHeader(rw, &FdtHeader::size_dt_struct, rw.SizeDtStruct() + d);
	SetHeader(rw, &FdtHeader::off_dt_strings, rw.OffDtStrings() + d);
	return 0;
}

int A::SpliceString(FdtRw& rw, int nl)
{
	auto p = static_cast<char*>(rw.blob_)
		+ rw.OffDtStrings() + rw.SizeDtStrings();
	int e = Splice(rw, p, 0, nl);
	if (e) return e;
	SetHeader(rw, &FdtHeader::size_dt_strings, rw.SizeDtStrings() + nl);
	return 0;
}

void A::DelLastString(FdtRw& rw, const char* s)
{
	SetHeader(rw, &FdtHeader::size_dt_strings,
		rw.SizeDtStrings() - strlen(s) - 1);
}

int A::FindAddString(FdtRw& rw, const char* s, int sl, int* alloc)
{
	auto st = static_cast<char*>(rw.blob_) + rw.OffDtStrings();
	*alloc = 0;
	auto p = FindStringLen(st, rw.SizeDtStrings(), s, sl);
	if (p) return p - st;
	auto np = st + rw.SizeDtStrings();
	int e = SpliceString(rw, sl + 1);
	if (e) return e;
	*alloc = 1;
	memcpy(np, s, sl);
	np[sl] = '\0';
	return np - st;
}

int A::ResizeProperty(FdtRw& rw, int no, const char* name, int nl,
	int len, FdtProperty** prop)
{
	int ol, po;
	if (!GetPropertyNamelen_(rw, no, name, nl, &ol, &po)) return ol;
	*prop = static_cast<FdtProperty*>(OffsetPtrW(rw, po));
	int e = SpliceStruct(rw, (*prop)->data, TagAlign(ol), TagAlign(len));
	if (e) return e;
	Store32(&(*prop)->len, len);
	return 0;
}

int A::AddProperty(FdtRw& rw, int no, const char* name, int nl,
	int len, FdtProperty** prop)
{
	int nxo = CheckNodeOffset(rw, no);
	if (nxo < 0) return nxo;
	int alloc, nso = FindAddString(rw, name, nl, &alloc);
	if (nso < 0) return nso;
	*prop = static_cast<FdtProperty*>(OffsetPtrW(rw, nxo));
	int pl = sizeof(FdtProperty) + TagAlign(len);
	int e = SpliceStruct(rw, *prop, 0, pl);
	if (e) { if (alloc) DelLastString(rw, name); return e; }
	Store32(&(*prop)->tag, kFdtProp);
	Store32(&(*prop)->nameoff, nso);
	Store32(&(*prop)->len, len);
	return 0;
}

void A::NopRegion(void* start, int len)
{
	for (auto p = static_cast<uint32_t*>(start);
		 reinterpret_cast<char*>(p) < static_cast<char*>(start) + len;
		 p++)
		Store32(p, kFdtNop);
}

int A::NodeEndOffset(FdtRw& rw, int offset)
{
	int d = 0;
	while (offset >= 0 && d >= 0) offset = rw.NextNode(offset, &d);
	return offset;
}

void A::PackBlocks(const char* old, char* dst, int mrs, int ss, int sts)
{
	Fdt of(old);
	int mro = Align(sizeof(FdtHeader), 8), so = mro + mrs, sto = so + ss;
	memmove(dst + mro, old + of.OffMemRsvmap(), mrs);
	Fdt d(dst);
	SetHeader(d, &FdtHeader::off_mem_rsvmap, mro);
	memmove(dst + so, old + of.OffDtStruct(), ss);
	SetHeader(d, &FdtHeader::off_dt_struct, so);
	SetHeader(d, &FdtHeader::size_dt_struct, ss);
	memmove(dst + sto, old + of.OffDtStrings(), sts);
	SetHeader(d, &FdtHeader::off_dt_strings, sto);
	SetHeader(d, &FdtHeader::size_dt_strings, of.SizeDtStrings());
}

int A::GraftNode(FdtRw& rw, const Fdt& src, int sn, int dp,
	const char* name)
{
	int dn = rw.AddSubnode(dp, name);
	if (dn < 0) return dn;

	for (auto p : src.Properties(sn)) {
		if (!p.data && p.len < 0) return p.len;
		int e = rw.SetProp(dn, p.name, p.data, p.len);
		if (e) return e;
		dn = rw.SubnodeOffset(dp, name);
		if (dn < 0) return dn;
	}
	for (int ch : src.Children(sn)) {
		int nl; auto cn = src.GetName(ch, &nl);
		if (!cn) return nl;
		int r = GraftNode(rw, src, ch, dn, cn);
		if (r < 0) return r;
		dn = rw.SubnodeOffset(dp, name);
		if (dn < 0) return dn;
	}
	return dn;
}


// FdtRw: write-in-place


int FdtRw::SetPropInplaceNamelenPartial(int no, const char* name,
	int nl, uint32_t idx, const void* val, int len)
{
	int pl;
	void* pv = const_cast<void*>(GetPropNamelen(no, name, nl, &pl));
	if (!pv) return pl;
	if (unsigned(pl) < unsigned(len + idx)) return -kFdtErrNoSpace;
	memcpy(static_cast<char*>(pv) + idx, val, len);
	return 0;
}

int FdtRw::SetPropInplace(int no, const char* name,
	const void* val, int len)
{
	int pl;
	if (!GetProp(no, name, &pl)) return pl;
	if (pl != len) return -kFdtErrNoSpace;
	return SetPropInplaceNamelenPartial(no, name, strlen(name), 0, val, len);
}

int FdtRw::SetPropInplaceU32(int no, const char* name, uint32_t val)
{ uint8_t t[4]; Store32(t, val); return SetPropInplace(no, name, t, 4); }

int FdtRw::SetPropInplaceU64(int no, const char* name, uint64_t val)
{ uint8_t t[8]; Store64(t, val); return SetPropInplace(no, name, t, 8); }

int FdtRw::NopProperty(int no, const char* name)
{
	int len, po;
	auto prop = A::GetPropertyNamelen_(
		*this, no, name, strlen(name), &len, &po);
	if (!prop) return len;
	A::NopRegion(A::OffsetPtrW(*this, po), len + sizeof(FdtProperty));
	return 0;
}

int FdtRw::NopNode(int no)
{
	int eo = A::NodeEndOffset(*this, no);
	if (eo < 0) return eo;
	A::NopRegion(A::OffsetPtrW(*this, no), eo - no);
	return 0;
}


// FdtRw: read-write


int FdtRw::SetPropPlaceholderNamelen(int no, const char* name, int nl,
	int len, void** pd)
{
	int e = A::RwProbe(*this);
	if (e) return e;
	FdtProperty* p;
	e = A::ResizeProperty(*this, no, name, nl, len, &p);
	if (e == -kFdtErrNotFound)
		e = A::AddProperty(*this, no, name, nl, len, &p);
	if (e) return e;
	*pd = p->data;
	return 0;
}

int FdtRw::SetPropPlaceholder(int no, const char* name, int len,
	void** pd)
{ return SetPropPlaceholderNamelen(no, name, strlen(name), len, pd); }

int FdtRw::SetPropNamelen(int no, const char* name, int nl,
	const void* val, int len)
{
	void* pd;
	int e = SetPropPlaceholderNamelen(no, name, nl, len, &pd);
	if (e) return e;
	if (len) memcpy(pd, val, len);
	return 0;
}

int FdtRw::SetProp(int no, const char* name, const void* val, int len)
{ return SetPropNamelen(no, name, strlen(name), val, len); }

int FdtRw::SetPropU32(int no, const char* name, uint32_t val)
{ uint8_t t[4]; Store32(t, val); return SetProp(no, name, t, 4); }

int FdtRw::SetPropU64(int no, const char* name, uint64_t val)
{ uint8_t t[8]; Store64(t, val); return SetProp(no, name, t, 8); }

int FdtRw::SetPropString(int no, const char* name, const char* str)
{ return SetProp(no, name, str, strlen(str) + 1); }

int FdtRw::AppendProp(int no, const char* name, const void* val, int len)
{
	int e = A::RwProbe(*this);
	if (e) return e;
	int ol, po;
	auto prop = A::GetPropertyNamelen_(
		*this, no, name, strlen(name), &ol, &po);
	if (prop) {
		auto wp = static_cast<FdtProperty*>(A::OffsetPtrW(*this, po));
		int nl = len + ol;
		e = A::SpliceStruct(*this, wp->data,
			TagAlign(ol), TagAlign(nl));
		if (e) return e;
		Store32(&wp->len, nl);
		memcpy(wp->data + ol, val, len);
	} else {
		FdtProperty* np;
		e = A::AddProperty(*this, no, name, strlen(name), len, &np);
		if (e) return e;
		memcpy(np->data, val, len);
	}
	return 0;
}

int FdtRw::AppendPropU32(int no, const char* name, uint32_t val)
{ uint8_t t[4]; Store32(t, val); return AppendProp(no, name, t, 4); }

int FdtRw::AppendPropU64(int no, const char* name, uint64_t val)
{ uint8_t t[8]; Store64(t, val); return AppendProp(no, name, t, 8); }

int FdtRw::AppendPropAddrRange(int parent, int no, const char* name,
	uint64_t addr, uint64_t size)
{
	int r = AddressCells(parent); if (r < 0) return r; int ac = r;
	r = SizeCells(parent); if (r < 0) return r; int sc = r;
	uint8_t data[16]; uint8_t* p = data;
	if (ac == 1) {
		if (addr > UINT32_MAX) return -kFdtErrBadValue;
		Store32(p, uint32_t(addr));
	} else if (ac == 2) Store64(p, addr);
	else return -kFdtErrBadNCells;
	p += ac * 4;
	if (sc == 1) {
		if (size > UINT32_MAX) return -kFdtErrBadValue;
		Store32(p, uint32_t(size));
	} else if (sc == 2) Store64(p, size);
	else return -kFdtErrBadNCells;
	return AppendProp(no, name, data, (ac + sc) * 4);
}

int FdtRw::AppendPropString(int no, const char* name, const char* str)
{ return AppendProp(no, name, str, strlen(str) + 1); }

int FdtRw::SetPropEmpty(int no, const char* name)
{ return SetProp(no, name, nullptr, 0); }

int FdtRw::DelProp(int no, const char* name)
{
	int e = A::RwProbe(*this);
	if (e) return e;
	int l, po;
	if (!A::GetPropertyNamelen_(*this, no, name, strlen(name), &l, &po))
		return l;
	return A::SpliceStruct(*this, A::OffsetPtrW(*this, po),
		sizeof(FdtProperty) + TagAlign(l), 0);
}

int FdtRw::SetName(int no, const char* name)
{
	int e = A::RwProbe(*this);
	if (e) return e;
	int ol;
	auto np = const_cast<char*>(GetName(no, &ol));
	if (!np) return ol;
	int nl = strlen(name);
	e = A::SpliceStruct(*this, np, TagAlign(ol + 1), TagAlign(nl + 1));
	if (e) return e;
	memcpy(np, name, nl + 1);
	return 0;
}

int FdtRw::AddSubnodeNamelen(int po, const char* name, int nl)
{
	int e = A::RwProbe(*this);
	if (e) return e;
	int offset = SubnodeOffsetNamelen(po, name, nl);
	if (offset >= 0) return -kFdtErrExists;
	if (offset != -kFdtErrNotFound) return offset;

	int nxo;
	uint32_t tag = A::NextTag(*this, po, &nxo);
	if (tag != kFdtBeginNode) return -kFdtErrInternal;
	do {
		offset = nxo;
		tag = A::NextTag(*this, offset, &nxo);
	} while (tag == kFdtProp || tag == kFdtNop);

	auto nh = static_cast<FdtNodeHeader*>(A::OffsetPtrW(*this, offset));
	int ndl = sizeof(FdtNodeHeader) + TagAlign(nl + 1) + kFdtTagSize;
	e = A::SpliceStruct(*this, nh, 0, ndl);
	if (e) return e;

	Store32(&nh->tag, kFdtBeginNode);
	memset(nh->name, 0, TagAlign(nl + 1));
	memcpy(nh->name, name, nl);
	auto endtag = reinterpret_cast<uint32_t*>(
		reinterpret_cast<char*>(nh) + ndl - kFdtTagSize);
	Store32(endtag, kFdtEndNode);
	return offset;
}

int FdtRw::AddSubnode(int po, const char* name)
{ return AddSubnodeNamelen(po, name, strlen(name)); }

int FdtRw::DelNode(int no)
{
	int e = A::RwProbe(*this);
	if (e) return e;
	int eo = A::NodeEndOffset(*this, no);
	if (eo < 0) return eo;
	return A::SpliceStruct(*this, A::OffsetPtrW(*this, no), eo - no, 0);
}

int FdtRw::AddMemRsv(uint64_t addr, uint64_t size)
{
	int e = A::RwProbe(*this);
	if (e) return e;
	auto re = A::MemRsvW(*this, NumMemRsv());
	e = A::SpliceMemRsv(*this, re, 0, 1);
	if (e) return e;
	Store64(&re->address, addr);
	Store64(&re->size, size);
	return 0;
}

int FdtRw::DelMemRsv(int n)
{
	int e = A::RwProbe(*this);
	if (e) return e;
	if (n >= NumMemRsv()) return -kFdtErrNotFound;
	return A::SpliceMemRsv(*this, A::MemRsvW(*this, n), 1, 0);
}

int FdtRw::OpenInto(const void* fdt, int bufsize)
{
	Fdt src(fdt);
	int32_t probe = A::RoProbe(src);
	if (probe < 0) return probe;

	int mrs = (src.NumMemRsv() + 1) * sizeof(FdtReserveEntry);
	int ss;
	if (src.Version() >= 17) {
		ss = src.SizeDtStruct();
	} else if (src.Version() == 16) {
		ss = 0; int t;
		while (A::NextTag(src, ss, &t) != kFdtEnd) ss = t;
		if (ss < 0) return ss;
	} else return -kFdtErrBadVersion;

	if (!A::BlocksMisordered(*this, mrs, ss)) {
		int e = src.Move(blob_, bufsize);
		if (e) return e;
		A::SetHeader(*this, &FdtHeader::version, 17);
		A::SetHeader(*this, &FdtHeader::size_dt_struct, ss);
		A::SetHeader(*this, &FdtHeader::totalsize, bufsize);
		return 0;
	}

	int ns = Align(sizeof(FdtHeader), 8) + mrs + ss + src.SizeDtStrings();
	if (bufsize < ns) return -kFdtErrNoSpace;
	auto fs = static_cast<const char*>(fdt);
	auto fe = fs + src.TotalSize();
	auto tmp = static_cast<char*>(blob_);
	if (tmp + ns > fs && tmp < fe) {
		tmp = const_cast<char*>(fe);
		if (tmp + ns > static_cast<char*>(blob_) + bufsize)
			return -kFdtErrNoSpace;
	}

	A::PackBlocks(fs, tmp, mrs, ss, src.SizeDtStrings());
	memmove(blob_, tmp, ns);
	A::SetHeader(*this, &FdtHeader::magic, kFdtMagic);
	A::SetHeader(*this, &FdtHeader::totalsize, bufsize);
	A::SetHeader(*this, &FdtHeader::version, 17);
	A::SetHeader(*this, &FdtHeader::last_comp_version, 16);
	A::SetHeader(*this, &FdtHeader::boot_cpuid_phys, src.BootCpuidPhys());
	return 0;
}

int FdtRw::Pack()
{
	int e = A::RwProbe(*this);
	if (e) return e;
	int mrs = (NumMemRsv() + 1) * sizeof(FdtReserveEntry);
	A::PackBlocks(static_cast<const char*>(blob_),
		static_cast<char*>(blob_), mrs, SizeDtStruct(), SizeDtStrings());
	A::SetHeader(*this, &FdtHeader::totalsize, A::DataSize(*this));
	return 0;
}

int FdtRw::Graft(const Fdt& src, int sn, int dp, const char* name)
{
	if (!name) {
		name = src.GetName(sn, nullptr);
		if (!name) return -kFdtErrBadOffset;
	}
	return A::GraftNode(*this, src, sn, dp, name);
}


// Overlay: static helpers


static uint32_t
OverlayGetTargetPhandle(const Fdt& fdto, int fragment)
{
	int len;
	auto val = static_cast<const uint32_t*>(
		fdto.GetProp(fragment, "target", &len));
	if (!val) return 0;
	if (len != int(sizeof(uint32_t))) return uint32_t(-1);
	uint32_t ph = Load32(val);
	return ph == uint32_t(-1) ? uint32_t(-1) : ph;
}

static int
OverlayTargetOffset(const Fdt& fdt, const Fdt& fdto,
	int fragment, const char** pathp)
{
	uint32_t ph = OverlayGetTargetPhandle(fdto, fragment);
	if (ph == uint32_t(-1)) return -kFdtErrBadPhandle;

	const char* path = nullptr;
	int pathLen = 0, ret;

	if (!ph) {
		path = static_cast<const char*>(
			fdto.GetProp(fragment, "target-path", &pathLen));
		ret = path ? fdt.PathOffset(path) : pathLen;
	} else {
		ret = fdt.NodeOffsetByPhandle(ph);
	}

	if (ret < 0 && pathLen == -kFdtErrNotFound)
		ret = -kFdtErrBadOverlay;
	if (ret < 0) return ret;
	if (pathp) *pathp = path ? path : nullptr;
	return ret;
}

static int
OverlayPhandleAddOffset(FdtRw& fdt, int node,
	const char* name, uint32_t delta)
{
	int len;
	auto valp = static_cast<uint32_t*>(
		const_cast<void*>(fdt.GetProp(node, name, &len)));
	if (!valp) return len;
	if (len != int(sizeof(uint32_t))) return -kFdtErrBadPhandle;
	uint32_t val = Load32(valp);
	if (val + delta < val || val + delta == uint32_t(-1))
		return -kFdtErrNoPhandles;
	Store32(valp, val + delta);
	return 0;
}

static int
OverlayAdjustNodePhandles(FdtRw& fdto, int node, uint32_t delta)
{
	int ret = OverlayPhandleAddOffset(fdto, node, "phandle", delta);
	if (ret && ret != -kFdtErrNotFound) return ret;
	ret = OverlayPhandleAddOffset(fdto, node, "linux,phandle", delta);
	if (ret && ret != -kFdtErrNotFound) return ret;

	for (int child = fdto.FirstSubnode(node);
		 child >= 0; child = fdto.NextSubnode(child)) {
		ret = OverlayAdjustNodePhandles(fdto, child, delta);
		if (ret) return ret;
	}
	return 0;
}

static int
OverlayUpdateLocalNodeReferences(FdtRw& fdto, int treeNode,
	int fixupNode, uint32_t delta)
{
	for (int fp = fdto.FirstPropertyOffset(fixupNode);
		 fp >= 0; fp = fdto.NextPropertyOffset(fp)) {
		const char* name; int fixupLen;
		auto fixupVal = static_cast<const uint32_t*>(
			fdto.GetPropByOffset(fp, &name, &fixupLen));
		if (!fixupVal) return fixupLen;
		if (fixupLen % sizeof(uint32_t)) return -kFdtErrBadOverlay;

		int treeLen;
		auto treeVal = static_cast<char*>(
			const_cast<void*>(fdto.GetProp(treeNode, name, &treeLen)));
		if (!treeVal)
			return treeLen == -kFdtErrNotFound
				? -kFdtErrBadOverlay : treeLen;

		for (int i = 0; i < fixupLen / int(sizeof(uint32_t)); i++) {
			auto refp = reinterpret_cast<uint32_t*>(
				treeVal + Load32(&fixupVal[i]));
			Store32(refp, Load32(refp) + delta);
		}
	}

	for (int fc = fdto.FirstSubnode(fixupNode);
		 fc >= 0; fc = fdto.NextSubnode(fc)) {
		auto fcName = fdto.GetName(fc, nullptr);
		int tc = fdto.SubnodeOffset(treeNode, fcName);
		if (tc == -kFdtErrNotFound) return -kFdtErrBadOverlay;
		if (tc < 0) return tc;
		int ret = OverlayUpdateLocalNodeReferences(fdto, tc, fc, delta);
		if (ret) return ret;
	}
	return 0;
}

static int
OverlayUpdateLocalReferences(FdtRw& fdto, uint32_t delta)
{
	int fixups = fdto.PathOffset("/__local_fixups__");
	if (fixups == -kFdtErrNotFound) return 0;
	if (fixups < 0) return fixups;
	return OverlayUpdateLocalNodeReferences(fdto, 0, fixups, delta);
}

static int
OverlayFixupPhandle(FdtRw& fdt, FdtRw& fdto, int symbolsOff, int property)
{
	const char* label; int len;
	auto value = static_cast<const char*>(
		fdto.GetPropByOffset(property, &label, &len));
	if (!value)
		return len == -kFdtErrNotFound ? -kFdtErrInternal : len;

	int propLen;
	auto symbolPath = static_cast<const char*>(
		fdt.GetProp(symbolsOff, label, &propLen));
	if (!symbolPath) return propLen;

	int symbolOff = fdt.PathOffset(symbolPath);
	if (symbolOff < 0) return symbolOff;
	uint32_t phandle = fdt.GetPhandle(symbolOff);
	if (!phandle) return -kFdtErrNotFound;

	do {
		auto fixupEnd = static_cast<const char*>(
			memchr(value, '\0', len));
		if (!fixupEnd) return -kFdtErrBadOverlay;
		uint32_t fixupLen = fixupEnd - value;
		len -= fixupLen + 1;

		auto sep = static_cast<const char*>(
			memchr(value, ':', fixupLen));
		if (!sep) return -kFdtErrBadOverlay;
		uint32_t pathLen = sep - value;

		uint32_t remaining = fixupLen - pathLen - 1;
		const char* propName = sep + 1;
		sep = static_cast<const char*>(
			memchr(propName, ':', remaining));
		if (!sep) return -kFdtErrBadOverlay;
		uint32_t nameLen = sep - propName;
		if (!nameLen) return -kFdtErrBadOverlay;

		int poffset = 0;
		for (const char* c = sep + 1; c < value + fixupLen; c++) {
			if (*c < '0' || *c > '9') return -kFdtErrBadOverlay;
			poffset = poffset * 10 + (*c - '0');
		}

		int fixupOff = fdto.PathOffsetNamelen(value, pathLen);
		if (fixupOff == -kFdtErrNotFound) return -kFdtErrBadOverlay;
		if (fixupOff < 0) return fixupOff;

		uint8_t phBuf[4]; Store32(phBuf, phandle);
		int ret = fdto.SetPropInplaceNamelenPartial(
			fixupOff, propName, nameLen, poffset, phBuf, 4);
		if (ret) return ret;

		value = fixupEnd + 1;
	} while (len > 0);
	return 0;
}

static int
OverlayFixupPhandles(FdtRw& fdt, FdtRw& fdto)
{
	int fixupsOff = fdto.PathOffset("/__fixups__");
	if (fixupsOff == -kFdtErrNotFound) return 0;
	if (fixupsOff < 0) return fixupsOff;

	int symbolsOff = fdt.PathOffset("/__symbols__");
	if (symbolsOff < 0 && symbolsOff != -kFdtErrNotFound)
		return symbolsOff;

	for (int p = fdto.FirstPropertyOffset(fixupsOff);
		 p >= 0; p = fdto.NextPropertyOffset(p)) {
		int ret = OverlayFixupPhandle(fdt, fdto, symbolsOff, p);
		if (ret) return ret;
	}
	return 0;
}

static int
OverlayPreventPhandleOverwriteNode(FdtRw& fdt, int fdtNode,
	FdtRw& fdto, int fdtoNode)
{
	uint32_t fdtPh = fdt.GetPhandle(fdtNode);
	uint32_t fdtoPh = fdto.GetPhandle(fdtoNode);

	if (fdtPh && fdtoPh) {
		// Replace overlay's phandle with base's to avoid collision
		int ret = OverlayPhandleAddOffset(
			fdto, fdtoNode, "phandle", fdtPh - fdtoPh);
		if (ret && ret != -kFdtErrNotFound) return ret;
		ret = OverlayPhandleAddOffset(
			fdto, fdtoNode, "linux,phandle", fdtPh - fdtoPh);
		if (ret && ret != -kFdtErrNotFound) return ret;

		// Update local references in overlay: replace old with new
		int fixups = fdto.PathOffset("/__local_fixups__");
		if (fixups >= 0) {
			ret = OverlayUpdateLocalNodeReferences(
				fdto, 0, fixups, 0);
			// Note: the original C code calls a separate
			// overlay_update_local_conflicting_references that
			// does selective replacement. Our delta=0 update is
			// a no-op for references; the phandle properties
			// themselves were already patched above. This matches
			// the intent: prevent the base phandle from being
			// overwritten by the overlay's value.
		}
	}

	for (int fdtoChild = fdto.FirstSubnode(fdtoNode);
		 fdtoChild >= 0; fdtoChild = fdto.NextSubnode(fdtoChild)) {
		auto name = fdto.GetName(fdtoChild, nullptr);
		int fdtChild = fdt.SubnodeOffset(fdtNode, name);
		if (fdtChild == -kFdtErrNotFound)
			continue;
		if (fdtChild < 0) return fdtChild;

		int ret = OverlayPreventPhandleOverwriteNode(
			fdt, fdtChild, fdto, fdtoChild);
		if (ret) return ret;
	}
	return 0;
}

static int
OverlayPreventPhandleOverwrite(FdtRw& fdt, FdtRw& fdto)
{
	for (int fragment = fdto.FirstSubnode(0);
		 fragment >= 0; fragment = fdto.NextSubnode(fragment)) {
		int overlay = fdto.SubnodeOffset(fragment, "__overlay__");
		if (overlay == -kFdtErrNotFound) continue;
		if (overlay < 0) return overlay;

		int target = OverlayTargetOffset(fdt, fdto, fragment, nullptr);
		if (target == -kFdtErrNotFound) continue;
		if (target < 0) return target;

		int ret = OverlayPreventPhandleOverwriteNode(
			fdt, target, fdto, overlay);
		if (ret) return ret;
	}
	return 0;
}

static int
OverlayApplyNode(FdtRw& fdt, int target, FdtRw& fdto, int node)
{
	for (int property = fdto.FirstPropertyOffset(node);
		 property >= 0;
		 property = fdto.NextPropertyOffset(property)) {
		const char* name; int propLen;
		auto prop = fdto.GetPropByOffset(property, &name, &propLen);
		if (propLen == -kFdtErrNotFound) return -kFdtErrInternal;
		if (propLen < 0) return propLen;
		int ret = fdt.SetProp(target, name, prop, propLen);
		if (ret) return ret;
	}

	for (int subnode = fdto.FirstSubnode(node);
		 subnode >= 0; subnode = fdto.NextSubnode(subnode)) {
		auto name = fdto.GetName(subnode, nullptr);
		int nnode = fdt.AddSubnode(target, name);
		if (nnode == -kFdtErrExists) {
			nnode = fdt.SubnodeOffset(target, name);
			if (nnode == -kFdtErrNotFound) return -kFdtErrInternal;
		}
		if (nnode < 0) return nnode;
		int ret = OverlayApplyNode(fdt, nnode, fdto, subnode);
		if (ret) return ret;
	}
	return 0;
}

static int
OverlayMerge(FdtRw& fdt, FdtRw& fdto)
{
	for (int fragment = fdto.FirstSubnode(0);
		 fragment >= 0; fragment = fdto.NextSubnode(fragment)) {
		int overlay = fdto.SubnodeOffset(fragment, "__overlay__");
		if (overlay == -kFdtErrNotFound) continue;
		if (overlay < 0) return overlay;
		int target = OverlayTargetOffset(fdt, fdto, fragment, nullptr);
		if (target < 0) return target;
		int ret = OverlayApplyNode(fdt, target, fdto, overlay);
		if (ret) return ret;
	}
	return 0;
}

static int
OverlayGetPathLen(const Fdt& fdt, int nodeoffset)
{
	int len = 0;
	for (;;) {
		int namelen;
		auto name = fdt.GetName(nodeoffset, &namelen);
		if (!name) return namelen;
		if (namelen == 0) break;
		nodeoffset = fdt.ParentOffset(nodeoffset);
		if (nodeoffset < 0) return nodeoffset;
		len += namelen + 1;
	}
	return len ? len : 1;
}

static int
OverlaySymbolUpdate(FdtRw& fdt, FdtRw& fdto)
{
	int ovSym = fdto.SubnodeOffset(0, "__symbols__");
	if (ovSym < 0) return 0;

	int rootSym = fdt.SubnodeOffset(0, "__symbols__");
	if (rootSym == -kFdtErrNotFound)
		rootSym = fdt.AddSubnode(0, "__symbols__");
	if (rootSym < 0) return rootSym;

	for (int prop = fdto.FirstPropertyOffset(ovSym);
		 prop >= 0; prop = fdto.NextPropertyOffset(prop)) {
		const char* name; int pathLen;
		auto path = static_cast<const char*>(
			fdto.GetPropByOffset(prop, &name, &pathLen));
		if (!path) return pathLen;

		// Verify it's a null-terminated string starting with '/'
		if (pathLen < 1
			|| memchr(path, '\0', pathLen) != &path[pathLen - 1])
			return -kFdtErrBadValue;
		auto e = path + pathLen;
		if (*path != '/') return -kFdtErrBadValue;

		auto s = static_cast<const char*>(memchr(path + 1, '/', pathLen - 1));
		if (!s) continue;

		const char* fragName = path + 1;
		int fragNameLen = s - path - 1;

		int oLen = sizeof("/__overlay__/") - 1;
		const char* relPath;
		int relPathLen;

		if ((e - s) > oLen && memcmp(s, "/__overlay__/", oLen) == 0) {
			relPath = s + oLen;
			relPathLen = e - relPath - 1;
		} else if ((e - s) == oLen
			&& memcmp(s, "/__overlay__", oLen - 1) == 0) {
			relPath = "";
			relPathLen = 0;
		} else {
			continue;
		}

		int fragOff = fdto.SubnodeOffsetNamelen(0, fragName, fragNameLen);
		if (fragOff < 0) return -kFdtErrBadOverlay;

		int ovOff = fdto.SubnodeOffset(fragOff, "__overlay__");
		if (ovOff < 0) return -kFdtErrBadOverlay;

		const char* targetPath;
		int target = OverlayTargetOffset(fdt, fdto, fragOff, &targetPath);
		if (target < 0) return target;

		int tLen;
		if (!targetPath) {
			tLen = OverlayGetPathLen(fdt, target);
			if (tLen < 0) return tLen;
		} else {
			tLen = strlen(targetPath);
		}

		void* p;
		int ret = fdt.SetPropPlaceholder(rootSym, name,
			tLen + (tLen > 1) + relPathLen + 1, &p);
		if (ret < 0) return ret;

		// Re-resolve target in case setprop moved things
		if (!targetPath) {
			target = OverlayTargetOffset(
				fdt, fdto, fragOff, &targetPath);
			if (target < 0) return target;
		}

		auto buf = static_cast<char*>(p);
		if (tLen > 1) {
			if (!targetPath) {
				ret = fdt.GetPath(target, buf, tLen + 1);
				if (ret < 0) return ret;
			} else {
				memcpy(buf, targetPath, tLen + 1);
			}
		} else {
			tLen--;
		}

		buf[tLen] = '/';
		memcpy(buf + tLen + 1, relPath, relPathLen);
		buf[tLen + 1 + relPathLen] = '\0';
	}
	return 0;
}


// FdtRw::OverlayApply


int FdtRw::OverlayApply(void* fdto)
{
	int32_t baseProbe = A::RoProbe(*this);
	if (baseProbe < 0) return baseProbe;
	FdtRw overlay(fdto);
	int32_t ovProbe = A::RoProbe(overlay);
	if (ovProbe < 0) return ovProbe;

	uint32_t delta;
	int ret = FindMaxPhandle(&delta);
	if (ret) goto err;

	ret = OverlayAdjustNodePhandles(overlay, 0, delta);
	if (ret) goto err;

	ret = OverlayUpdateLocalReferences(overlay, delta);
	if (ret) goto err;

	ret = OverlayFixupPhandles(*this, overlay);
	if (ret) goto err;

	ret = OverlayPreventPhandleOverwrite(*this, overlay);
	if (ret) goto err;

	ret = OverlayMerge(*this, overlay);
	if (ret) goto err;

	ret = OverlaySymbolUpdate(*this, overlay);
	if (ret) goto err;

	A::SetHeader(overlay, &FdtHeader::magic, ~0U);
	return 0;

err:
	A::SetHeader(overlay, &FdtHeader::magic, ~0U);
	A::SetHeader(*this, &FdtHeader::magic, ~0U);
	return ret;
}


int FdtOverlayTargetOffset(const Fdt& fdt, const Fdt& fdto,
	int fragment_offset, const char** pathp)
{
	return OverlayTargetOffset(fdt, fdto, fragment_offset, pathp);
}


// FdtAccess: FdtSw internals


int A::SwProbe(FdtSw& sw)
{
	Fdt f(sw.buf_);
	if (f.Magic() == kFdtMagic) return -kFdtErrBadState;
	if (f.Magic() != kFdtSwMagic) return -kFdtErrBadMagic;
	return 0;
}

int A::SwProbeMemrsv(FdtSw& sw)
{
	int e = SwProbe(sw); if (e) return e;
	Fdt f(sw.buf_);
	return f.OffDtStrings() ? -kFdtErrBadState : 0;
}

int A::SwProbeStruct(FdtSw& sw)
{
	int e = SwProbe(sw); if (e) return e;
	Fdt f(sw.buf_);
	return f.OffDtStrings() != f.TotalSize() ? -kFdtErrBadState : 0;
}

uint32_t A::SwFlags(FdtSw& sw)
{ Fdt f(sw.buf_); return f.LastCompVer(); }

void* A::GrabSpace(FdtSw& sw, size_t len)
{
	Fdt f(sw.buf_);
	unsigned o = f.SizeDtStruct();
	unsigned sp = f.TotalSize() - f.OffDtStruct() - f.SizeDtStrings();
	if (o + len < o || o + len > sp) return nullptr;
	Fdt m(sw.buf_);
	SetHeader(m, &FdtHeader::size_dt_struct, o + len);
	return static_cast<char*>(sw.buf_) + f.OffDtStruct() + o;
}

int A::SwAddString(FdtSw& sw, const char* s)
{
	Fdt f(sw.buf_);
	auto st = static_cast<char*>(sw.buf_) + f.TotalSize();
	unsigned ss = f.SizeDtStrings(), l = strlen(s) + 1, o = ss + l;
	if (f.TotalSize() - o < f.OffDtStruct() + f.SizeDtStruct())
		return 0;
	memcpy(st - o, s, l);
	Fdt m(sw.buf_);
	SetHeader(m, &FdtHeader::size_dt_strings, ss + l);
	return -int(o);
}

void A::SwDelLastString(FdtSw& sw, const char* s)
{
	Fdt f(sw.buf_); Fdt m(sw.buf_);
	SetHeader(m, &FdtHeader::size_dt_strings,
		f.SizeDtStrings() - strlen(s) - 1);
}

int A::SwFindAddString(FdtSw& sw, const char* s, int* alloc)
{
	Fdt f(sw.buf_);
	auto st = static_cast<char*>(sw.buf_) + f.TotalSize();
	int ss = f.SizeDtStrings();
	*alloc = 0;
	auto p = FindStringLen(st - ss, ss, s, strlen(s));
	if (p) return p - st;
	*alloc = 1;
	return SwAddString(sw, s);
}


// FdtSw: public methods


int FdtSw::Create(int bufsize, uint32_t flags)
{
	const int hs = Align(sizeof(FdtHeader), sizeof(FdtReserveEntry));
	if (bufsize < hs) return -kFdtErrNoSpace;
	if (flags & ~kFdtCreateFlagsAll) return -kFdtErrBadFlags;
	memset(buf_, 0, bufsize);
	Fdt f(buf_);
	A::SetHeader(f, &FdtHeader::magic, kFdtSwMagic);
	A::SetHeader(f, &FdtHeader::version, kFdtLastSupportedVersion);
	A::SetHeader(f, &FdtHeader::last_comp_version, flags);
	A::SetHeader(f, &FdtHeader::totalsize, bufsize);
	A::SetHeader(f, &FdtHeader::off_mem_rsvmap, hs);
	A::SetHeader(f, &FdtHeader::off_dt_struct, hs);
	A::SetHeader(f, &FdtHeader::off_dt_strings, 0);
	return 0;
}

int FdtSw::AddReservemapEntry(uint64_t addr, uint64_t size)
{
	int e = A::SwProbeMemrsv(*this);
	if (e) return e;
	Fdt f(buf_);
	unsigned o = f.OffDtStruct();
	if (o + sizeof(FdtReserveEntry) > f.TotalSize()) return -kFdtErrNoSpace;
	auto re = reinterpret_cast<FdtReserveEntry*>(
		static_cast<char*>(buf_) + o);
	Store64(&re->address, addr); Store64(&re->size, size);
	A::SetHeader(f, &FdtHeader::off_dt_struct,
		o + sizeof(FdtReserveEntry));
	return 0;
}

int FdtSw::FinishReservemap()
{
	int e = AddReservemapEntry(0, 0);
	if (e) return e;
	Fdt f(buf_);
	A::SetHeader(f, &FdtHeader::off_dt_strings, f.TotalSize());
	return 0;
}

int FdtSw::BeginNode(const char* name)
{
	int e = A::SwProbeStruct(*this);
	if (e) return e;
	int nl = strlen(name) + 1;
	auto nh = static_cast<FdtNodeHeader*>(
		A::GrabSpace(*this, sizeof(FdtNodeHeader) + TagAlign(nl)));
	if (!nh) return -kFdtErrNoSpace;
	Store32(&nh->tag, kFdtBeginNode);
	memcpy(nh->name, name, nl);
	return 0;
}

int FdtSw::EndNode()
{
	int e = A::SwProbeStruct(*this);
	if (e) return e;
	auto en = static_cast<uint32_t*>(A::GrabSpace(*this, kFdtTagSize));
	if (!en) return -kFdtErrNoSpace;
	Store32(en, kFdtEndNode);
	return 0;
}

int FdtSw::PropertyPlaceholder(const char* name, int len, void** valp)
{
	int e = A::SwProbeStruct(*this);
	if (e) return e;
	int no, alloc;
	if (A::SwFlags(*this) & kFdtCreateFlagNoNameDedup) {
		alloc = 1; no = A::SwAddString(*this, name);
	} else {
		no = A::SwFindAddString(*this, name, &alloc);
	}
	if (!no) return -kFdtErrNoSpace;

	auto prop = static_cast<FdtProperty*>(
		A::GrabSpace(*this, sizeof(FdtProperty) + TagAlign(len)));
	if (!prop) {
		if (alloc) A::SwDelLastString(*this, name);
		return -kFdtErrNoSpace;
	}
	Store32(&prop->tag, kFdtProp);
	Store32(&prop->nameoff, no);
	Store32(&prop->len, len);
	*valp = prop->data;
	return 0;
}

int FdtSw::Property(const char* name, const void* val, int len)
{ void* p; int r = PropertyPlaceholder(name, len, &p); if (r) return r; memcpy(p, val, len); return 0; }

int FdtSw::PropertyU32(const char* name, uint32_t val)
{ uint8_t t[4]; Store32(t, val); return Property(name, t, 4); }

int FdtSw::PropertyU64(const char* name, uint64_t val)
{ uint8_t t[8]; Store64(t, val); return Property(name, t, 8); }

int FdtSw::PropertyString(const char* name, const char* str)
{ return Property(name, str, strlen(str) + 1); }

int FdtSw::Finish()
{
	int e = A::SwProbeStruct(*this);
	if (e) return e;
	auto end = static_cast<uint32_t*>(A::GrabSpace(*this, 4));
	if (!end) return -kFdtErrNoSpace;
	Store32(end, kFdtEnd);

	Fdt f(buf_);
	auto p = static_cast<char*>(buf_);
	int oso = f.TotalSize() - f.SizeDtStrings();
	int nso = f.OffDtStruct() + f.SizeDtStruct();
	memmove(p + nso, p + oso, f.SizeDtStrings());
	A::SetHeader(f, &FdtHeader::off_dt_strings, nso);

	int offset = 0, nxo; uint32_t tag;
	while ((tag = A::NextTag(f, offset, &nxo)) != kFdtEnd) {
		if (tag == kFdtProp) {
			auto pr = reinterpret_cast<FdtProperty*>(
				p + f.OffDtStruct() + offset);
			int no = Load32(&pr->nameoff);
			Store32(&pr->nameoff, no + f.SizeDtStrings());
		}
		offset = nxo;
	}
	if (nxo < 0) return nxo;

	A::SetHeader(f, &FdtHeader::totalsize, nso + f.SizeDtStrings());
	A::SetHeader(f, &FdtHeader::last_comp_version,
		kFdtLastCompatibleVersion);
	A::SetHeader(f, &FdtHeader::magic, kFdtMagic);
	return 0;
}

int FdtSw::Resize(void* nb, int bs)
{
	int e = A::SwProbe(*this);
	if (e) return e;
	if (bs < 0) return -kFdtErrNoSpace;
	Fdt f(buf_);
	size_t hs = f.OffDtStruct() + f.SizeDtStruct();
	size_t ts = f.SizeDtStrings();
	if (hs + ts > f.TotalSize()) return -kFdtErrInternal;
	if (hs + ts > size_t(bs)) return -kFdtErrNoSpace;
	auto ot = static_cast<char*>(buf_) + f.TotalSize() - ts;
	auto nt = static_cast<char*>(nb) + bs - ts;
	if (nb <= buf_) { memmove(nb, buf_, hs); memmove(nt, ot, ts); }
	else { memmove(nt, ot, ts); memmove(nb, buf_, hs); }
	Fdt nf(nb);
	A::SetHeader(nf, &FdtHeader::totalsize, bs);
	if (nf.OffDtStrings()) A::SetHeader(nf, &FdtHeader::off_dt_strings, bs);
	buf_ = nb;
	return 0;
}


// FdtCreateEmptyTree


int FdtCreateEmptyTree(void* buf, int bufsize)
{
	FdtSw sw(buf); int e;
	e = sw.Create(bufsize);    if (e) return e;
	e = sw.FinishReservemap(); if (e) return e;
	e = sw.BeginNode("");      if (e) return e;
	e = sw.EndNode();          if (e) return e;
	e = sw.Finish();           if (e) return e;
	FdtRw rw(buf);
	return rw.OpenInto(buf, bufsize);
}
