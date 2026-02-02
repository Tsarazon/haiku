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


extern addr_t _stac;
extern addr_t _clac;
extern addr_t _xsave;
extern addr_t _xsavec;
extern addr_t _xrstor;
uint64 gXsaveMask;
uint64 gFPUSaveLength = 512;
bool gHasXsave = false;
bool gHasXsavec = false;

extern "C" void x86_reboot(void);
	// from arch.S

void (*gCpuIdleFunc)(void);

static uint32 sCpuRendezvous;
static uint32 sCpuRendezvous2;
static uint32 sCpuRendezvous3;
static vint32 sTSCSyncRendezvous;

static addr_t sDoubleFaultStacks = 0;
static const size_t kDoubleFaultStackSize = 4096;

static x86_cpu_module_info* sCpuModule;

static uint32 (*sGetCPUTopologyID)(int currentCPU);
static uint32 sHierarchyMask[CPU_TOPOLOGY_LEVELS];
static uint32 sHierarchyShift[CPU_TOPOLOGY_LEVELS];

static uint32 sCacheSharingMask[CPU_MAX_CACHE_LEVEL];

static void* sUcodeData = NULL;
static size_t sUcodeDataSize = 0;
static void* sLoadedUcodeUpdate;
static spinlock sUcodeUpdateLock = B_SPINLOCK_INITIALIZER;

static bool sUsePAT = false;


// #pragma mark - ACPI shutdown


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
			//cpu_status state = disable_interrupts();
			status = acpi->enter_sleep_state(ACPI_POWER_STATE_OFF);
			//restore_interrupts(state);
		}
	}

	put_module(B_ACPI_MODULE_NAME);
	return status;
}


// #pragma mark - Cache control


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


// #pragma mark - MTRR


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


// #pragma mark - PAT


static void
init_pat(int cpu)
{
	disable_caches();

	uint64 value = x86_read_msr(IA32_MSR_PAT);
	dprintf("PAT MSR on CPU %d before init: %#" B_PRIx64 "\n", cpu, value);

	value &= ~(IA32_MSR_PAT_ENTRY_MASK << IA32_MSR_PAT_ENTRY_SHIFT(4));
	value |= IA32_MSR_PAT_TYPE_WRITE_COMBINING << IA32_MSR_PAT_ENTRY_SHIFT(4);

	dprintf("PAT MSR on CPU %d after init: %#" B_PRIx64 "\n", cpu, value);
	x86_write_msr(IA32_MSR_PAT, value);

	enable_caches();
}


// #pragma mark - FPU


void
x86_init_fpu(void)
{
	dprintf("%s: CPU has SSE... enabling FXSR and XMM.\n", __func__);
}


// #pragma mark - CPU feature dump


#if DUMP_FEATURE_STRING

struct feature_entry {
	uint32				flag;
	enum x86_feature_type	type;
	const char*			name;
};

