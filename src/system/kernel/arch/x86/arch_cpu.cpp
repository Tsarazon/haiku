/*
 * Copyright 2018, Jérôme Duval, jerome.duval@gmail.com.
 * Copyright 2002-2010, Axel Dörfler, axeld@pinc-software.de.
 * Copyright 2013, Paweł Dziepak, pdziepak@quarnos.org.
 * Copyright 2012, Alex Smith, alex@alex-smith.me.uk.
 * Distributed under the terms of the MIT License.
 *
 * Copyright 2001-2002, Travis Geiselbrecht. All rights reserved.
 * Distributed under the terms of the NewOS License.
 */

#include <cpu.h>

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include <algorithm>

#include <ACPI.h>

#include <boot_device.h>
#include <commpage.h>
#include <debug.h>
#include <elf.h>
#include <safemode.h>
#include <smp.h>
#include <util/BitUtils.h>
#include <vm/vm.h>
#include <vm/vm_types.h>
#include <vm/VMAddressSpace.h>

#include <arch_system_info.h>
#include <arch/x86/apic.h>
#include <boot/kernel_args.h>

#include "paging/X86PagingStructures.h"
#include "paging/X86VMTranslationMap.h"


#define DUMP_FEATURE_STRING	1
#define DUMP_CPU_TOPOLOGY	1
#define DUMP_CPU_PATCHLEVEL_TYPE	1


// CPUID Leaf Constants (Intel SDM Vol. 2A)
static const uint32 CPUID_LEAF_BASIC_INFO = 0x01;
static const uint32 CPUID_LEAF_CACHE_PARAMS = 0x04;
static const uint32 CPUID_LEAF_POWER_MANAGEMENT = 0x06;
static const uint32 CPUID_LEAF_EXTENDED_FEATURES = 0x07;
static const uint32 CPUID_LEAF_EXTENDED_STATE = 0x0d;
static const uint32 CPUID_LEAF_FREQUENCY = 0x16;
static const uint32 CPUID_LEAF_HYBRID_INFO = 0x1a;
static const uint32 CPUID_LEAF_V2_TOPOLOGY = 0x1f;
static const uint32 IA32_CPUID_LEAF_TSC = 0x15;

// AMD Extended CPUID Leaves (AMD APM Vol. 3)
static const uint32 CPUID_LEAF_AMD_EXTENDED = 0x80000001;
static const uint32 CPUID_LEAF_AMD_BRAND_1 = 0x80000002;
static const uint32 CPUID_LEAF_AMD_BRAND_2 = 0x80000003;
static const uint32 CPUID_LEAF_AMD_BRAND_3 = 0x80000004;
static const uint32 CPUID_LEAF_AMD_POWER = 0x80000007;
static const uint32 CPUID_LEAF_AMD_TOPOLOGY = 0x80000008;
static const uint32 CPUID_LEAF_AMD_CACHE_TOPOLOGY = 0x8000001d;

// Intel Model-specific constants
static const uint32 INTEL_MODEL_DENVERTON = 0x5f;

// AMD Family constants
static const uint32 AMD_FAMILY_10H = 0x10;
static const uint32 AMD_FAMILY_11H = 0x11;
static const uint32 AMD_FAMILY_17H = 0x17;
static const uint32 AMD_FAMILY_19H = 0x19;

// Pentium errata model thresholds
static const uint32 PENTIUM_III_MAX_MODEL = 13;
static const uint32 PENTIUM_4_MAX_MODEL = 6;

// Intel topology level types (CPUID.1Fh/0Bh.ECX[15:8])
static const int TOPOLOGY_LEVEL_TYPE_SMT = 1;
static const int TOPOLOGY_LEVEL_TYPE_CORE = 2;

// Microcode update constants
static const uint32 MICROCODE_LOADER_REVISION = 1;
static const uint32 MICROCODE_HEADER_ALIGNMENT = 16;
static const uint32 MICROCODE_DEFAULT_SIZE = 2000;
static const uint32 MICROCODE_MAX_TABLE_ENTRIES = 1024;

// AMD MSR constants
static const uint32 MSR_F10H_HWCR = 0xc0010015;
static const uint32 MSR_F10H_PSTATEDEF_BASE = 0xc0010064;
static const uint32 HWCR_TSCFREQSEL = (1 << 24);
static const uint32 PSTATEDEF_EN = (1ULL << 63);

// AMD K8 C1E control bits (AMD BKDG)
static const uint64 K8_SMIONCMPHALT = (1ULL << 27);
static const uint64 K8_C1EONCMPHALT = (1ULL << 28);
static const uint64 K8_CMPHALT = (K8_SMIONCMPHALT | K8_C1EONCMPHALT);
static const uint32 K8_MSR_IPM = 0xc0010055;

// PAT MSR entry manipulation
static const uint32 IA32_MSR_PAT_ENTRY_SHIFT_BASE = 0;
static const uint32 IA32_MSR_PAT_ENTRY_MASK = 0xff;
static const uint32 IA32_MSR_PAT_TYPE_WRITE_COMBINING = 0x01;

static inline uint32 IA32_MSR_PAT_ENTRY_SHIFT(uint32 entry) {
	return IA32_MSR_PAT_ENTRY_SHIFT_BASE + (entry * 8);
}

// AMD DE_CFG MSR
static const uint32 MSR_F10H_DE_CFG = 0xc0011029;
static const uint32 DE_CFG_SERIALIZE_LFENCE = (1 << 1);


// CPU vendor identification strings (CPUID.0 EBX:EDX:ECX)
struct cpu_vendor_info {
	const char *vendor;
	const char *ident_string[2];
};

static const struct cpu_vendor_info vendor_info[VENDOR_NUM] = {
	{ "Intel", { "GenuineIntel" } },
	{ "AMD", { "AuthenticAMD" } },
	{ "Cyrix", { "CyrixInstead" } },
	{ "UMC", { "UMC UMC UMC" } },
	{ "NexGen", { "NexGenDriven" } },
	{ "Centaur", { "CentaurHauls" } },
	{ "Rise", { "RiseRiseRise" } },
	{ "Transmeta", { "GenuineTMx86", "TransmetaCPU" } },
	{ "NSC", { "Geode by NSC" } },
	{ "Hygon", { "HygonGenuine" } },
};

struct set_mtrr_parameter {
	int32	index;
	uint64	base;
	uint64	length;
	uint8	type;
};

struct set_mtrrs_parameter {
	const x86_mtrr_info*	infos;
	uint32					count;
	uint8					defaultType;
};


#ifdef __x86_64__
extern addr_t _stac;
extern addr_t _clac;
extern addr_t _xsave;
extern addr_t _xsavec;
extern addr_t _xrstor;
uint64 gXsaveMask;
uint64 gFPUSaveLength = 512;
bool gHasXsave = false;
bool gHasXsavec = false;
#endif

extern "C" void x86_reboot(void);

void (*gCpuIdleFunc)(void);
#ifndef __x86_64__
void (*gX86SwapFPUFunc)(void* oldState, const void* newState) = x86_noop_swap;
bool gHasSSE = false;
#endif

static uint32 sCpuRendezvous;
static uint32 sCpuRendezvous2;
static uint32 sCpuRendezvous3;
static vint32 sTSCSyncRendezvous;

static addr_t sDoubleFaultStacks = 0;
static const size_t kDoubleFaultStackSize = 4096;

static x86_cpu_module_info* sCpuModule;

// CPU topology information
static uint32 (*sGetCPUTopologyID)(int currentCPU);
static uint32 sHierarchyMask[CPU_TOPOLOGY_LEVELS];
static uint32 sHierarchyShift[CPU_TOPOLOGY_LEVELS];

// Cache topology information
static uint32 sCacheSharingMask[CPU_MAX_CACHE_LEVEL];

static void* sUcodeData = NULL;
static size_t sUcodeDataSize = 0;
static void* sLoadedUcodeUpdate;
static spinlock sUcodeUpdateLock = B_SPINLOCK_INITIALIZER;

static bool sUsePAT = false;


namespace {

	// CPU feature flags for display
	struct FeatureFlag {
		uint64 mask;
		int feature_index;
		const char* name;
	};