static const feature_entry kFeatureTable[] = {
	// FEATURE_COMMON (CPUID.1 EDX)
	{ IA32_FEATURE_FPU,		FEATURE_COMMON,		"fpu " },
	{ IA32_FEATURE_VME,		FEATURE_COMMON,		"vme " },
	{ IA32_FEATURE_DE,		FEATURE_COMMON,		"de " },
	{ IA32_FEATURE_PSE,		FEATURE_COMMON,		"pse " },
	{ IA32_FEATURE_TSC,		FEATURE_COMMON,		"tsc " },
	{ IA32_FEATURE_MSR,		FEATURE_COMMON,		"msr " },
	{ IA32_FEATURE_PAE,		FEATURE_COMMON,		"pae " },
	{ IA32_FEATURE_MCE,		FEATURE_COMMON,		"mce " },
	{ IA32_FEATURE_CX8,		FEATURE_COMMON,		"cx8 " },
	{ IA32_FEATURE_APIC,		FEATURE_COMMON,		"apic " },
	{ IA32_FEATURE_SEP,		FEATURE_COMMON,		"sep " },
	{ IA32_FEATURE_MTRR,		FEATURE_COMMON,		"mtrr " },
	{ IA32_FEATURE_PGE,		FEATURE_COMMON,		"pge " },
	{ IA32_FEATURE_MCA,		FEATURE_COMMON,		"mca " },
	{ IA32_FEATURE_CMOV,		FEATURE_COMMON,		"cmov " },
	{ IA32_FEATURE_PAT,		FEATURE_COMMON,		"pat " },
	{ IA32_FEATURE_PSE36,		FEATURE_COMMON,		"pse36 " },
	{ IA32_FEATURE_PSN,		FEATURE_COMMON,		"psn " },
	{ IA32_FEATURE_CLFSH,		FEATURE_COMMON,		"clfsh " },
	{ IA32_FEATURE_DS,		FEATURE_COMMON,		"ds " },
	{ IA32_FEATURE_ACPI,		FEATURE_COMMON,		"acpi " },
	{ IA32_FEATURE_MMX,		FEATURE_COMMON,		"mmx " },
	{ IA32_FEATURE_FXSR,		FEATURE_COMMON,		"fxsr " },
	{ IA32_FEATURE_SSE,		FEATURE_COMMON,		"sse " },
	{ IA32_FEATURE_SSE2,		FEATURE_COMMON,		"sse2 " },
	{ IA32_FEATURE_SS,		FEATURE_COMMON,		"ss " },
	{ IA32_FEATURE_HTT,		FEATURE_COMMON,		"htt " },
	{ IA32_FEATURE_TM,		FEATURE_COMMON,		"tm " },
	{ IA32_FEATURE_PBE,		FEATURE_COMMON,		"pbe " },

	// FEATURE_EXT (CPUID.1 ECX)
	{ IA32_FEATURE_EXT_SSE3,		FEATURE_EXT,	"sse3 " },
	{ IA32_FEATURE_EXT_PCLMULQDQ,		FEATURE_EXT,	"pclmulqdq " },
	{ IA32_FEATURE_EXT_DTES64,		FEATURE_EXT,	"dtes64 " },
	{ IA32_FEATURE_EXT_MONITOR,		FEATURE_EXT,	"monitor " },
	{ IA32_FEATURE_EXT_DSCPL,		FEATURE_EXT,	"dscpl " },
	{ IA32_FEATURE_EXT_VMX,			FEATURE_EXT,	"vmx " },
	{ IA32_FEATURE_EXT_SMX,			FEATURE_EXT,	"smx " },
	{ IA32_FEATURE_EXT_EST,			FEATURE_EXT,	"est " },
	{ IA32_FEATURE_EXT_TM2,			FEATURE_EXT,	"tm2 " },
	{ IA32_FEATURE_EXT_SSSE3,		FEATURE_EXT,	"ssse3 " },
	{ IA32_FEATURE_EXT_CNXTID,		FEATURE_EXT,	"cnxtid " },
	{ IA32_FEATURE_EXT_FMA,			FEATURE_EXT,	"fma " },
	{ IA32_FEATURE_EXT_CX16,		FEATURE_EXT,	"cx16 " },
	{ IA32_FEATURE_EXT_XTPR,		FEATURE_EXT,	"xtpr " },
	{ IA32_FEATURE_EXT_PDCM,		FEATURE_EXT,	"pdcm " },
	{ IA32_FEATURE_EXT_PCID,		FEATURE_EXT,	"pcid " },
	{ IA32_FEATURE_EXT_DCA,			FEATURE_EXT,	"dca " },
	{ IA32_FEATURE_EXT_SSE4_1,		FEATURE_EXT,	"sse4_1 " },
	{ IA32_FEATURE_EXT_SSE4_2,		FEATURE_EXT,	"sse4_2 " },
	{ IA32_FEATURE_EXT_X2APIC,		FEATURE_EXT,	"x2apic " },
	{ IA32_FEATURE_EXT_MOVBE,		FEATURE_EXT,	"movbe " },
	{ IA32_FEATURE_EXT_POPCNT,		FEATURE_EXT,	"popcnt " },
	{ IA32_FEATURE_EXT_TSCDEADLINE,	FEATURE_EXT,	"tscdeadline " },
	{ IA32_FEATURE_EXT_AES,			FEATURE_EXT,	"aes " },
	{ IA32_FEATURE_EXT_XSAVE,		FEATURE_EXT,	"xsave " },
	{ IA32_FEATURE_EXT_OSXSAVE,		FEATURE_EXT,	"osxsave " },
	{ IA32_FEATURE_EXT_AVX,			FEATURE_EXT,	"avx " },
	{ IA32_FEATURE_EXT_F16C,		FEATURE_EXT,	"f16c " },
	{ IA32_FEATURE_EXT_RDRND,		FEATURE_EXT,	"rdrnd " },
	{ IA32_FEATURE_EXT_HYPERVISOR,		FEATURE_EXT,	"hypervisor " },

	// FEATURE_EXT_AMD_ECX (CPUID.80000001 ECX)
	{ IA32_FEATURE_AMD_EXT_MWAITX,	FEATURE_EXT_AMD_ECX,	"mwaitx " },

	// FEATURE_EXT_AMD (CPUID.80000001 EDX)
	{ IA32_FEATURE_AMD_EXT_SYSCALL,	FEATURE_EXT_AMD,	"syscall " },
	{ IA32_FEATURE_AMD_EXT_NX,		FEATURE_EXT_AMD,	"nx " },
	{ IA32_FEATURE_AMD_EXT_MMXEXT,		FEATURE_EXT_AMD,	"mmxext " },
	{ IA32_FEATURE_AMD_EXT_FFXSR,		FEATURE_EXT_AMD,	"ffxsr " },
	{ IA32_FEATURE_AMD_EXT_PDPE1GB,	FEATURE_EXT_AMD,	"pdpe1gb " },
	{ IA32_FEATURE_AMD_EXT_LONG,		FEATURE_EXT_AMD,	"long " },
	{ IA32_FEATURE_AMD_EXT_3DNOWEXT,	FEATURE_EXT_AMD,	"3dnowext " },
	{ IA32_FEATURE_AMD_EXT_3DNOW,		FEATURE_EXT_AMD,	"3dnow " },

	// FEATURE_6_EAX (CPUID.6 EAX)
	{ IA32_FEATURE_DTS,		FEATURE_6_EAX,		"dts " },
	{ IA32_FEATURE_ITB,		FEATURE_6_EAX,		"itb " },
	{ IA32_FEATURE_ARAT,		FEATURE_6_EAX,		"arat " },
	{ IA32_FEATURE_PLN,		FEATURE_6_EAX,		"pln " },
	{ IA32_FEATURE_ECMD,		FEATURE_6_EAX,		"ecmd " },
	{ IA32_FEATURE_PTM,		FEATURE_6_EAX,		"ptm " },
	{ IA32_FEATURE_HWP,		FEATURE_6_EAX,		"hwp " },
	{ IA32_FEATURE_HWP_NOTIFY,	FEATURE_6_EAX,		"hwp_notify " },
	{ IA32_FEATURE_HWP_ACTWIN,	FEATURE_6_EAX,		"hwp_actwin " },
	{ IA32_FEATURE_HWP_EPP,	FEATURE_6_EAX,		"hwp_epp " },
	{ IA32_FEATURE_HWP_PLR,	FEATURE_6_EAX,		"hwp_plr " },
	{ IA32_FEATURE_HDC,		FEATURE_6_EAX,		"hdc " },
	{ IA32_FEATURE_TBMT3,		FEATURE_6_EAX,		"tbmt3 " },
	{ IA32_FEATURE_HWP_CAP,	FEATURE_6_EAX,		"hwp_cap " },
	{ IA32_FEATURE_HWP_PECI,	FEATURE_6_EAX,		"hwp_peci " },
	{ IA32_FEATURE_HWP_FLEX,	FEATURE_6_EAX,		"hwp_flex " },
	{ IA32_FEATURE_HWP_FAST,	FEATURE_6_EAX,		"hwp_fast " },
	{ IA32_FEATURE_HW_FEEDBACK,	FEATURE_6_EAX,		"hw_feedback " },
	{ IA32_FEATURE_HWP_IGNIDL,	FEATURE_6_EAX,		"hwp_ignidl " },

	// FEATURE_6_ECX (CPUID.6 ECX)
	{ IA32_FEATURE_APERFMPERF,	FEATURE_6_ECX,		"aperfmperf " },
	{ IA32_FEATURE_EPB,		FEATURE_6_ECX,		"epb " },

	// FEATURE_7_EBX (CPUID.7 EBX)
	{ IA32_FEATURE_TSC_ADJUST,	FEATURE_7_EBX,		"tsc_adjust " },
	{ IA32_FEATURE_SGX,		FEATURE_7_EBX,		"sgx " },
	{ IA32_FEATURE_BMI1,		FEATURE_7_EBX,		"bmi1 " },
	{ IA32_FEATURE_HLE,		FEATURE_7_EBX,		"hle " },
	{ IA32_FEATURE_AVX2,		FEATURE_7_EBX,		"avx2 " },
	{ IA32_FEATURE_SMEP,		FEATURE_7_EBX,		"smep " },
	{ IA32_FEATURE_BMI2,		FEATURE_7_EBX,		"bmi2 " },
	{ IA32_FEATURE_ERMS,		FEATURE_7_EBX,		"erms " },
	{ IA32_FEATURE_INVPCID,		FEATURE_7_EBX,		"invpcid " },
	{ IA32_FEATURE_RTM,		FEATURE_7_EBX,		"rtm " },
	{ IA32_FEATURE_CQM,		FEATURE_7_EBX,		"cqm " },
	{ IA32_FEATURE_MPX,		FEATURE_7_EBX,		"mpx " },
	{ IA32_FEATURE_RDT_A,		FEATURE_7_EBX,		"rdt_a " },
	{ IA32_FEATURE_AVX512F,		FEATURE_7_EBX,		"avx512f " },
	{ IA32_FEATURE_AVX512DQ,	FEATURE_7_EBX,		"avx512dq " },
	{ IA32_FEATURE_RDSEED,		FEATURE_7_EBX,		"rdseed " },
	{ IA32_FEATURE_ADX,		FEATURE_7_EBX,		"adx " },
	{ IA32_FEATURE_SMAP,		FEATURE_7_EBX,		"smap " },
	{ IA32_FEATURE_AVX512IFMA,	FEATURE_7_EBX,		"avx512ifma " },
	{ IA32_FEATURE_PCOMMIT,		FEATURE_7_EBX,		"pcommit " },
	{ IA32_FEATURE_CLFLUSHOPT,	FEATURE_7_EBX,		"cflushopt " },
	{ IA32_FEATURE_CLWB,		FEATURE_7_EBX,		"clwb " },
	{ IA32_FEATURE_INTEL_PT,	FEATURE_7_EBX,		"intel_pt " },
	{ IA32_FEATURE_AVX512PF,	FEATURE_7_EBX,		"avx512pf " },
	{ IA32_FEATURE_AVX512ER,	FEATURE_7_EBX,		"avx512er " },
	{ IA32_FEATURE_AVX512CD,	FEATURE_7_EBX,		"avx512cd " },
	{ IA32_FEATURE_SHA_NI,		FEATURE_7_EBX,		"sha_ni " },
	{ IA32_FEATURE_AVX512BW,	FEATURE_7_EBX,		"avx512bw " },
	{ IA32_FEATURE_AVX512VI,	FEATURE_7_EBX,		"avx512vi " },

	// FEATURE_7_ECX (CPUID.7 ECX)
	{ IA32_FEATURE_AVX512VMBI,	FEATURE_7_ECX,		"avx512vmbi " },
	{ IA32_FEATURE_UMIP,		FEATURE_7_ECX,		"umip " },
	{ IA32_FEATURE_PKU,		FEATURE_7_ECX,		"pku " },
	{ IA32_FEATURE_OSPKE,		FEATURE_7_ECX,		"ospke " },
	{ IA32_FEATURE_WAITPKG,		FEATURE_7_ECX,		"waitpkg " },
	{ IA32_FEATURE_AVX512VMBI2,	FEATURE_7_ECX,		"avx512vmbi2 " },
	{ IA32_FEATURE_GFNI,		FEATURE_7_ECX,		"gfni " },
	{ IA32_FEATURE_VAES,		FEATURE_7_ECX,		"vaes " },
	{ IA32_FEATURE_VPCLMULQDQ,	FEATURE_7_ECX,		"vpclmulqdq " },
	{ IA32_FEATURE_AVX512_VNNI,	FEATURE_7_ECX,		"avx512vnni " },
	{ IA32_FEATURE_AVX512_BITALG,	FEATURE_7_ECX,		"avx512bitalg " },
	{ IA32_FEATURE_AVX512_VPOPCNTDQ, FEATURE_7_ECX,	"avx512vpopcntdq " },
	{ IA32_FEATURE_LA57,		FEATURE_7_ECX,		"la57 " },
	{ IA32_FEATURE_RDPID,		FEATURE_7_ECX,		"rdpid " },
	{ IA32_FEATURE_SGX_LC,		FEATURE_7_ECX,		"sgx_lc " },

	// FEATURE_7_EDX (CPUID.7 EDX)
	{ IA32_FEATURE_HYBRID_CPU,		FEATURE_7_EDX,	"hybrid " },
	{ IA32_FEATURE_IBRS,			FEATURE_7_EDX,	"ibrs " },
	{ IA32_FEATURE_STIBP,			FEATURE_7_EDX,	"stibp " },
	{ IA32_FEATURE_L1D_FLUSH,		FEATURE_7_EDX,	"l1d_flush " },
	{ IA32_FEATURE_ARCH_CAPABILITIES,	FEATURE_7_EDX,	"msr_arch " },
	{ IA32_FEATURE_SSBD,			FEATURE_7_EDX,	"ssbd " },

	// FEATURE_EXT_7_EDX (CPUID.80000007 EDX)
	{ IA32_FEATURE_AMD_HW_PSTATE,	FEATURE_EXT_7_EDX,	"hwpstate " },
	{ IA32_FEATURE_INVARIANT_TSC,	FEATURE_EXT_7_EDX,	"constant_tsc " },
	{ IA32_FEATURE_CPB,		FEATURE_EXT_7_EDX,	"cpb " },
	{ IA32_FEATURE_PROC_FEEDBACK,	FEATURE_EXT_7_EDX,	"proc_feedback " },

	// FEATURE_D_1_EAX (CPUID.D.1 EAX)
	{ IA32_FEATURE_XSAVEOPT,	FEATURE_D_1_EAX,	"xsaveopt " },
	{ IA32_FEATURE_XSAVEC,		FEATURE_D_1_EAX,	"xsavec " },
	{ IA32_FEATURE_XGETBV1,	FEATURE_D_1_EAX,	"xgetbv1 " },
	{ IA32_FEATURE_XSAVES,		FEATURE_D_1_EAX,	"xsaves " },

	// FEATURE_EXT_8_EBX (CPUID.80000008 EBX)
	{ IA32_FEATURE_CLZERO,		FEATURE_EXT_8_EBX,	"clzero " },
	{ IA32_FEATURE_IBPB,		FEATURE_EXT_8_EBX,	"ibpb " },
	{ IA32_FEATURE_AMD_SSBD,	FEATURE_EXT_8_EBX,	"amd_ssbd " },
	{ IA32_FEATURE_VIRT_SSBD,	FEATURE_EXT_8_EBX,	"virt_ssbd " },
	{ IA32_FEATURE_AMD_SSB_NO,	FEATURE_EXT_8_EBX,	"amd_ssb_no " },
	{ IA32_FEATURE_CPPC,		FEATURE_EXT_8_EBX,	"cppc " },
};