	// Intel SDM Vol. 2A: CPUID - CPU Identification
	// AMD APM Vol. 3: General-Purpose and System Instructions
	static const FeatureFlag kCpuFeatures[] = {
		// CPUID.01h.EDX
		{IA32_FEATURE_FPU, FEATURE_COMMON, "fpu"},
		{IA32_FEATURE_VME, FEATURE_COMMON, "vme"},
		{IA32_FEATURE_DE, FEATURE_COMMON, "de"},
		{IA32_FEATURE_PSE, FEATURE_COMMON, "pse"},
		{IA32_FEATURE_TSC, FEATURE_COMMON, "tsc"},
		{IA32_FEATURE_MSR, FEATURE_COMMON, "msr"},
		{IA32_FEATURE_PAE, FEATURE_COMMON, "pae"},
		{IA32_FEATURE_MCE, FEATURE_COMMON, "mce"},
		{IA32_FEATURE_CX8, FEATURE_COMMON, "cx8"},
		{IA32_FEATURE_APIC, FEATURE_COMMON, "apic"},
		{IA32_FEATURE_SEP, FEATURE_COMMON, "sep"},
		{IA32_FEATURE_MTRR, FEATURE_COMMON, "mtrr"},
		{IA32_FEATURE_PGE, FEATURE_COMMON, "pge"},
		{IA32_FEATURE_MCA, FEATURE_COMMON, "mca"},
		{IA32_FEATURE_CMOV, FEATURE_COMMON, "cmov"},
		{IA32_FEATURE_PAT, FEATURE_COMMON, "pat"},
		{IA32_FEATURE_PSE36, FEATURE_COMMON, "pse36"},
		{IA32_FEATURE_PSN, FEATURE_COMMON, "psn"},
		{IA32_FEATURE_CLFSH, FEATURE_COMMON, "clfsh"},
		{IA32_FEATURE_DS, FEATURE_COMMON, "ds"},
		{IA32_FEATURE_ACPI, FEATURE_COMMON, "acpi"},
		{IA32_FEATURE_MMX, FEATURE_COMMON, "mmx"},
		{IA32_FEATURE_FXSR, FEATURE_COMMON, "fxsr"},
		{IA32_FEATURE_SSE, FEATURE_COMMON, "sse"},
		{IA32_FEATURE_SSE2, FEATURE_COMMON, "sse2"},
		{IA32_FEATURE_SS, FEATURE_COMMON, "ss"},
		{IA32_FEATURE_HTT, FEATURE_COMMON, "htt"},
		{IA32_FEATURE_TM, FEATURE_COMMON, "tm"},
		{IA32_FEATURE_PBE, FEATURE_COMMON, "pbe"},

		// CPUID.01h.ECX
		{IA32_FEATURE_EXT_SSE3, FEATURE_EXT, "sse3"},
		{IA32_FEATURE_EXT_PCLMULQDQ, FEATURE_EXT, "pclmulqdq"},
		{IA32_FEATURE_EXT_DTES64, FEATURE_EXT, "dtes64"},
		{IA32_FEATURE_EXT_MONITOR, FEATURE_EXT, "monitor"},
		{IA32_FEATURE_EXT_DSCPL, FEATURE_EXT, "dscpl"},
		{IA32_FEATURE_EXT_VMX, FEATURE_EXT, "vmx"},
		{IA32_FEATURE_EXT_SMX, FEATURE_EXT, "smx"},
		{IA32_FEATURE_EXT_EST, FEATURE_EXT, "est"},
		{IA32_FEATURE_EXT_TM2, FEATURE_EXT, "tm2"},
		{IA32_FEATURE_EXT_SSSE3, FEATURE_EXT, "ssse3"},
		{IA32_FEATURE_EXT_CNXTID, FEATURE_EXT, "cnxtid"},
		{IA32_FEATURE_EXT_FMA, FEATURE_EXT, "fma"},
		{IA32_FEATURE_EXT_CX16, FEATURE_EXT, "cx16"},
		{IA32_FEATURE_EXT_XTPR, FEATURE_EXT, "xtpr"},
		{IA32_FEATURE_EXT_PDCM, FEATURE_EXT, "pdcm"},
		{IA32_FEATURE_EXT_PCID, FEATURE_EXT, "pcid"},
		{IA32_FEATURE_EXT_DCA, FEATURE_EXT, "dca"},
		{IA32_FEATURE_EXT_SSE4_1, FEATURE_EXT, "sse4_1"},
		{IA32_FEATURE_EXT_SSE4_2, FEATURE_EXT, "sse4_2"},
		{IA32_FEATURE_EXT_X2APIC, FEATURE_EXT, "x2apic"},
		{IA32_FEATURE_EXT_MOVBE, FEATURE_EXT, "movbe"},
		{IA32_FEATURE_EXT_POPCNT, FEATURE_EXT, "popcnt"},
		{IA32_FEATURE_EXT_TSCDEADLINE, FEATURE_EXT, "tscdeadline"},
		{IA32_FEATURE_EXT_AES, FEATURE_EXT, "aes"},
		{IA32_FEATURE_EXT_XSAVE, FEATURE_EXT, "xsave"},
		{IA32_FEATURE_EXT_OSXSAVE, FEATURE_EXT, "osxsave"},
		{IA32_FEATURE_EXT_AVX, FEATURE_EXT, "avx"},
		{IA32_FEATURE_EXT_F16C, FEATURE_EXT, "f16c"},
		{IA32_FEATURE_EXT_RDRND, FEATURE_EXT, "rdrnd"},
		{IA32_FEATURE_EXT_HYPERVISOR, FEATURE_EXT, "hypervisor"},

		// AMD extended features
		{IA32_FEATURE_AMD_EXT_SYSCALL, FEATURE_EXT_AMD, "syscall"},
		{IA32_FEATURE_AMD_EXT_NX, FEATURE_EXT_AMD, "nx"},
		{IA32_FEATURE_AMD_EXT_MMXEXT, FEATURE_EXT_AMD, "mmxext"},
		{IA32_FEATURE_AMD_EXT_FFXSR, FEATURE_EXT_AMD, "ffxsr"},
		{IA32_FEATURE_AMD_EXT_PDPE1GB, FEATURE_EXT_AMD, "pdpe1gb"},
		{IA32_FEATURE_AMD_EXT_LONG, FEATURE_EXT_AMD, "long"},
		{IA32_FEATURE_AMD_EXT_3DNOWEXT, FEATURE_EXT_AMD, "3dnowext"},
		{IA32_FEATURE_AMD_EXT_3DNOW, FEATURE_EXT_AMD, "3dnow"},

		// CPUID.06h.EAX
		{IA32_FEATURE_DTS, FEATURE_6_EAX, "dts"},
		{IA32_FEATURE_ITB, FEATURE_6_EAX, "itb"},
		{IA32_FEATURE_ARAT, FEATURE_6_EAX, "arat"},
		{IA32_FEATURE_PLN, FEATURE_6_EAX, "pln"},
		{IA32_FEATURE_ECMD, FEATURE_6_EAX, "ecmd"},
		{IA32_FEATURE_PTM, FEATURE_6_EAX, "ptm"},
		{IA32_FEATURE_HWP, FEATURE_6_EAX, "hwp"},
		{IA32_FEATURE_HWP_NOTIFY, FEATURE_6_EAX, "hwp_notify"},
		{IA32_FEATURE_HWP_ACTWIN, FEATURE_6_EAX, "hwp_actwin"},
		{IA32_FEATURE_HWP_EPP, FEATURE_6_EAX, "hwp_epp"},
		{IA32_FEATURE_HWP_PLR, FEATURE_6_EAX, "hwp_plr"},
		{IA32_FEATURE_HDC, FEATURE_6_EAX, "hdc"},
		{IA32_FEATURE_TBMT3, FEATURE_6_EAX, "tbmt3"},
		{IA32_FEATURE_HWP_CAP, FEATURE_6_EAX, "hwp_cap"},
		{IA32_FEATURE_HWP_PECI, FEATURE_6_EAX, "hwp_peci"},
		{IA32_FEATURE_HWP_FLEX, FEATURE_6_EAX, "hwp_flex"},
		{IA32_FEATURE_HWP_FAST, FEATURE_6_EAX, "hwp_fast"},
		{IA32_FEATURE_HW_FEEDBACK, FEATURE_6_EAX, "hw_feedback"},
		{IA32_FEATURE_HWP_IGNIDL, FEATURE_6_EAX, "hwp_ignidl"},
		{IA32_FEATURE_APERFMPERF, FEATURE_6_ECX, "aperfmperf"},
		{IA32_FEATURE_EPB, FEATURE_6_ECX, "epb"},

		// CPUID.07h.EBX
		{IA32_FEATURE_TSC_ADJUST, FEATURE_7_EBX, "tsc_adjust"},
		{IA32_FEATURE_SGX, FEATURE_7_EBX, "sgx"},
		{IA32_FEATURE_BMI1, FEATURE_7_EBX, "bmi1"},
		{IA32_FEATURE_HLE, FEATURE_7_EBX, "hle"},
		{IA32_FEATURE_AVX2, FEATURE_7_EBX, "avx2"},
		{IA32_FEATURE_SMEP, FEATURE_7_EBX, "smep"},
		{IA32_FEATURE_BMI2, FEATURE_7_EBX, "bmi2"},
		{IA32_FEATURE_ERMS, FEATURE_7_EBX, "erms"},
		{IA32_FEATURE_INVPCID, FEATURE_7_EBX, "invpcid"},
		{IA32_FEATURE_RTM, FEATURE_7_EBX, "rtm"},
		{IA32_FEATURE_CQM, FEATURE_7_EBX, "cqm"},
		{IA32_FEATURE_MPX, FEATURE_7_EBX, "mpx"},
		{IA32_FEATURE_RDT_A, FEATURE_7_EBX, "rdt_a"},
		{IA32_FEATURE_AVX512F, FEATURE_7_EBX, "avx512f"},
		{IA32_FEATURE_AVX512DQ, FEATURE_7_EBX, "avx512dq"},
		{IA32_FEATURE_RDSEED, FEATURE_7_EBX, "rdseed"},
		{IA32_FEATURE_ADX, FEATURE_7_EBX, "adx"},
		{IA32_FEATURE_SMAP, FEATURE_7_EBX, "smap"},
		{IA32_FEATURE_AVX512IFMA, FEATURE_7_EBX, "avx512ifma"},
		{IA32_FEATURE_PCOMMIT, FEATURE_7_EBX, "pcommit"},
		{IA32_FEATURE_CLFLUSHOPT, FEATURE_7_EBX, "cflushopt"},
		{IA32_FEATURE_CLWB, FEATURE_7_EBX, "clwb"},
		{IA32_FEATURE_INTEL_PT, FEATURE_7_EBX, "intel_pt"},
		{IA32_FEATURE_AVX512PF, FEATURE_7_EBX, "avx512pf"},
		{IA32_FEATURE_AVX512ER, FEATURE_7_EBX, "avx512er"},
		{IA32_FEATURE_AVX512CD, FEATURE_7_EBX, "avx512cd"},
		{IA32_FEATURE_SHA_NI, FEATURE_7_EBX, "sha_ni"},
		{IA32_FEATURE_AVX512BW, FEATURE_7_EBX, "avx512bw"},
		{IA32_FEATURE_AVX512VI, FEATURE_7_EBX, "avx512vi"},

		// CPUID.07h.ECX
		{IA32_FEATURE_AVX512VMBI, FEATURE_7_ECX, "avx512vmbi"},
		{IA32_FEATURE_UMIP, FEATURE_7_ECX, "umip"},
		{IA32_FEATURE_PKU, FEATURE_7_ECX, "pku"},
		{IA32_FEATURE_OSPKE, FEATURE_7_ECX, "ospke"},
		{IA32_FEATURE_AVX512VMBI2, FEATURE_7_ECX, "avx512vmbi2"},
		{IA32_FEATURE_GFNI, FEATURE_7_ECX, "gfni"},
		{IA32_FEATURE_VAES, FEATURE_7_ECX, "vaes"},
		{IA32_FEATURE_VPCLMULQDQ, FEATURE_7_ECX, "vpclmulqdq"},
		{IA32_FEATURE_AVX512_VNNI, FEATURE_7_ECX, "avx512vnni"},
		{IA32_FEATURE_AVX512_BITALG, FEATURE_7_ECX, "avx512bitalg"},
		{IA32_FEATURE_AVX512_VPOPCNTDQ, FEATURE_7_ECX, "avx512vpopcntdq"},
		{IA32_FEATURE_LA57, FEATURE_7_ECX, "la57"},
		{IA32_FEATURE_RDPID, FEATURE_7_ECX, "rdpid"},
		{IA32_FEATURE_SGX_LC, FEATURE_7_ECX, "sgx_lc"},

		// CPUID.07h.EDX
		{IA32_FEATURE_HYBRID_CPU, FEATURE_7_EDX, "hybrid"},
		{IA32_FEATURE_IBRS, FEATURE_7_EDX, "ibrs"},
		{IA32_FEATURE_STIBP, FEATURE_7_EDX, "stibp"},
		{IA32_FEATURE_L1D_FLUSH, FEATURE_7_EDX, "l1d_flush"},
		{IA32_FEATURE_ARCH_CAPABILITIES, FEATURE_7_EDX, "msr_arch"},
		{IA32_FEATURE_SSBD, FEATURE_7_EDX, "ssbd"},

		// AMD extended features
		{IA32_FEATURE_AMD_HW_PSTATE, FEATURE_EXT_7_EDX, "hwpstate"},
		{IA32_FEATURE_INVARIANT_TSC, FEATURE_EXT_7_EDX, "constant_tsc"},
		{IA32_FEATURE_CPB, FEATURE_EXT_7_EDX, "cpb"},
		{IA32_FEATURE_PROC_FEEDBACK, FEATURE_EXT_7_EDX, "proc_feedback"},

		// CPUID.0Dh.01h.EAX
		{IA32_FEATURE_XSAVEOPT, FEATURE_D_1_EAX, "xsaveopt"},
		{IA32_FEATURE_XSAVEC, FEATURE_D_1_EAX, "xsavec"},
		{IA32_FEATURE_XGETBV1, FEATURE_D_1_EAX, "xgetbv1"},
		{IA32_FEATURE_XSAVES, FEATURE_D_1_EAX, "xsaves"},

		// AMD extended features (CPUID.80000008h.EBX)
		{IA32_FEATURE_CLZERO, FEATURE_EXT_8_EBX, "clzero"},
		{IA32_FEATURE_IBPB, FEATURE_EXT_8_EBX, "ibpb"},
		{IA32_FEATURE_AMD_SSBD, FEATURE_EXT_8_EBX, "amd_ssbd"},
		{IA32_FEATURE_VIRT_SSBD, FEATURE_EXT_8_EBX, "virt_ssbd"},
		{IA32_FEATURE_AMD_SSB_NO, FEATURE_EXT_8_EBX, "amd_ssb_no"},
		{IA32_FEATURE_CPPC, FEATURE_EXT_8_EBX, "cppc"},
	};

	static const size_t kFeatureCount = sizeof(kCpuFeatures) / sizeof(kCpuFeatures[0]);


	// Helper functions

	static inline bool
	is_vendor_intel(const cpu_ent* cpu)
	{
		return cpu->arch.vendor == VENDOR_INTEL;
	}


	static inline bool
	is_vendor_amd(const cpu_ent* cpu)
	{
		return cpu->arch.vendor == VENDOR_AMD || cpu->arch.vendor == VENDOR_HYGON;
	}


	static inline uint32
	compute_cpu_family(const cpu_ent* cpu)
	{
		return cpu->arch.family + cpu->arch.extended_family;
	}


	static inline uint32
	compute_cpu_model(const cpu_ent* cpu)
	{
		return (cpu->arch.extended_model << 4) | cpu->arch.model;
	}


	// Intel SDM Vol. 3A 11.11: Pentium II/III/4 errata - upper PAT entries unusable
	static inline bool
	has_broken_pat(const cpu_ent* cpu)
	{
		if (!is_vendor_intel(cpu))
			return false;

		return cpu->arch.extended_family == 0
		&& cpu->arch.extended_model == 0
		&& ((cpu->arch.family == 6 && cpu->arch.model <= PENTIUM_III_MAX_MODEL)
		|| (cpu->arch.family == 15 && cpu->arch.model <= PENTIUM_4_MAX_MODEL));
	}


	static inline uint32
	MSR_F10H_PSTATEDEF(uint32 index)
	{
		return MSR_F10H_PSTATEDEF_BASE + index;
	}

} // anonymous namespace


// ACPI shutdown

static status_t
acpi_shutdown(bool rebootSystem)
{
	if (debug_debugger_running() || !are_interrupts_enabled())
		return B_ERROR;

	acpi_module_info* acpi;
	if (get_module(B_ACPI_MODULE_NAME, (module_info**)&acpi) != B_OK)
		return B_NOT_SUPPORTED;

	status_t status;
	if (rebootSystem) {
		status = acpi->reboot();
	} else {
		status = acpi->prepare_sleep_state(ACPI_POWER_STATE_OFF, NULL, 0);
		if (status == B_OK) {
			status = acpi->enter_sleep_state(ACPI_POWER_STATE_OFF);
		}
	}

	put_module(B_ACPI_MODULE_NAME);
	return status;
}


// Cache control

static void
disable_caches()
{
	x86_write_cr0((x86_read_cr0() | CR0_CACHE_DISABLE)
	& ~CR0_NOT_WRITE_THROUGH);
	wbinvd();
	arch_cpu_global_TLB_invalidate();
}


static void
enable_caches()
{
	wbinvd();
	arch_cpu_global_TLB_invalidate();
	x86_write_cr0(x86_read_cr0()
	& ~(CR0_CACHE_DISABLE | CR0_NOT_WRITE_THROUGH));
}


// MTRR management

static void
set_mtrr(void* _parameter, int cpu)
{
	struct set_mtrr_parameter* parameter
	= (struct set_mtrr_parameter*)_parameter;

	smp_cpu_rendezvous(&sCpuRendezvous);

	if (cpu == 0)
		atomic_set((int32*)&sCpuRendezvous3, 0);

	disable_caches();

	sCpuModule->set_mtrr(parameter->index, parameter->base, parameter->length,
						 parameter->type);

	enable_caches();

	smp_cpu_rendezvous(&sCpuRendezvous2);
	smp_cpu_rendezvous(&sCpuRendezvous3);
}


static void
set_mtrrs(void* _parameter, int cpu)
{
	set_mtrrs_parameter* parameter = (set_mtrrs_parameter*)_parameter;

	smp_cpu_rendezvous(&sCpuRendezvous);

	if (cpu == 0)
		atomic_set((int32*)&sCpuRendezvous3, 0);

	disable_caches();

	sCpuModule->set_mtrrs(parameter->defaultType, parameter->infos,
						  parameter->count);

	enable_caches();

	smp_cpu_rendezvous(&sCpuRendezvous2);
	smp_cpu_rendezvous(&sCpuRendezvous3);
}


static void
init_mtrrs(void* _unused, int cpu)
{
	smp_cpu_rendezvous(&sCpuRendezvous);

	if (cpu == 0)
		atomic_set((int32*)&sCpuRendezvous3, 0);

	disable_caches();

	sCpuModule->init_mtrrs();

	enable_caches();

	smp_cpu_rendezvous(&sCpuRendezvous2);
	smp_cpu_rendezvous(&sCpuRendezvous3);
}


uint32
x86_count_mtrrs(void)
{
	if (sUsePAT) {
		dprintf("ignoring MTRRs due to PAT support\n");
		return 0;
	}

	if (sCpuModule == NULL)
		return 0;

	return sCpuModule->count_mtrrs();
}


void
x86_set_mtrr(uint32 index, uint64 base, uint64 length, uint8 type)
{
	struct set_mtrr_parameter parameter;
	parameter.index = index;
	parameter.base = base;
	parameter.length = length;
	parameter.type = type;

	sCpuRendezvous = sCpuRendezvous2 = 0;
	call_all_cpus(&set_mtrr, &parameter);
}


status_t
x86_get_mtrr(uint32 index, uint64* _base, uint64* _length, uint8* _type)
{
	return sCpuModule->get_mtrr(index, _base, _length, _type);
}


void
x86_set_mtrrs(uint8 defaultType, const x86_mtrr_info* infos, uint32 count)
{
	if (sCpuModule == NULL)
		return;

	struct set_mtrrs_parameter parameter;
	parameter.defaultType = defaultType;
	parameter.infos = infos;
	parameter.count = count;

	sCpuRendezvous = sCpuRendezvous2 = 0;
	call_all_cpus(&set_mtrrs, &parameter);
}


// PAT (Page Attribute Table) management

static void
init_pat(int cpu)
{
	disable_caches();

	uint64 value = x86_read_msr(IA32_MSR_PAT);
	dprintf("PAT MSR on CPU %d before init: %#" B_PRIx64 "\n", cpu, value);

	// Intel SDM Vol. 3A 11.12: Use entry 4 for write-combining
	value &= ~(uint64(IA32_MSR_PAT_ENTRY_MASK) << IA32_MSR_PAT_ENTRY_SHIFT(4));
	value |= uint64(IA32_MSR_PAT_TYPE_WRITE_COMBINING) << IA32_MSR_PAT_ENTRY_SHIFT(4);

	dprintf("PAT MSR on CPU %d after init: %#" B_PRIx64 "\n", cpu, value);
	x86_write_msr(IA32_MSR_PAT, value);

	enable_caches();
}


// FPU initialization