static const uint32 kFeatureTableCount = B_COUNT_OF(kFeatureTable);

static void
dump_feature_string(int currentCPU, cpu_ent* cpu)
{
	char features[768];
	features[0] = 0;

	for (uint32 i = 0; i < kFeatureTableCount; i++) {
		if (cpu->arch.feature[kFeatureTable[i].type] & kFeatureTable[i].flag)
			strlcat(features, kFeatureTable[i].name, sizeof(features));
	}

	dprintf("CPU %d: features: %s\n", currentCPU, features);
}

#endif	// DUMP_FEATURE_STRING


// #pragma mark - CPU topology


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
	get_current_cpuid(&cpuid, 1, 0);
	return cpuid.regs.ebx >> 24;
}


static inline status_t
detect_amd_cpu_topology(uint32 maxBasicLeaf, uint32 maxExtendedLeaf)
{
	sGetCPUTopologyID = get_cpu_legacy_initial_apic_id;

	cpuid_info cpuid;
	get_current_cpuid(&cpuid, 1, 0);
	int maxLogicalID = next_power_of_2((cpuid.regs.ebx >> 16) & 0xff);

	int maxCoreID = 1;
	if (maxExtendedLeaf >= 0x80000008) {
		get_current_cpuid(&cpuid, 0x80000008, 0);
		maxCoreID = (cpuid.regs.ecx >> 12) & 0xf;
		if (maxCoreID != 0)
			maxCoreID = 1 << maxCoreID;
		else
			maxCoreID = next_power_of_2((cpuid.regs.edx & 0xf) + 1);
	}

	if (maxExtendedLeaf >= 0x80000001) {
		get_current_cpuid(&cpuid, 0x80000001, 0);
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

	if (maxExtendedLeaf < 0x8000001d)
		return;

	uint8 hierarchyLevels[CPU_MAX_CACHE_LEVEL];
	int maxCacheLevel = 0;

	int currentLevel = 0;
	int cacheType;
	do {
		cpuid_info cpuid;
		get_current_cpuid(&cpuid, 0x8000001d, currentLevel);

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
	if (maxBasicLeaf >= 0x1f) {
		get_current_cpuid(&cpuid, 0x1f, 0);
		if (cpuid.regs.ebx != 0)
			leaf = 0x1f;
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
		cpuid_info cpuid;
		get_current_cpuid(&cpuid, leaf, currentLevel++);
		int levelType = (cpuid.regs.ecx >> 8) & 0xff;
		int levelValue = cpuid.regs.eax & 0x1f;

		if (levelType == 0)
			break;

		switch (levelType) {
			case 1:
				hierarchyLevels[CPU_TOPOLOGY_SMT] = levelValue;
				levelsSet |= 1;
				break;
			case 2:
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

	get_current_cpuid(&cpuid, 1, 0);
	int maxLogicalID = next_power_of_2((cpuid.regs.ebx >> 16) & 0xff);

	int maxCoreID = 1;
	if (maxBasicLeaf >= 4) {
		get_current_cpuid(&cpuid, 4, 0);
		maxCoreID = next_power_of_2((cpuid.regs.eax >> 26) + 1);
	}

	compute_cpu_hierarchy_masks(maxLogicalID, maxCoreID);
	return B_OK;
}


static void
detect_intel_cache_topology(uint32 maxBasicLeaf)
{
	if (maxBasicLeaf < 4)
		return;

	uint8 hierarchyLevels[CPU_MAX_CACHE_LEVEL];
	int maxCacheLevel = 0;

	int currentLevel = 0;
	int cacheType;
	do {
		cpuid_info cpuid;
		get_current_cpuid(&cpuid, 4, currentLevel);

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
			if (cpu->arch.vendor == VENDOR_AMD) {
				result = detect_amd_cpu_topology(maxBasicLeaf, maxExtendedLeaf);
				if (result == B_OK)
					detect_amd_cache_topology(maxExtendedLeaf);
			} else if (cpu->arch.vendor == VENDOR_INTEL) {
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


// #pragma mark - Microcode: Intel


static void
detect_intel_patch_level(cpu_ent* cpu)
{
	if (cpu->arch.feature[FEATURE_EXT] & IA32_FEATURE_EXT_HYPERVISOR) {
		cpu->arch.patch_level = 0;
		return;
	}

	x86_write_msr(IA32_MSR_UCODE_REV, 0);
	cpuid_info cpuid;
	get_current_cpuid(&cpuid, 1, 0);

	uint64 value = x86_read_msr(IA32_MSR_UCODE_REV);
	cpu->arch.patch_level = value >> 32;
}


static struct intel_microcode_header*
find_microcode_intel(addr_t data, size_t size, uint32 patchLevel)
{
	// 9.11.3 Processor Identification
	cpuid_info cpuid;
	get_current_cpuid(&cpuid, 1, 0);
	uint32 signature = cpuid.regs.eax;
	// 9.11.4 Platform Identification
	uint64 platformBits = (x86_read_msr(IA32_MSR_PLATFORM_ID) >> 50) & 0x7;
	uint64 mask = 1 << platformBits;

	while (size > 0) {
		if (size < sizeof(struct intel_microcode_header)) {
			dprintf("find_microcode_intel update is too small for header\n");
			break;
		}
		struct intel_microcode_header* header
			= (struct intel_microcode_header*)data;

		uint32 totalSize = header->total_size;
		uint32 dataSize = header->data_size;
		if (dataSize == 0) {
			dataSize = 2000;
			totalSize = sizeof(struct intel_microcode_header) + dataSize;
		}
		if (totalSize > size) {
			dprintf("find_microcode_intel update is too small for data\n");
			break;
		}

		uint32* dwords = (uint32*)data;
		size -= totalSize;
		data += totalSize;

		if (header->loader_revision != 1) {
			dprintf("find_microcode_intel incorrect loader version\n");
			continue;
		}
		// 9.11.6 The microcode update data requires a 16-byte boundary
		// alignment.
		if (((addr_t)header % 16) != 0) {
			dprintf("find_microcode_intel incorrect alignment\n");
			continue;
		}
		uint32 sum = 0;
		for (uint32 i = 0; i < totalSize / 4; i++)
			sum += dwords[i];
		if (sum != 0) {
			dprintf("find_microcode_intel incorrect checksum\n");
			continue;
		}
		if (patchLevel > header->update_revision) {
			dprintf("find_microcode_intel update_revision is lower\n");
			continue;
		}
		if (signature == header->processor_signature
			&& (mask & header->processor_flags) != 0) {
			return header;
		}
		if (totalSize <= (sizeof(struct intel_microcode_header) + dataSize
			+ sizeof(struct intel_microcode_extended_signature_header))) {
			continue;
		}
		struct intel_microcode_extended_signature_header* extSigHeader
			= (struct intel_microcode_extended_signature_header*)((addr_t)header
				+ sizeof(struct intel_microcode_header) + dataSize);
		struct intel_microcode_extended_signature* extended_signature
			= (struct intel_microcode_extended_signature*)((addr_t)extSigHeader
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
	struct intel_microcode_header* update
		= (struct intel_microcode_header*)sLoadedUcodeUpdate;
	if (update == NULL) {
		update = find_microcode_intel((addr_t)sUcodeData, sUcodeDataSize,
			revision);
	}
	if (update == NULL) {
		dprintf("CPU %d: no update found\n", currentCPU);
	} else if (update->update_revision != revision) {
		addr_t data = (addr_t)update + sizeof(struct intel_microcode_header);
		wbinvd();
		x86_write_msr(IA32_MSR_UCODE_WRITE, data);
		detect_intel_patch_level(cpu);
		if (revision == cpu->arch.patch_level) {
			dprintf("CPU %d: update failed\n", currentCPU);
		} else {
			if (sLoadedUcodeUpdate == NULL)
				sLoadedUcodeUpdate = update;
			dprintf("CPU %d: updated from revision 0x%" B_PRIx32 " to 0x%"
				B_PRIx32 "\n", currentCPU, revision, cpu->arch.patch_level);
		}
	}

	if (currentCPU != 0)
		release_spinlock(&sUcodeUpdateLock);
}


// #pragma mark - Microcode: AMD


static void
detect_amd_patch_level(cpu_ent* cpu)
{
	if (cpu->arch.feature[FEATURE_EXT] & IA32_FEATURE_EXT_HYPERVISOR) {
		cpu->arch.patch_level = 0;
		return;
	}

	uint64 value = x86_read_msr(IA32_MSR_UCODE_REV);
	cpu->arch.patch_level = (uint32)value;
}


static struct amd_microcode_header*
find_microcode_amd(addr_t data, size_t size, uint32 patchLevel)
{
	// 9.11.3 Processor Identification
	cpuid_info cpuid;
	get_current_cpuid(&cpuid, 1, 0);
	uint32 signature = cpuid.regs.eax;

	if (size < sizeof(struct amd_container_header)) {
		dprintf("find_microcode_amd update is too small for header\n");
		return NULL;
	}
	struct amd_container_header* container = (struct amd_container_header*)data;
	if (container->magic != 0x414d44) {
		dprintf("find_microcode_amd update invalid magic\n");
		return NULL;
	}

	size -= sizeof(*container);
	data += sizeof(*container);

	struct amd_section_header* section = (struct amd_section_header*)data;
	if (section->type != 0 || section->size == 0) {
		dprintf("find_microcode_amd update first section invalid\n");
		return NULL;
	}

	size -= sizeof(*section);
	data += sizeof(*section);

	amd_equiv_cpu_entry* table = (amd_equiv_cpu_entry*)data;
	size -= section->size;
	data += section->size;

	uint16 equiv_id = 0;
	for (uint32 i = 0; table[i].installed_cpu != 0; i++) {
		if (signature == table[i].equiv_cpu) {
			equiv_id = table[i].equiv_cpu;
			dprintf("find_microcode_amd found equiv cpu: %x\n", equiv_id);
			break;
		}
	}
	if (equiv_id == 0) {
		dprintf("find_microcode_amd update cpu not found in equiv table\n");
		return NULL;
	}

	while (size > sizeof(amd_section_header)) {
		struct amd_section_header* section = (struct amd_section_header*)data;
		size -= sizeof(*section);
		data += sizeof(*section);

		if (section->type != 1 || section->size > size
			|| section->size < sizeof(amd_microcode_header)) {
			dprintf("find_microcode_amd update firmware section invalid\n");
			return NULL;
		}
		struct amd_microcode_header* header = (struct amd_microcode_header*)data;
		size -= section->size;
		data += section->size;

		if (header->processor_rev_id != equiv_id) {
			dprintf("find_microcode_amd update found rev_id %x\n",
				header->processor_rev_id);
			continue;
		}
		if (patchLevel >= header->patch_id) {
			dprintf("find_microcode_amd update_revision is lower\n");
			continue;
		}
		if (header->nb_dev_id != 0 || header->sb_dev_id != 0) {
			dprintf("find_microcode_amd update chipset specific firmware\n");
			continue;
		}
		if (((addr_t)header % 16) != 0) {
			dprintf("find_microcode_amd incorrect alignment\n");
			continue;
		}

		return header;
	}
	dprintf("find_microcode_amd no fw update found for this cpu\n");
	return NULL;
}


static void
load_microcode_amd(int currentCPU, cpu_ent* cpu)
{
	if (currentCPU != 0)
		acquire_spinlock(&sUcodeUpdateLock);

	detect_amd_patch_level(cpu);
	uint32 revision = cpu->arch.patch_level;
	struct amd_microcode_header* update
		= (struct amd_microcode_header*)sLoadedUcodeUpdate;
	if (update == NULL) {
		update = find_microcode_amd((addr_t)sUcodeData, sUcodeDataSize,
			revision);
	}
	if (update != NULL) {
		addr_t data = (addr_t)update;
		wbinvd();
		x86_write_msr(MSR_K8_UCODE_UPDATE, data);
		detect_amd_patch_level(cpu);
		if (revision == cpu->arch.patch_level) {
			dprintf("CPU %d: update failed\n", currentCPU);
		} else {
			if (sLoadedUcodeUpdate == NULL)
				sLoadedUcodeUpdate = update;
			dprintf("CPU %d: updated from revision 0x%" B_PRIx32 " to 0x%"
				B_PRIx32 "\n", currentCPU, revision, cpu->arch.patch_level);
		}
	} else {
		dprintf("CPU %d: no update found\n", currentCPU);
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
	if (cpu->arch.vendor == VENDOR_INTEL)
		load_microcode_intel(currentCPU, cpu);
	else if (cpu->arch.vendor == VENDOR_AMD)
		load_microcode_amd(currentCPU, cpu);
}


// #pragma mark - Hybrid CPU


static uint8
get_hybrid_cpu_type()
{
	cpu_ent* cpu = get_cpu_struct();
	if ((cpu->arch.feature[FEATURE_7_EDX] & IA32_FEATURE_HYBRID_CPU) == 0)
		return 0;

#define X86_HYBRID_CPU_TYPE_ID_SHIFT	24
	cpuid_info cpuid;
	get_current_cpuid(&cpuid, 0x1a, 0);
	return cpuid.regs.eax >> X86_HYBRID_CPU_TYPE_ID_SHIFT;
}


static const char*
get_hybrid_cpu_type_string(uint8 type)
{
	switch (type) {
		case 0x20:	return "Atom";
		case 0x40:	return "Core";
		default:	return "";
	}
}


// #pragma mark - CPU identification


static void
identify_vendor(cpu_ent* cpu, const char* vendorString)
{
	if (strcmp(vendorString, "GenuineIntel") == 0) {
		cpu->arch.vendor = VENDOR_INTEL;
		cpu->arch.vendor_name = "Intel";
	} else if (strcmp(vendorString, "AuthenticAMD") == 0) {
		cpu->arch.vendor = VENDOR_AMD;
		cpu->arch.vendor_name = "AMD";
	} else {
		cpu->arch.vendor = VENDOR_UNKNOWN;
		cpu->arch.vendor_name = "UNKNOWN";
	}
}


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

	get_current_cpuid(&cpuid, 1, 0);
	cpu->arch.type = cpuid.eax_1.type;
	cpu->arch.family = cpuid.eax_1.family;
	cpu->arch.extended_family = cpuid.eax_1.extended_family;
	cpu->arch.model = cpuid.eax_1.model;
	cpu->arch.extended_model = cpuid.eax_1.extended_model;
	cpu->arch.stepping = cpuid.eax_1.stepping;
	if (full) {
		dprintf("CPU %d: type %d family %d extended_family %d model %d "
			"extended_model %d stepping %d, string '%s'\n",
			currentCPU, cpu->arch.type, cpu->arch.family,
			cpu->arch.extended_family, cpu->arch.model,
			cpu->arch.extended_model, cpu->arch.stepping, vendorString);
	}

	identify_vendor(cpu, vendorString);

	// model name
	get_current_cpuid(&cpuid, 0x80000000, 0);
	uint32 maxExtendedLeaf = cpuid.eax_0.max_eax;
	if (maxExtendedLeaf >= 0x80000004) {
		unsigned int temp;
		memset(cpu->arch.model_name, 0, sizeof(cpu->arch.model_name));

		get_current_cpuid(&cpuid, 0x80000002, 0);
		temp = cpuid.regs.edx;
		cpuid.regs.edx = cpuid.regs.ecx;
		cpuid.regs.ecx = temp;
		memcpy(cpu->arch.model_name, cpuid.as_chars, sizeof(cpuid.as_chars));

		get_current_cpuid(&cpuid, 0x80000003, 0);
		temp = cpuid.regs.edx;
		cpuid.regs.edx = cpuid.regs.ecx;
		cpuid.regs.ecx = temp;
		memcpy(cpu->arch.model_name + 16, cpuid.as_chars,
			sizeof(cpuid.as_chars));

		get_current_cpuid(&cpuid, 0x80000004, 0);
		temp = cpuid.regs.edx;
		cpuid.regs.edx = cpuid.regs.ecx;
		cpuid.regs.ecx = temp;
		memcpy(cpu->arch.model_name + 32, cpuid.as_chars,
			sizeof(cpuid.as_chars));

		// some cpus return a right-justified string
		int32 i = 0;
		while (cpu->arch.model_name[i] == ' ')
			i++;
		if (i > 0) {
			memmove(cpu->arch.model_name, &cpu->arch.model_name[i],
				strlen(&cpu->arch.model_name[i]) + 1);
		}

		if (full) {
			dprintf("CPU %d: vendor '%s' model name '%s'\n",
				currentCPU, cpu->arch.vendor_name, cpu->arch.model_name);
		}
	} else {
		strlcpy(cpu->arch.model_name, "unknown", sizeof(cpu->arch.model_name));
	}

	// feature bits
	get_current_cpuid(&cpuid, 1, 0);
	cpu->arch.feature[FEATURE_COMMON] = cpuid.eax_1.features;
	cpu->arch.feature[FEATURE_EXT] = cpuid.eax_1.extended_features;

	if (!full)
		return;

	if (maxExtendedLeaf >= 0x80000001) {
		get_current_cpuid(&cpuid, 0x80000001, 0);
		if (cpu->arch.vendor == VENDOR_AMD)
			cpu->arch.feature[FEATURE_EXT_AMD_ECX] = cpuid.regs.ecx;
		cpu->arch.feature[FEATURE_EXT_AMD] = cpuid.regs.edx;
		if (cpu->arch.vendor != VENDOR_AMD)
			cpu->arch.feature[FEATURE_EXT_AMD] &= IA32_FEATURES_INTEL_EXT;
	}

	if (maxBasicLeaf >= 6) {
		get_current_cpuid(&cpuid, 6, 0);
		cpu->arch.feature[FEATURE_6_EAX] = cpuid.regs.eax;
		cpu->arch.feature[FEATURE_6_ECX] = cpuid.regs.ecx;
	}

	if (maxBasicLeaf >= 7) {
		get_current_cpuid(&cpuid, 7, 0);
		cpu->arch.feature[FEATURE_7_EBX] = cpuid.regs.ebx;
		cpu->arch.feature[FEATURE_7_ECX] = cpuid.regs.ecx;
		cpu->arch.feature[FEATURE_7_EDX] = cpuid.regs.edx;
	}

	if (maxBasicLeaf >= 0xd) {
		get_current_cpuid(&cpuid, 0xd, 1);
		cpu->arch.feature[FEATURE_D_1_EAX] = cpuid.regs.eax;
	}

	if (maxExtendedLeaf >= 0x80000007) {
		get_current_cpuid(&cpuid, 0x80000007, 0);
		cpu->arch.feature[FEATURE_EXT_7_EDX] = cpuid.regs.edx;
	}

	if (maxExtendedLeaf >= 0x80000008) {
		get_current_cpuid(&cpuid, 0x80000008, 0);
		cpu->arch.feature[FEATURE_EXT_8_EBX] = cpuid.regs.ebx;
	}

	detect_cpu_topology(currentCPU, cpu, maxBasicLeaf, maxExtendedLeaf);

	if (cpu->arch.vendor == VENDOR_INTEL)
		detect_intel_patch_level(cpu);
	else if (cpu->arch.vendor == VENDOR_AMD)
		detect_amd_patch_level(cpu);

	cpu->arch.hybrid_type = get_hybrid_cpu_type();

#if DUMP_FEATURE_STRING
	dump_feature_string(currentCPU, cpu);
#endif
#if DUMP_CPU_PATCHLEVEL_TYPE
	dprintf("CPU %d: patch_level 0x%" B_PRIx32 "%s%s\n", currentCPU,
		cpu->arch.patch_level,
		cpu->arch.hybrid_type != 0 ? ", hybrid type ": "",
		get_hybrid_cpu_type_string(cpu->arch.hybrid_type));
#endif
}


bool
x86_check_feature(uint32 feature, enum x86_feature_type type)
{
	cpu_ent* cpu = get_cpu_struct();

#if 0
	int i;
	dprintf("x86_check_feature: feature 0x%x, type %d\n", feature, type);
	for (i = 0; i < FEATURE_NUM; i++) {
		dprintf("features %d: 0x%x\n", i, cpu->arch.feature[i]);
	}
#endif

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


// #pragma mark - Idle


static void
halt_idle(void)
{
	asm("hlt");
}


// #pragma mark - TSC calibration


static void
init_tsc_with_cpuid(kernel_args* args, uint32* conversionFactor)
{
	cpu_ent* cpu = get_cpu_struct();
	if (cpu->arch.vendor != VENDOR_INTEL)
		return;

	uint32 model = (cpu->arch.extended_model << 4) | cpu->arch.model;
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
	if (khz == 0 && model == 0x5f)
		khz = 25000;

	if (khz == 0 && maxBasicLeaf >= IA32_CPUID_LEAF_FREQUENCY) {
		get_current_cpuid(&cpuid, IA32_CPUID_LEAF_FREQUENCY, 0);
		khz = cpuid.regs.eax * 1000 * denominator / numerator;
	}
	if (khz == 0)
		return;

	dprintf("CPU: using TSC frequency from CPUID\n");
	*conversionFactor = (1000ULL << 32) / (khz * numerator / denominator);
	args->arch_args.system_time_cv_factor = *conversionFactor;
}


static void
init_tsc_with_msr(kernel_args* args, uint32* conversionFactor)
{
	cpu_ent* cpuEnt = get_cpu_struct();
	if (cpuEnt->arch.vendor != VENDOR_AMD)
		return;

	uint32 family = cpuEnt->arch.family + cpuEnt->arch.extended_family;
	if (family < 0x10)
		return;
	uint64 value = x86_read_msr(MSR_F10H_HWCR);
	if ((value & HWCR_TSCFREQSEL) == 0)
		return;

	value = x86_read_msr(MSR_F10H_PSTATEDEF(0));
	if ((value & PSTATEDEF_EN) == 0)
		return;
	if (family != 0x17 && family != 0x19)
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

	dprintf("CPU: using TSC frequency from MSR %" B_PRIu64 "\n",
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

	__x86_setup_system_time((uint64)conversionFactor << 32,
		conversionFactorNsecs);
}


// #pragma mark - Initialization


status_t
arch_cpu_preboot_init_percpu(kernel_args* args, int cpu)
{
	if (cpu == 0) {
		sDoubleFaultStacks = vm_allocate_early(args,
			kDoubleFaultStackSize * smp_get_num_cpus(), 0, 0, 0);
	}

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


status_t
arch_cpu_init_percpu(kernel_args* args, int cpu)
{
	detect_cpu(cpu, false);
	load_microcode(cpu);
	detect_cpu(cpu);

	if (cpu == 0) {
		init_tsc(args);
		gCpuIdleFunc = halt_idle;
	}

	if (x86_check_feature(IA32_FEATURE_MCE, FEATURE_COMMON))
		x86_write_cr4(x86_read_cr4() | IA32_CR4_MCE);

	cpu_ent* cpuEnt = get_cpu_struct();
	if (cpu == 0) {
		bool supportsPAT = x86_check_feature(IA32_FEATURE_PAT, FEATURE_COMMON);

		sUsePAT = supportsPAT
			&& !get_safemode_boolean_early(args, B_SAFEMODE_DISABLE_PAT, false);

		if (sUsePAT) {
			dprintf("using PAT for memory type configuration\n");
		} else {
			dprintf("not using PAT for memory type configuration (%s)\n",
				supportsPAT ? "disabled" : "unsupported");
		}
	}

	if (sUsePAT)
		init_pat(cpu);

	if (x86_check_feature(IA32_FEATURE_AMD_EXT_RDTSCP, FEATURE_EXT_AMD)
		|| x86_check_feature(IA32_FEATURE_RDPID, FEATURE_7_ECX)) {
		x86_write_msr(IA32_MSR_TSC_AUX, cpu);
	}

	// make LFENCE dispatch-serializing on AMD
	if (cpuEnt->arch.vendor == VENDOR_AMD) {
		uint32 family = cpuEnt->arch.family + cpuEnt->arch.extended_family;
		if (family >= 0x10 && family != 0x11) {
			uint64 value = x86_read_msr(MSR_F10H_DE_CFG);
			if ((value & DE_CFG_SERIALIZE_LFENCE) == 0)
				x86_write_msr(MSR_F10H_DE_CFG, value | DE_CFG_SERIALIZE_LFENCE);
		}
	}

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


status_t
arch_cpu_init_post_vm(kernel_args* args)
{
	area_id stacks = create_area("double fault stacks",
		(void**)&sDoubleFaultStacks, B_EXACT_ADDRESS,
		kDoubleFaultStackSize * smp_get_num_cpus(),
		B_FULL_LOCK, B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA);
	if (stacks < B_OK)
		panic("failed to create double fault stacks area: %" B_PRId32, stacks);

	X86PagingStructures* kernelPagingStructures
		= static_cast<X86VMTranslationMap*>(
			VMAddressSpace::Kernel()->TranslationMap())->PagingStructures();

	for (uint32 i = 0; i < args->num_cpus; i++) {
		gCPU[i].arch.active_paging_structures = kernelPagingStructures;
		kernelPagingStructures->AddReference();
	}

	if (!apic_available())
		x86_init_fpu();

	// SMEP
	if (x86_check_feature(IA32_FEATURE_SMEP, FEATURE_7_EBX)) {
		if (!get_safemode_boolean(B_SAFEMODE_DISABLE_SMEP_SMAP, false)) {
			dprintf("enable SMEP\n");
			call_all_cpus_sync(&enable_smep, NULL);
		} else
			dprintf("SMEP disabled per safemode setting\n");
	}

	// SMAP
	if (x86_check_feature(IA32_FEATURE_SMAP, FEATURE_7_EBX)) {
		if (!get_safemode_boolean(B_SAFEMODE_DISABLE_SMEP_SMAP, false)) {
			dprintf("enable SMAP\n");
			call_all_cpus_sync(&enable_smap, NULL);
			arch_altcodepatch_replace(ALTCODEPATCH_TAG_STAC, &_stac, 3);
			arch_altcodepatch_replace(ALTCODEPATCH_TAG_CLAC, &_clac, 3);
		} else
			dprintf("SMAP disabled per safemode setting\n");
	}

	// XSAVE
	gHasXsave = x86_check_feature(IA32_FEATURE_EXT_XSAVE, FEATURE_EXT);
	if (gHasXsave) {
		gHasXsavec = x86_check_feature(IA32_FEATURE_XSAVEC, FEATURE_D_1_EAX);

		call_all_cpus_sync(&enable_osxsave, NULL);
		gXsaveMask = IA32_XCR0_X87 | IA32_XCR0_SSE;
		cpuid_info cpuid;
		get_current_cpuid(&cpuid, IA32_CPUID_LEAF_XSTATE, 0);
		gXsaveMask |= (cpuid.regs.eax & IA32_XCR0_AVX);
		call_all_cpus_sync(&enable_xsavemask, NULL);
		get_current_cpuid(&cpuid, IA32_CPUID_LEAF_XSTATE, 0);
		gFPUSaveLength = cpuid.regs.ebx;
		if (gFPUSaveLength > sizeof(((struct arch_thread *)0)->user_fpu_state))
			gFPUSaveLength = 832;

		arch_altcodepatch_replace(ALTCODEPATCH_TAG_XSAVE,
			gHasXsavec ? &_xsavec : &_xsave, 4);
		arch_altcodepatch_replace(ALTCODEPATCH_TAG_XRSTOR, &_xrstor, 4);

		dprintf("enable %s 0x%" B_PRIx64 " %" B_PRId64 "\n",
			gHasXsavec ? "XSAVEC" : "XSAVE", gXsaveMask, gFPUSaveLength);
	}

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


// #pragma mark - TLB


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
	for (int i = 0; i < num_pages; i++)
		invalidate_TLB(pages[i]);
}


// #pragma mark - Shutdown


status_t
arch_cpu_shutdown(bool rebootSystem)
{
	if (acpi_shutdown(rebootSystem) == B_OK)
		return B_OK;

	if (!rebootSystem)
		return B_NOT_SUPPORTED;

	cpu_status state = disable_interrupts();

	// try keyboard controller reset
	out8(0xfe, 0x64);
	snooze(500000);

	// fallback
	x86_reboot();

	restore_interrupts(state);
	return B_ERROR;
}


void
arch_cpu_sync_icache(void* address, size_t length)
{
	// instruction cache is always consistent on x86
}