void
x86_init_fpu(void)
{
	#ifndef __x86_64__
	if (!x86_check_feature(IA32_FEATURE_FPU, FEATURE_COMMON)) {
		dprintf("%s: Warning: CPU has no reported FPU.\n", __func__);
		gX86SwapFPUFunc = x86_noop_swap;
		return;
	}

	if (!x86_check_feature(IA32_FEATURE_SSE, FEATURE_COMMON)
		|| !x86_check_feature(IA32_FEATURE_FXSR, FEATURE_COMMON)) {
		dprintf("%s: CPU has no SSE... just enabling FPU.\n", __func__);
	x86_write_cr0(x86_read_cr0() & ~(CR0_FPU_EMULATION | CR0_MONITOR_FPU));
	gX86SwapFPUFunc = x86_fnsave_swap;
	return;
		}
		#endif

		dprintf("%s: CPU has SSE... enabling FXSR and XMM.\n", __func__);
		#ifndef __x86_64__
		x86_write_cr4(x86_read_cr4() | CR4_OS_FXSR | CR4_OS_XMM_EXCEPTION);
		x86_write_cr0(x86_read_cr0() & ~(CR0_FPU_EMULATION | CR0_MONITOR_FPU));

		gX86SwapFPUFunc = x86_fxsave_swap;
		gHasSSE = true;
		#endif
}


// CPU feature string dumping

#if DUMP_FEATURE_STRING
static void
dump_feature_string(int currentCPU, cpu_ent* cpu)
{
	char features[768];
	features[0] = 0;

	for (size_t i = 0; i < kFeatureCount; i++) {
		const FeatureFlag& feature = kCpuFeatures[i];
		if (cpu->arch.feature[feature.feature_index] & feature.mask) {
			strlcat(features, feature.name, sizeof(features));
			strlcat(features, " ", sizeof(features));
		}
	}

	dprintf("CPU %d: features: %s\n", currentCPU, features);
}
#endif


// CPU topology detection

static void
compute_cpu_hierarchy_masks(int maxLogicalID, int maxCoreID)
{
	ASSERT(maxLogicalID >= maxCoreID);
	const int kMaxSMTID = maxLogicalID / maxCoreID;

	sHierarchyMask[CPU_TOPOLOGY_SMT] = kMaxSMTID - 1;
	sHierarchyShift[CPU_TOPOLOGY_SMT] = 0;

	sHierarchyMask[CPU_TOPOLOGY_CORE] = (maxCoreID - 1) * kMaxSMTID;
	sHierarchyShift[CPU_TOPOLOGY_CORE]
	= count_set_bits(sHierarchyMask[CPU_TOPOLOGY_SMT]);

	const uint32 kSinglePackageMask = sHierarchyMask[CPU_TOPOLOGY_SMT]
	| sHierarchyMask[CPU_TOPOLOGY_CORE];
	sHierarchyMask[CPU_TOPOLOGY_PACKAGE] = ~kSinglePackageMask;
	sHierarchyShift[CPU_TOPOLOGY_PACKAGE] = count_set_bits(kSinglePackageMask);
}


static uint32
get_cpu_legacy_initial_apic_id(int /* currentCPU */)
{
	cpuid_info cpuid;
	get_current_cpuid(&cpuid, CPUID_LEAF_BASIC_INFO, 0);
	return cpuid.regs.ebx >> 24;
}


// AMD CPU topology detection (AMD APM Vol. 3, Appendix E)

static inline status_t
detect_amd_cpu_topology(uint32 maxBasicLeaf, uint32 maxExtendedLeaf)
{
	sGetCPUTopologyID = get_cpu_legacy_initial_apic_id;

	cpuid_info cpuid;
	get_current_cpuid(&cpuid, CPUID_LEAF_BASIC_INFO, 0);
	int maxLogicalID = next_power_of_2((cpuid.regs.ebx >> 16) & 0xff);

	int maxCoreID = 1;
	if (maxExtendedLeaf >= CPUID_LEAF_AMD_TOPOLOGY) {
		get_current_cpuid(&cpuid, CPUID_LEAF_AMD_TOPOLOGY, 0);
		maxCoreID = (cpuid.regs.ecx >> 12) & 0xf;
		if (maxCoreID != 0)
			maxCoreID = 1 << maxCoreID;
		else
			maxCoreID = next_power_of_2((cpuid.regs.edx & 0xf) + 1);
	}

	if (maxExtendedLeaf >= CPUID_LEAF_AMD_EXTENDED) {
		get_current_cpuid(&cpuid, CPUID_LEAF_AMD_EXTENDED, 0);
		if (x86_check_feature(IA32_FEATURE_AMD_EXT_CMPLEGACY,
			FEATURE_EXT_AMD_ECX))
			maxCoreID = maxLogicalID;
	}

	compute_cpu_hierarchy_masks(maxLogicalID, maxCoreID);

	return B_OK;
}


static void
detect_amd_cache_topology(uint32 maxExtendedLeaf)
{
	if (!x86_check_feature(IA32_FEATURE_AMD_EXT_TOPOLOGY, FEATURE_EXT_AMD_ECX))
		return;

	if (maxExtendedLeaf < CPUID_LEAF_AMD_CACHE_TOPOLOGY)
		return;

	uint8 hierarchyLevels[CPU_MAX_CACHE_LEVEL];
	int maxCacheLevel = 0;

	int currentLevel = 0;
	int cacheType;
	do {
		cpuid_info cpuid;
		get_current_cpuid(&cpuid, CPUID_LEAF_AMD_CACHE_TOPOLOGY, currentLevel);

		cacheType = cpuid.regs.eax & 0x1f;
		if (cacheType == 0)
			break;

		int cacheLevel = (cpuid.regs.eax >> 5) & 0x7;
		int coresCount = next_power_of_2(((cpuid.regs.eax >> 14) & 0x3f) + 1);
		hierarchyLevels[cacheLevel - 1]
		= coresCount * (sHierarchyMask[CPU_TOPOLOGY_SMT] + 1);
		maxCacheLevel = std::max(maxCacheLevel, cacheLevel);

		currentLevel++;
	} while (true);

	for (int i = 0; i < maxCacheLevel; i++)
		sCacheSharingMask[i] = ~uint32(hierarchyLevels[i] - 1);
	gCPUCacheLevelCount = maxCacheLevel;
}


// Intel CPU topology detection (Intel SDM Vol. 3A, Section 8.9)

static uint32
get_intel_cpu_initial_x2apic_id(int /* currentCPU */)
{
	cpuid_info cpuid;
	get_current_cpuid(&cpuid, 11, 0);
	return cpuid.regs.edx;
}


static inline status_t
detect_intel_cpu_topology_x2apic(uint32 maxBasicLeaf)
{
	uint32 leaf = 0;
	cpuid_info cpuid;

	// Intel SDM: Try CPUID.1Fh first, fall back to CPUID.0Bh
	if (maxBasicLeaf >= CPUID_LEAF_V2_TOPOLOGY) {
		get_current_cpuid(&cpuid, CPUID_LEAF_V2_TOPOLOGY, 0);
		if (cpuid.regs.ebx != 0)
			leaf = CPUID_LEAF_V2_TOPOLOGY;
	}

	if (maxBasicLeaf >= 0xb && leaf == 0) {
		get_current_cpuid(&cpuid, 0xb, 0);
		if (cpuid.regs.ebx != 0)
			leaf = 0xb;
	}

	if (leaf == 0)
		return B_UNSUPPORTED;

	uint8 hierarchyLevels[CPU_TOPOLOGY_LEVELS] = { 0 };

	int currentLevel = 0;
	unsigned int levelsSet = 0;
	do {
		get_current_cpuid(&cpuid, leaf, currentLevel++);
		int levelType = (cpuid.regs.ecx >> 8) & 0xff;
		int levelValue = cpuid.regs.eax & 0x1f;

		if (levelType == 0)
			break;

		switch (levelType) {
			case TOPOLOGY_LEVEL_TYPE_SMT:
				hierarchyLevels[CPU_TOPOLOGY_SMT] = levelValue;
				levelsSet |= 1;
				break;
			case TOPOLOGY_LEVEL_TYPE_CORE:
				hierarchyLevels[CPU_TOPOLOGY_CORE] = levelValue;
				levelsSet |= 2;
				break;
		}

	} while (levelsSet != 3);

	sGetCPUTopologyID = get_intel_cpu_initial_x2apic_id;

	for (int i = 1; i < CPU_TOPOLOGY_LEVELS; i++) {
		if ((levelsSet & (1u << i)) != 0)
			continue;
		hierarchyLevels[i] = hierarchyLevels[i - 1];
	}

	for (int i = 0; i < CPU_TOPOLOGY_LEVELS; i++) {
		uint32 mask = ~uint32(0);
		if (i < CPU_TOPOLOGY_LEVELS - 1)
			mask = (1u << hierarchyLevels[i]) - 1;
		if (i > 0)
			mask &= ~sHierarchyMask[i - 1];
		sHierarchyMask[i] = mask;
		sHierarchyShift[i] = i > 0 ? hierarchyLevels[i - 1] : 0;
	}

	return B_OK;
}


static inline status_t
detect_intel_cpu_topology_legacy(uint32 maxBasicLeaf)
{
	sGetCPUTopologyID = get_cpu_legacy_initial_apic_id;

	cpuid_info cpuid;

	get_current_cpuid(&cpuid, CPUID_LEAF_BASIC_INFO, 0);
	int maxLogicalID = next_power_of_2((cpuid.regs.ebx >> 16) & 0xff);

	int maxCoreID = 1;
	if (maxBasicLeaf >= CPUID_LEAF_CACHE_PARAMS) {
		get_current_cpuid(&cpuid, CPUID_LEAF_CACHE_PARAMS, 0);
		maxCoreID = next_power_of_2((cpuid.regs.eax >> 26) + 1);
	}

	compute_cpu_hierarchy_masks(maxLogicalID, maxCoreID);

	return B_OK;
}


static void
detect_intel_cache_topology(uint32 maxBasicLeaf)
{
	if (maxBasicLeaf < CPUID_LEAF_CACHE_PARAMS)
		return;

	uint8 hierarchyLevels[CPU_MAX_CACHE_LEVEL];
	int maxCacheLevel = 0;

	int currentLevel = 0;
	int cacheType;
	do {
		cpuid_info cpuid;
		get_current_cpuid(&cpuid, CPUID_LEAF_CACHE_PARAMS, currentLevel);

		cacheType = cpuid.regs.eax & 0x1f;
		if (cacheType == 0)
			break;

		int cacheLevel = (cpuid.regs.eax >> 5) & 0x7;
		hierarchyLevels[cacheLevel - 1]
		= next_power_of_2(((cpuid.regs.eax >> 14) & 0x3f) + 1);
		maxCacheLevel = std::max(maxCacheLevel, cacheLevel);

		currentLevel++;
	} while (true);

	for (int i = 0; i < maxCacheLevel; i++)
		sCacheSharingMask[i] = ~uint32(hierarchyLevels[i] - 1);

	gCPUCacheLevelCount = maxCacheLevel;
}


static uint32
get_simple_cpu_topology_id(int currentCPU)
{
	return currentCPU;
}


static inline int
get_topology_level_id(uint32 id, cpu_topology_level level)
{
	ASSERT(level < CPU_TOPOLOGY_LEVELS);
	return (id & sHierarchyMask[level]) >> sHierarchyShift[level];
}


static void
detect_cpu_topology(int currentCPU, cpu_ent* cpu, uint32 maxBasicLeaf,
					uint32 maxExtendedLeaf)
{
	if (currentCPU == 0) {
		memset(sCacheSharingMask, 0xff, sizeof(sCacheSharingMask));

		status_t result = B_UNSUPPORTED;
		if (x86_check_feature(IA32_FEATURE_HTT, FEATURE_COMMON)) {
			if (is_vendor_amd(cpu)) {
				result = detect_amd_cpu_topology(maxBasicLeaf, maxExtendedLeaf);

				if (result == B_OK)
					detect_amd_cache_topology(maxExtendedLeaf);
			}

			if (is_vendor_intel(cpu)) {
				result = detect_intel_cpu_topology_x2apic(maxBasicLeaf);
				if (result != B_OK)
					result = detect_intel_cpu_topology_legacy(maxBasicLeaf);

				if (result == B_OK)
					detect_intel_cache_topology(maxBasicLeaf);
			}
		}

		if (result != B_OK) {
			dprintf("No CPU topology information available.\n");

			sGetCPUTopologyID = get_simple_cpu_topology_id;

			sHierarchyMask[CPU_TOPOLOGY_PACKAGE] = ~uint32(0);
		}
	}

	ASSERT(sGetCPUTopologyID != NULL);
	int topologyID = sGetCPUTopologyID(currentCPU);
	cpu->topology_id[CPU_TOPOLOGY_SMT]
	= get_topology_level_id(topologyID, CPU_TOPOLOGY_SMT);
	cpu->topology_id[CPU_TOPOLOGY_CORE]
	= get_topology_level_id(topologyID, CPU_TOPOLOGY_CORE);
	cpu->topology_id[CPU_TOPOLOGY_PACKAGE]
	= get_topology_level_id(topologyID, CPU_TOPOLOGY_PACKAGE);

	unsigned int i;
	for (i = 0; i < gCPUCacheLevelCount; i++)
		cpu->cache_id[i] = topologyID & sCacheSharingMask[i];
	for (; i < CPU_MAX_CACHE_LEVEL; i++)
		cpu->cache_id[i] = -1;

	#if DUMP_CPU_TOPOLOGY
	dprintf("CPU %d: apic id %d, package %d, core %d, smt %d\n", currentCPU,
			topologyID, cpu->topology_id[CPU_TOPOLOGY_PACKAGE],
		 cpu->topology_id[CPU_TOPOLOGY_CORE],
		 cpu->topology_id[CPU_TOPOLOGY_SMT]);

	if (gCPUCacheLevelCount > 0) {
		char cacheLevels[256];
		unsigned int offset = 0;
		for (i = 0; i < gCPUCacheLevelCount; i++) {
			offset += snprintf(cacheLevels + offset,
							   sizeof(cacheLevels) - offset,
							   " L%d id %d%s", i + 1, cpu->cache_id[i],
					  i < gCPUCacheLevelCount - 1 ? "," : "");

			if (offset >= sizeof(cacheLevels))
				break;
		}

		dprintf("CPU %d: cache sharing:%s\n", currentCPU, cacheLevels);
	}
	#endif
}


// Microcode update (Intel SDM Vol. 3A Chapter 9, AMD APM Vol. 2 Section 2.8)

static void
detect_intel_patch_level(cpu_ent* cpu)
{
	if (cpu->arch.feature[FEATURE_EXT] & IA32_FEATURE_EXT_HYPERVISOR) {
		cpu->arch.patch_level = 0;
		return;
	}

	// Intel SDM: Write 0 to MSR, execute CPUID, then read MSR
	x86_write_msr(IA32_MSR_UCODE_REV, 0);
	cpuid_info cpuid;
	get_current_cpuid(&cpuid, CPUID_LEAF_BASIC_INFO, 0);

	uint64 value = x86_read_msr(IA32_MSR_UCODE_REV);
	cpu->arch.patch_level = value >> 32;
}


static void
detect_amd_patch_level(cpu_ent* cpu)
{
	if (cpu->arch.feature[FEATURE_EXT] & IA32_FEATURE_EXT_HYPERVISOR) {
		cpu->arch.patch_level = 0;
		return;
	}

	// AMD APM: Patch level in low 32 bits
	uint64 value = x86_read_msr(IA32_MSR_UCODE_REV);
	cpu->arch.patch_level = (uint32)value;
}


static struct intel_microcode_header*
find_microcode_intel(addr_t data, size_t size, uint32 patchLevel)
{
	cpuid_info cpuid;
	get_current_cpuid(&cpuid, CPUID_LEAF_BASIC_INFO, 0);
	uint32 signature = cpuid.regs.eax;

	// Intel SDM: Platform ID in bits 52:50
	uint64 platformBits = (x86_read_msr(IA32_MSR_PLATFORM_ID) >> 50) & 0x7;
	uint64 mask = 1 << platformBits;

	while (size > 0) {
		if (size < sizeof(struct intel_microcode_header)) {
			dprintf("find_microcode_intel: buffer too small for header\n");
			break;
		}
		struct intel_microcode_header* header =
		(struct intel_microcode_header*)data;

		uint32 totalSize = header->total_size;
		uint32 dataSize = header->data_size;
		if (dataSize == 0) {
			dataSize = MICROCODE_DEFAULT_SIZE;
			totalSize = sizeof(struct intel_microcode_header) + dataSize;
		}
		if (totalSize > size) {
			dprintf("find_microcode_intel: update larger than buffer\n");
			break;
		}

		size -= totalSize;
		data += totalSize;

		// Intel SDM: Validate header fields
		if (header->loader_revision != MICROCODE_LOADER_REVISION) {
			dprintf("find_microcode_intel: invalid loader version\n");
			continue;
		}

		if (((addr_t)header % MICROCODE_HEADER_ALIGNMENT) != 0) {
			dprintf("find_microcode_intel: misaligned header\n");
			continue;
		}

		// Intel SDM: Checksum validation
		uint32* dwords = (uint32*)header;
		uint32 sum = 0;
		for (uint32 i = 0; i < totalSize / 4; i++)
			sum += dwords[i];

		if (sum != 0) {
			dprintf("find_microcode_intel: checksum mismatch\n");
			continue;
		}

		if (patchLevel >= header->update_revision)
			continue;

		// Check main signature match
		if (signature == header->processor_signature
			&& (mask & header->processor_flags) != 0) {
			return header;
			}

			// Check extended signatures
			if (totalSize <= (sizeof(struct intel_microcode_header) + dataSize
				+ sizeof(struct intel_microcode_extended_signature_header))) {
				continue;
				}

				struct intel_microcode_extended_signature_header* extSigHeader =
				(struct intel_microcode_extended_signature_header*)((addr_t)header
				+ sizeof(struct intel_microcode_header) + dataSize);
			struct intel_microcode_extended_signature* extended_signature =
			(struct intel_microcode_extended_signature*)((addr_t)extSigHeader
			+ sizeof(struct intel_microcode_extended_signature_header));
			for (uint32 i = 0; i < extSigHeader->extended_signature_count; i++) {
				if (signature == extended_signature[i].processor_signature
					&& (mask & extended_signature[i].processor_flags) != 0)
					return header;
			}
	}
	return NULL;
}


static void
load_microcode_intel(int currentCPU, cpu_ent* cpu)
{
	if (currentCPU != 0)
		acquire_spinlock(&sUcodeUpdateLock);

	detect_intel_patch_level(cpu);
	uint32 revision = cpu->arch.patch_level;

	struct intel_microcode_header* update =
	(struct intel_microcode_header*)sLoadedUcodeUpdate;
	if (update == NULL) {
		update = find_microcode_intel((addr_t)sUcodeData, sUcodeDataSize,
									  revision);
	}

	if (update == NULL) {
		dprintf("CPU %d: no microcode update found\n", currentCPU);
	} else if (update->update_revision != revision) {
		addr_t updateData = (addr_t)update + sizeof(struct intel_microcode_header);

		// Intel SDM: Cache writeback before update
		wbinvd();
		memory_write_barrier();

		x86_write_msr(IA32_MSR_UCODE_WRITE, updateData);

		detect_intel_patch_level(cpu);

		if (revision == cpu->arch.patch_level) {
			dprintf("CPU %d: microcode update failed\n", currentCPU);
		} else {
			if (sLoadedUcodeUpdate == NULL)
				sLoadedUcodeUpdate = update;
			dprintf("CPU %d: microcode updated 0x%" B_PRIx32 " -> 0x%"
			B_PRIx32 "\n", currentCPU, revision, cpu->arch.patch_level);
		}
	}

	if (currentCPU != 0)
		release_spinlock(&sUcodeUpdateLock);
}


static struct amd_microcode_header*
find_microcode_amd(addr_t data, size_t size, uint32 patchLevel)
{
	cpuid_info cpuid;
	get_current_cpuid(&cpuid, CPUID_LEAF_BASIC_INFO, 0);
	uint32 signature = cpuid.regs.eax;

	if (size < sizeof(struct amd_container_header)) {
		dprintf("find_microcode_amd: buffer too small\n");
		return NULL;
	}

	struct amd_container_header* container =
	(struct amd_container_header*)data;
	if (container->magic != 0x414d44) {
		dprintf("find_microcode_amd: invalid magic\n");
		return NULL;
	}

	size -= sizeof(*container);
	data += sizeof(*container);

	struct amd_section_header* section = (struct amd_section_header*)data;
	if (section->type != 0 || section->size == 0) {
		dprintf("find_microcode_amd: invalid first section\n");
		return NULL;
	}

	size -= sizeof(*section);
	data += sizeof(*section);

	amd_equiv_cpu_entry* table = (amd_equiv_cpu_entry*)data;
	size -= section->size;
	data += section->size;

	uint16 equiv_id = 0;
	// AMD APM: Prevent infinite loop on malformed tables
	for (uint32 i = 0; table[i].installed_cpu != 0 && i < MICROCODE_MAX_TABLE_ENTRIES; i++) {
		if (signature == table[i].equiv_cpu) {
			equiv_id = table[i].equiv_cpu;
			dprintf("find_microcode_amd: equiv cpu %x\n", equiv_id);
			break;
		}
	}

	if (equiv_id == 0) {
		dprintf("find_microcode_amd: cpu not in equiv table\n");
		return NULL;
	}

	while (size > sizeof(amd_section_header)) {
		struct amd_section_header* section = (struct amd_section_header*)data;
		size -= sizeof(*section);
		data += sizeof(*section);

		if (section->type != 1 || section->size > size
			|| section->size < sizeof(amd_microcode_header)) {
			dprintf("find_microcode_amd: invalid firmware section\n");
		return NULL;
			}

			struct amd_microcode_header* header = (struct amd_microcode_header*)data;
			size -= section->size;
			data += section->size;

			if (header->processor_rev_id != equiv_id)
				continue;

		if (patchLevel >= header->patch_id)
			continue;

		if (header->nb_dev_id != 0 || header->sb_dev_id != 0) {
			dprintf("find_microcode_amd: chipset-specific update\n");
			continue;
		}

		if (((addr_t)header % MICROCODE_HEADER_ALIGNMENT) != 0) {
			dprintf("find_microcode_amd: misaligned header\n");
			continue;
		}

		return header;
	}

	dprintf("find_microcode_amd: no matching update\n");
	return NULL;
}


static void
load_microcode_amd(int currentCPU, cpu_ent* cpu)
{
	if (currentCPU != 0)
		acquire_spinlock(&sUcodeUpdateLock);

	detect_amd_patch_level(cpu);
	uint32 revision = cpu->arch.patch_level;

	struct amd_microcode_header* update =
	(struct amd_microcode_header*)sLoadedUcodeUpdate;
	if (update == NULL) {
		update = find_microcode_amd((addr_t)sUcodeData, sUcodeDataSize,
									revision);
	}

	if (update != NULL) {
		addr_t updateData = (addr_t)update;

		// AMD APM: Cache writeback before update
		wbinvd();
		memory_write_barrier();

		x86_write_msr(MSR_K8_UCODE_UPDATE, updateData);

		detect_amd_patch_level(cpu);
		if (revision == cpu->arch.patch_level) {
			dprintf("CPU %d: microcode update failed\n", currentCPU);
		} else {
			if (sLoadedUcodeUpdate == NULL)
				sLoadedUcodeUpdate = update;
			dprintf("CPU %d: microcode updated 0x%" B_PRIx32 " -> 0x%"
			B_PRIx32 "\n", currentCPU, revision, cpu->arch.patch_level);
		}
	} else {
		dprintf("CPU %d: no microcode update found\n", currentCPU);
	}

	if (currentCPU != 0)
		release_spinlock(&sUcodeUpdateLock);
}


static void
load_microcode(int currentCPU)
{
	if (sUcodeData == NULL)
		return;

	cpu_ent* cpu = get_cpu_struct();
	if ((cpu->arch.feature[FEATURE_EXT] & IA32_FEATURE_EXT_HYPERVISOR) != 0)
		return;

	if (is_vendor_intel(cpu))
		load_microcode_intel(currentCPU, cpu);
	else if (is_vendor_amd(cpu))
		load_microcode_amd(currentCPU, cpu);
}


// Hybrid CPU type detection (Intel SDM Vol. 3A, Section 8.9.1)

static uint8
get_hybrid_cpu_type()
{
	cpu_ent* cpu = get_cpu_struct();
	if ((cpu->arch.feature[FEATURE_7_EDX] & IA32_FEATURE_HYBRID_CPU) == 0)
		return 0;

	static const int X86_HYBRID_CPU_TYPE_ID_SHIFT = 24;
	cpuid_info cpuid;
	get_current_cpuid(&cpuid, CPUID_LEAF_HYBRID_INFO, 0);
	return cpuid.regs.eax >> X86_HYBRID_CPU_TYPE_ID_SHIFT;
}


static const char*
get_hybrid_cpu_type_string(uint8 type)
{
	switch (type) {
		case 0x20:
			return "Atom";
		case 0x40:
			return "Core";
		default:
			return "";
	}
}


// CPU detection and initialization

static void
detect_cpu(int currentCPU, bool full = true)
{
	cpu_ent* cpu = get_cpu_struct();
	char vendorString[17];
	cpuid_info cpuid;

	cpu->arch.vendor = VENDOR_UNKNOWN;
	cpu->arch.vendor_name = "UNKNOWN VENDOR";
	cpu->arch.feature[FEATURE_COMMON] = 0;
	cpu->arch.feature[FEATURE_EXT] = 0;
	cpu->arch.feature[FEATURE_EXT_AMD] = 0;
	cpu->arch.feature[FEATURE_7_EBX] = 0;
	cpu->arch.feature[FEATURE_7_ECX] = 0;
	cpu->arch.feature[FEATURE_7_EDX] = 0;
	cpu->arch.feature[FEATURE_D_1_EAX] = 0;
	cpu->arch.model_name[0] = 0;

	get_current_cpuid(&cpuid, 0, 0);
	uint32 maxBasicLeaf = cpuid.eax_0.max_eax;

	memset(vendorString, 0, sizeof(vendorString));
	memcpy(vendorString, cpuid.eax_0.vendor_id, sizeof(cpuid.eax_0.vendor_id));

	get_current_cpuid(&cpuid, CPUID_LEAF_BASIC_INFO, 0);
	cpu->arch.type = cpuid.eax_1.type;
	cpu->arch.family = cpuid.eax_1.family;
	cpu->arch.extended_family = cpuid.eax_1.extended_family;
	cpu->arch.model = cpuid.eax_1.model;
	cpu->arch.extended_model = cpuid.eax_1.extended_model;
	cpu->arch.stepping = cpuid.eax_1.stepping;

	if (full) {
		dprintf("CPU %d: type %d family %d ext_family %d model %d "
		"ext_model %d stepping %d, vendor '%s'\n",
		currentCPU, cpu->arch.type, cpu->arch.family,
		cpu->arch.extended_family, cpu->arch.model,
		cpu->arch.extended_model, cpu->arch.stepping, vendorString);
	}

	for (int32 i = 0; i < VENDOR_NUM; i++) {
		if (vendor_info[i].ident_string[0]
			&& !strcmp(vendorString, vendor_info[i].ident_string[0])) {
			cpu->arch.vendor = (x86_vendors)i;
		cpu->arch.vendor_name = vendor_info[i].vendor;
		break;
			}
			if (vendor_info[i].ident_string[1]
				&& !strcmp(vendorString, vendor_info[i].ident_string[1])) {
				cpu->arch.vendor = (x86_vendors)i;
			cpu->arch.vendor_name = vendor_info[i].vendor;
			break;
				}
	}

	get_current_cpuid(&cpuid, 0x80000000, 0);
	uint32 maxExtendedLeaf = cpuid.eax_0.max_eax;

	if (maxExtendedLeaf >= CPUID_LEAF_AMD_BRAND_3) {
		unsigned int temp;
		memset(cpu->arch.model_name, 0, sizeof(cpu->arch.model_name));

		get_current_cpuid(&cpuid, CPUID_LEAF_AMD_BRAND_1, 0);
		temp = cpuid.regs.edx;
		cpuid.regs.edx = cpuid.regs.ecx;
		cpuid.regs.ecx = temp;
		memcpy(cpu->arch.model_name, cpuid.as_chars, sizeof(cpuid.as_chars));

		get_current_cpuid(&cpuid, CPUID_LEAF_AMD_BRAND_2, 0);
		temp = cpuid.regs.edx;
		cpuid.regs.edx = cpuid.regs.ecx;
		cpuid.regs.ecx = temp;
		memcpy(cpu->arch.model_name + 16, cpuid.as_chars,
			   sizeof(cpuid.as_chars));

		get_current_cpuid(&cpuid, CPUID_LEAF_AMD_BRAND_3, 0);
		temp = cpuid.regs.edx;
		cpuid.regs.edx = cpuid.regs.ecx;
		cpuid.regs.ecx = temp;
		memcpy(cpu->arch.model_name + 32, cpuid.as_chars,
			   sizeof(cpuid.as_chars));

		int32 i = 0;
		while (cpu->arch.model_name[i] == ' ')
			i++;
		if (i > 0) {
			memmove(cpu->arch.model_name, &cpu->arch.model_name[i],
					strlen(&cpu->arch.model_name[i]) + 1);
		}

		if (full) {
			dprintf("CPU %d: '%s' '%s'\n",
					currentCPU, cpu->arch.vendor_name, cpu->arch.model_name);
		}
	} else {
		strlcpy(cpu->arch.model_name, "unknown", sizeof(cpu->arch.model_name));
	}

	get_current_cpuid(&cpuid, CPUID_LEAF_BASIC_INFO, 0);
	cpu->arch.feature[FEATURE_COMMON] = cpuid.eax_1.features;
	cpu->arch.feature[FEATURE_EXT] = cpuid.eax_1.extended_features;

	if (!full)
		return;

	if (maxExtendedLeaf >= CPUID_LEAF_AMD_EXTENDED) {
		get_current_cpuid(&cpuid, CPUID_LEAF_AMD_EXTENDED, 0);
		if (is_vendor_amd(cpu))
			cpu->arch.feature[FEATURE_EXT_AMD_ECX] = cpuid.regs.ecx;
		cpu->arch.feature[FEATURE_EXT_AMD] = cpuid.regs.edx;
		if (!is_vendor_amd(cpu))
			cpu->arch.feature[FEATURE_EXT_AMD] &= IA32_FEATURES_INTEL_EXT;
	}

	if (maxBasicLeaf >= CPUID_LEAF_POWER_MANAGEMENT) {
		get_current_cpuid(&cpuid, CPUID_LEAF_POWER_MANAGEMENT, 0);
		cpu->arch.feature[FEATURE_6_EAX] = cpuid.regs.eax;
		cpu->arch.feature[FEATURE_6_ECX] = cpuid.regs.ecx;
	}

	if (maxBasicLeaf >= CPUID_LEAF_EXTENDED_FEATURES) {
		get_current_cpuid(&cpuid, CPUID_LEAF_EXTENDED_FEATURES, 0);
		cpu->arch.feature[FEATURE_7_EBX] = cpuid.regs.ebx;
		cpu->arch.feature[FEATURE_7_ECX] = cpuid.regs.ecx;
		cpu->arch.feature[FEATURE_7_EDX] = cpuid.regs.edx;
	}

	if (maxBasicLeaf >= CPUID_LEAF_EXTENDED_STATE) {
		get_current_cpuid(&cpuid, CPUID_LEAF_EXTENDED_STATE, 1);
		cpu->arch.feature[FEATURE_D_1_EAX] = cpuid.regs.eax;
	}

	if (maxExtendedLeaf >= CPUID_LEAF_AMD_POWER) {
		get_current_cpuid(&cpuid, CPUID_LEAF_AMD_POWER, 0);
		cpu->arch.feature[FEATURE_EXT_7_EDX] = cpuid.regs.edx;
	}

	if (maxExtendedLeaf >= CPUID_LEAF_AMD_TOPOLOGY) {
		get_current_cpuid(&cpuid, CPUID_LEAF_AMD_TOPOLOGY, 0);
		cpu->arch.feature[FEATURE_EXT_8_EBX] = cpuid.regs.ebx;
	}

	detect_cpu_topology(currentCPU, cpu, maxBasicLeaf, maxExtendedLeaf);

	if (is_vendor_intel(cpu))
		detect_intel_patch_level(cpu);
	else if (is_vendor_amd(cpu))
		detect_amd_patch_level(cpu);

	cpu->arch.hybrid_type = get_hybrid_cpu_type();

	#if DUMP_FEATURE_STRING
	dump_feature_string(currentCPU, cpu);
	#endif

	#if DUMP_CPU_PATCHLEVEL_TYPE
	dprintf("CPU %d: patch_level 0x%" B_PRIx32 "%s%s\n", currentCPU,
			cpu->arch.patch_level,
		 cpu->arch.hybrid_type != 0 ? ", hybrid ": "",
		 get_hybrid_cpu_type_string(cpu->arch.hybrid_type));
	#endif
}


bool
x86_check_feature(uint32 feature, enum x86_feature_type type)
{
	cpu_ent* cpu = get_cpu_struct();
	return (cpu->arch.feature[type] & feature) != 0;
}


bool
x86_use_pat()
{
	return sUsePAT;
}


void*
x86_get_double_fault_stack(int32 cpu, size_t* _size)
{
	*_size = kDoubleFaultStackSize;
	return (void*)(sDoubleFaultStacks + kDoubleFaultStackSize * cpu);
}


int32
x86_double_fault_get_cpu()
{
	addr_t stack = x86_get_stack_frame();
	int32 cpu = (stack - sDoubleFaultStacks) / kDoubleFaultStackSize;
	if (cpu < 0 || cpu >= smp_get_num_cpus())
		return -1;
	return cpu;
}


// Architecture-specific initialization

status_t
arch_cpu_preboot_init_percpu(kernel_args* args, int cpu)
{
	if (cpu == 0) {
		sDoubleFaultStacks = vm_allocate_early(args,
											   kDoubleFaultStackSize * smp_get_num_cpus(), 0, 0, 0);
	}

	// TSC synchronization for SMP
	if (smp_get_num_cpus() > 1) {
		if (cpu == 0)
			sTSCSyncRendezvous = smp_get_num_cpus() - 1;

		while (sTSCSyncRendezvous != cpu) {
		}

		sTSCSyncRendezvous = cpu - 1;

		while (sTSCSyncRendezvous != -1) {
		}

		x86_write_msr(IA32_MSR_TSC, 0);
	}

	x86_descriptors_preboot_init_percpu(args, cpu);

	return B_OK;
}


static void
halt_idle(void)
{
	asm("hlt");
}


// AMD BKDG: Disable C1E to prevent TSC drift
static void
amdc1e_noarat_idle(void)
{
	uint64 msr = x86_read_msr(K8_MSR_IPM);
	if (msr & K8_CMPHALT)
		x86_write_msr(K8_MSR_IPM, msr & ~K8_CMPHALT);
	halt_idle();
}


static bool
detect_amdc1e_noarat()
{
	cpu_ent* cpu = get_cpu_struct();

	if (!is_vendor_amd(cpu))
		return false;

	// AMD Family 12h+ support ARAT, <0Fh don't support C1E
	uint32 family = compute_cpu_family(cpu);
	uint32 model = compute_cpu_model(cpu);
	return (family < 0x12 && family > 0xf) || (family == 0xf && model > 0x40);
}


// TSC frequency detection

static void
init_tsc_with_cpuid(kernel_args* args, uint32* conversionFactor)
{
	cpu_ent* cpu = get_cpu_struct();
	if (!is_vendor_intel(cpu))
		return;

	uint32 model = compute_cpu_model(cpu);
	cpuid_info cpuid;
	get_current_cpuid(&cpuid, 0, 0);
	uint32 maxBasicLeaf = cpuid.eax_0.max_eax;

	if (maxBasicLeaf < IA32_CPUID_LEAF_TSC)
		return;

	get_current_cpuid(&cpuid, IA32_CPUID_LEAF_TSC, 0);
	if (cpuid.regs.eax == 0 || cpuid.regs.ebx == 0)
		return;

	uint32 khz = cpuid.regs.ecx / 1000;
	uint32 denominator = cpuid.regs.eax;
	uint32 numerator = cpuid.regs.ebx;

	// Intel Denverton fallback
	if (khz == 0 && model == INTEL_MODEL_DENVERTON)
		khz = 25000;

	if (khz == 0 && maxBasicLeaf >= CPUID_LEAF_FREQUENCY) {
		get_current_cpuid(&cpuid, CPUID_LEAF_FREQUENCY, 0);
		khz = cpuid.regs.eax * 1000 * denominator / numerator;
	}

	if (khz == 0)
		return;

	dprintf("CPU: TSC frequency from CPUID: %" B_PRIu32 " kHz\n", khz);
	*conversionFactor = (1000ULL << 32) / (khz * numerator / denominator);
	args->arch_args.system_time_cv_factor = *conversionFactor;
}


static void
init_tsc_with_msr(kernel_args* args, uint32* conversionFactor)
{
	cpu_ent* cpuEnt = get_cpu_struct();
	if (!is_vendor_amd(cpuEnt))
		return;

	uint32 family = compute_cpu_family(cpuEnt);
	if (family < AMD_FAMILY_10H)
		return;

	uint64 value = x86_read_msr(MSR_F10H_HWCR);
	if ((value & HWCR_TSCFREQSEL) == 0)
		return;

	value = x86_read_msr(MSR_F10H_PSTATEDEF(0));
	if ((value & PSTATEDEF_EN) == 0)
		return;

	if (family != AMD_FAMILY_17H && family != AMD_FAMILY_19H)
		return;

	uint64 khz = 200 * 1000;
	uint32 denominator = (value >> 8) & 0x3f;
	if (denominator < 0x8 || denominator > 0x2c)
		return;
	if (denominator > 0x1a && (denominator % 2) == 1)
		return;

	uint32 numerator = value & 0xff;
	if (numerator < 0x10)
		return;

	dprintf("CPU: TSC frequency from MSR: %" B_PRIu64 " kHz\n",
			khz * numerator / denominator);
	*conversionFactor = (1000ULL << 32) / (khz * numerator / denominator);
	args->arch_args.system_time_cv_factor = *conversionFactor;
}


static void
init_tsc(kernel_args* args)
{
	uint32 conversionFactor = args->arch_args.system_time_cv_factor;
	init_tsc_with_cpuid(args, &conversionFactor);
	init_tsc_with_msr(args, &conversionFactor);
	uint64 conversionFactorNsecs = (uint64)conversionFactor * 1000;

	#ifdef __x86_64__
	__x86_setup_system_time((uint64)conversionFactor << 32,
							conversionFactorNsecs);
	#else
	if (conversionFactorNsecs >> 32 != 0) {
		__x86_setup_system_time(conversionFactor, conversionFactorNsecs >> 16,
								true);
	} else {
		__x86_setup_system_time(conversionFactor, conversionFactorNsecs, false);
	}
	#endif
}


status_t
arch_cpu_init_percpu(kernel_args* args, int cpu)
{
	detect_cpu(cpu, false);
	load_microcode(cpu);
	detect_cpu(cpu);

	if (cpu == 0)
		init_tsc(args);

	if (!gCpuIdleFunc) {
		if (detect_amdc1e_noarat())
			gCpuIdleFunc = amdc1e_noarat_idle;
		else
			gCpuIdleFunc = halt_idle;
	}

	if (x86_check_feature(IA32_FEATURE_MCE, FEATURE_COMMON))
		x86_write_cr4(x86_read_cr4() | IA32_CR4_MCE);

	cpu_ent* cpuEnt = get_cpu_struct();
	if (cpu == 0) {
		bool supportsPAT = x86_check_feature(IA32_FEATURE_PAT, FEATURE_COMMON);
		bool brokenPAT = has_broken_pat(cpuEnt);

		sUsePAT = supportsPAT && !brokenPAT
		&& !get_safemode_boolean_early(args, B_SAFEMODE_DISABLE_PAT, false);

		if (sUsePAT) {
			dprintf("using PAT for memory types\n");
		} else {
			dprintf("not using PAT (%s)\n",
					supportsPAT ? (brokenPAT ? "broken" : "disabled")
					: "unsupported");
		}
	}

	if (sUsePAT)
		init_pat(cpu);

	#ifdef __x86_64__
	// Set CPU number in TSC_AUX for RDTSCP/RDPID
	if (x86_check_feature(IA32_FEATURE_AMD_EXT_RDTSCP, FEATURE_EXT_AMD)
		|| x86_check_feature(IA32_FEATURE_RDPID, FEATURE_7_ECX)) {
		x86_write_msr(IA32_MSR_TSC_AUX, cpu);
		}

		// AMD APM: Make LFENCE dispatch-serializing
		if (is_vendor_amd(cpuEnt)) {
			uint32 family = compute_cpu_family(cpuEnt);
			if (family >= AMD_FAMILY_10H && family != AMD_FAMILY_11H) {
				uint64 value = x86_read_msr(MSR_F10H_DE_CFG);
				if ((value & DE_CFG_SERIALIZE_LFENCE) == 0)
					x86_write_msr(MSR_F10H_DE_CFG, value | DE_CFG_SERIALIZE_LFENCE);
			}
		}
		#endif

		if (x86_check_feature(IA32_FEATURE_APERFMPERF, FEATURE_6_ECX)) {
			gCPU[cpu].arch.mperf_prev = x86_read_msr(IA32_MSR_MPERF);
			gCPU[cpu].arch.aperf_prev = x86_read_msr(IA32_MSR_APERF);
			gCPU[cpu].arch.frequency = 0;
			gCPU[cpu].arch.perf_timestamp = 0;
		}

		return __x86_patch_errata_percpu(cpu);
}


status_t
arch_cpu_init(kernel_args* args)
{
	if (args->ucode_data != NULL && args->ucode_data_size > 0) {
		sUcodeData = args->ucode_data;
		sUcodeDataSize = args->ucode_data_size;
	} else {
		dprintf("CPU: no microcode provided\n");
	}

	x86_descriptors_init(args);

	return B_OK;
}


#ifdef __x86_64__
static void
enable_smap(void* dummy, int cpu)
{
	x86_write_cr4(x86_read_cr4() | IA32_CR4_SMAP);
}


static void
enable_smep(void* dummy, int cpu)
{
	x86_write_cr4(x86_read_cr4() | IA32_CR4_SMEP);
}


static void
enable_osxsave(void* dummy, int cpu)
{
	x86_write_cr4(x86_read_cr4() | IA32_CR4_OSXSAVE);
}


static void
enable_xsavemask(void* dummy, int cpu)
{
	xsetbv(0, gXsaveMask);
}
#endif


status_t
arch_cpu_init_post_vm(kernel_args* args)
{
	area_id stacks = create_area("double fault stacks",
								 (void**)&sDoubleFaultStacks, B_EXACT_ADDRESS,
								 kDoubleFaultStackSize * smp_get_num_cpus(),
								 B_FULL_LOCK, B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA);
	if (stacks < B_OK)
		panic("failed to create double fault stacks: %" B_PRId32, stacks);

	X86PagingStructures* kernelPagingStructures
	= static_cast<X86VMTranslationMap*>(
		VMAddressSpace::Kernel()->TranslationMap())->PagingStructures();

		for (uint32 i = 0; i < args->num_cpus; i++) {
			gCPU[i].arch.active_paging_structures = kernelPagingStructures;
			kernelPagingStructures->AddReference();
		}

		if (!apic_available())
			x86_init_fpu();

	#ifdef __x86_64__
	// Intel SDM: Enable SMEP (Supervisor Mode Execution Protection)
	if (x86_check_feature(IA32_FEATURE_SMEP, FEATURE_7_EBX)) {
		if (!get_safemode_boolean(B_SAFEMODE_DISABLE_SMEP_SMAP, false)) {
			dprintf("enable SMEP\n");
			call_all_cpus_sync(&enable_smep, NULL);
		} else
			dprintf("SMEP disabled per safemode\n");
	}

	// Intel SDM: Enable SMAP (Supervisor Mode Access Protection)
	if (x86_check_feature(IA32_FEATURE_SMAP, FEATURE_7_EBX)) {
		if (!get_safemode_boolean(B_SAFEMODE_DISABLE_SMEP_SMAP, false)) {
			dprintf("enable SMAP\n");
			call_all_cpus_sync(&enable_smap, NULL);

			arch_altcodepatch_replace(ALTCODEPATCH_TAG_STAC, &_stac, 3);
			arch_altcodepatch_replace(ALTCODEPATCH_TAG_CLAC, &_clac, 3);
		} else
			dprintf("SMAP disabled per safemode\n");
	}

	gHasXsave = x86_check_feature(IA32_FEATURE_EXT_XSAVE, FEATURE_EXT);
	if (gHasXsave) {
		gHasXsavec = x86_check_feature(IA32_FEATURE_XSAVEC, FEATURE_D_1_EAX);

		call_all_cpus_sync(&enable_osxsave, NULL);
		gXsaveMask = IA32_XCR0_X87 | IA32_XCR0_SSE;

		cpuid_info cpuid;
		get_current_cpuid(&cpuid, CPUID_LEAF_EXTENDED_STATE, 0);
		gXsaveMask |= (cpuid.regs.eax & IA32_XCR0_AVX);
		call_all_cpus_sync(&enable_xsavemask, NULL);

		get_current_cpuid(&cpuid, CPUID_LEAF_EXTENDED_STATE, 0);
		gFPUSaveLength = cpuid.regs.ebx;
		if (gFPUSaveLength > sizeof(((struct arch_thread *)0)->user_fpu_state))
			gFPUSaveLength = 832;

		arch_altcodepatch_replace(ALTCODEPATCH_TAG_XSAVE,
								  gHasXsavec ? &_xsavec : &_xsave, 4);
		arch_altcodepatch_replace(ALTCODEPATCH_TAG_XRSTOR, &_xrstor, 4);

		dprintf("enable %s mask 0x%" B_PRIx64 " len %" B_PRId64 "\n",
				gHasXsavec ? "XSAVEC" : "XSAVE", gXsaveMask, gFPUSaveLength);
	}
	#endif

	return B_OK;
}


status_t
arch_cpu_init_post_modules(kernel_args* args)
{
	void* cookie = open_module_list("cpu");

	while (true) {
		char name[B_FILE_NAME_LENGTH];
		size_t nameLength = sizeof(name);

		if (read_next_module_name(cookie, name, &nameLength) != B_OK
			|| get_module(name, (module_info**)&sCpuModule) == B_OK)
			break;
	}

	close_module_list(cookie);

	if (x86_count_mtrrs() > 0) {
		sCpuRendezvous = sCpuRendezvous2 = 0;
		call_all_cpus(&init_mtrrs, NULL);
	}

	size_t threadExitLen = (addr_t)x86_end_userspace_thread_exit
	- (addr_t)x86_userspace_thread_exit;
	addr_t threadExitPosition = fill_commpage_entry(
		COMMPAGE_ENTRY_X86_THREAD_EXIT, (const void*)x86_userspace_thread_exit,
													threadExitLen);

	image_id image = get_commpage_image();

	elf_add_memory_image_symbol(image, "commpage_thread_exit",
								threadExitPosition, threadExitLen, B_SYMBOL_TYPE_TEXT);

	return B_OK;
}


void
arch_cpu_user_TLB_invalidate(void)
{
	x86_write_cr3(x86_read_cr3());
}


void
arch_cpu_global_TLB_invalidate(void)
{
	uint32 flags = x86_read_cr4();

	if (flags & IA32_CR4_GLOBAL_PAGES) {
		// Intel SDM: Flush global TLB entries
		x86_write_cr4(flags & ~IA32_CR4_GLOBAL_PAGES);
		x86_write_cr4(flags | IA32_CR4_GLOBAL_PAGES);
	} else {
		cpu_status state = disable_interrupts();
		arch_cpu_user_TLB_invalidate();
		restore_interrupts(state);
	}
}


void
arch_cpu_invalidate_TLB_range(addr_t start, addr_t end)
{
	int32 num_pages = end / B_PAGE_SIZE - start / B_PAGE_SIZE;
	while (num_pages-- >= 0) {
		invalidate_TLB(start);
		start += B_PAGE_SIZE;
	}
}


void
arch_cpu_invalidate_TLB_list(addr_t pages[], int num_pages)
{
	for (int i = 0; i < num_pages; i++) {
		invalidate_TLB(pages[i]);
	}
}


status_t
arch_cpu_shutdown(bool rebootSystem)
{
	if (acpi_shutdown(rebootSystem) == B_OK)
		return B_OK;

	if (!rebootSystem) {
		#ifndef __x86_64__
		return apm_shutdown();
		#else
		return B_NOT_SUPPORTED;
		#endif
	}

	cpu_status state = disable_interrupts();

	// Intel SDM: Try keyboard controller reset
	out8(0xfe, 0x64);

	snooze(500000);

	// Triple fault fallback
	x86_reboot();

	restore_interrupts(state);
	return B_ERROR;
}


void
arch_cpu_sync_icache(void* address, size_t length)
{
	// Intel SDM: x86 has coherent instruction cache
}
