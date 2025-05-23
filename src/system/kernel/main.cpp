/*
 * Copyright 2018, Jérôme Duval, jerome.duval@gmail.com.
 * Copyright 2002-2020, Axel Dörfler, axeld@pinc-software.de.
 * Distributed under the terms of the MIT License.
 *
 * Copyright 2001-2002, Travis Geiselbrecht. All rights reserved.
 * Distributed under the terms of the NewOS License.
 */


/*! This is main - initializes the kernel and launches the launch_daemon */


#include <string.h>

#include <FindDirectory.h>
#include <OS.h>

#include <arch/platform.h>
#include <boot_device.h>
#include <boot_item.h>
#include <boot_splash.h>
#include <commpage.h>
#ifdef _COMPAT_MODE
#	include <commpage_compat.h>
#endif
#include <condition_variable.h>
#include <cpu.h>
#include <debug.h>
#include <DPC.h>
#include <elf.h>
#include <find_directory_private.h>
#include <fs/devfs.h>
#include <fs/KPath.h>
#include <interrupts.h>
#include <kdevice_manager.h>
#include <kdriver_settings.h>
#include <kernel_daemon.h>
#include <kmodule.h>
#include <kscheduler.h>
#include <ksyscalls.h>
#include <ksystem_info.h>
#include <lock.h>
#include <low_resource_manager.h>
#include <messaging.h>
#include <Notifications.h>
#include <port.h>
#include <posix/realtime_sem.h>
#include <posix/xsi_message_queue.h>
#include <posix/xsi_semaphore.h>
#include <real_time_clock.h>
#include <sem.h>
#include <smp.h>
#include <stack_protector.h>
#include <system_profiler.h>
#include <team.h>
#include <timer.h>
#include <user_debugger.h>
#include <user_mutex.h>
#include <vfs.h>
#include <vm/vm.h>
#include <boot/kernel_args.h>

#include "vm/VMAnonymousCache.h"


//#define TRACE_BOOT
#ifdef TRACE_BOOT
#	define TRACE(x...) dprintf("INIT: " x)
#else
#	define TRACE(x...) ;
#endif


void *__dso_handle;

bool gKernelStartup = true;
bool gKernelShutdown = false;

static kernel_args sKernelArgs;
static uint32 sCpuRendezvous;
static uint32 sCpuRendezvous2;
static uint32 sCpuRendezvous3;

static int32 main2(void *);


static void
non_boot_cpu_init(void* args, int currentCPU)
{
	kernel_args* kernelArgs = (kernel_args*)args;
	if (currentCPU != 0)
		cpu_init_percpu(kernelArgs, currentCPU);
}


extern "C" int
_start(kernel_args *bootKernelArgs, int currentCPU)
{
	if (bootKernelArgs->version == CURRENT_KERNEL_ARGS_VERSION
		&& bootKernelArgs->kernel_args_size == kernel_args_size_v1) {
		sKernelArgs.ucode_data = NULL;
		sKernelArgs.ucode_data_size = 0;
	} else if (bootKernelArgs->kernel_args_size != sizeof(kernel_args)
		|| bootKernelArgs->version != CURRENT_KERNEL_ARGS_VERSION) {
		// This is something we cannot handle right now - release kernels
		// should always be able to handle the kernel_args of earlier
		// released kernels.
		debug_early_boot_message("Version mismatch between boot loader and "
			"kernel!\n");
		return -1;
	}

	smp_set_num_cpus(bootKernelArgs->num_cpus);

	// wait for all the cpus to get here
	smp_cpu_rendezvous(&sCpuRendezvous);

	// the passed in kernel args are in a non-allocated range of memory
	if (currentCPU == 0)
		memcpy(&sKernelArgs, bootKernelArgs, bootKernelArgs->kernel_args_size);

	smp_cpu_rendezvous(&sCpuRendezvous2);

	// do any pre-booting cpu config
	cpu_preboot_init_percpu(&sKernelArgs, currentCPU);
	thread_preboot_init_percpu(&sKernelArgs, currentCPU);

	// if we're not a boot cpu, spin here until someone wakes us up
	if (smp_trap_non_boot_cpus(currentCPU, &sCpuRendezvous3)) {
		// init platform
		arch_platform_init(&sKernelArgs);

		// setup debug output
		debug_init(&sKernelArgs);
		set_dprintf_enabled(true);
		dprintf("Welcome to kernel debugger output!\n");
		dprintf("Haiku revision: %s, debug level: %d\n", get_haiku_revision(),
			KDEBUG_LEVEL);

		// init modules
		TRACE("init CPU\n");
		cpu_init(&sKernelArgs);
		cpu_init_percpu(&sKernelArgs, currentCPU);
		TRACE("init interrupts\n");
		interrupts_init(&sKernelArgs);

		TRACE("init VM\n");
		vm_init(&sKernelArgs);
			// Before vm_init_post_sem() is called, we have to make sure that
			// the boot loader allocated region is not used anymore
		boot_item_init();
		debug_init_post_vm(&sKernelArgs);
		low_resource_manager_init();

		// now we can use the heap and create areas
		arch_platform_init_post_vm(&sKernelArgs);
		lock_debug_init();
		TRACE("init driver_settings\n");
		driver_settings_init(&sKernelArgs);
		debug_init_post_settings(&sKernelArgs);
		TRACE("init notification services\n");
		notifications_init();
		TRACE("init teams\n");
		team_init(&sKernelArgs);
		TRACE("init ELF loader\n");
		elf_init(&sKernelArgs);
		TRACE("init modules\n");
		module_init(&sKernelArgs);
		TRACE("init semaphores\n");
		haiku_sem_init(&sKernelArgs);
		TRACE("init interrupts post vm\n");
		interrupts_init_post_vm(&sKernelArgs);
		cpu_init_post_vm(&sKernelArgs);
		commpage_init();
#ifdef _COMPAT_MODE
		commpage_compat_init();
#endif
		call_all_cpus_sync(non_boot_cpu_init, &sKernelArgs);

		TRACE("init system info\n");
		system_info_init(&sKernelArgs);

		TRACE("init SMP\n");
		smp_init(&sKernelArgs);
		cpu_build_topology_tree();
		TRACE("init timer\n");
		timer_init(&sKernelArgs);
		TRACE("init real time clock\n");
		rtc_init(&sKernelArgs);
		timer_init_post_rtc();

		TRACE("init condition variables\n");
		condition_variable_init();

		// now we can create and use semaphores
		TRACE("init VM semaphores\n");
		vm_init_post_sem(&sKernelArgs);
		TRACE("init generic syscall\n");
		generic_syscall_init();
		smp_init_post_generic_syscalls();
		TRACE("init scheduler\n");
		scheduler_init();
		TRACE("init threads\n");
		thread_init(&sKernelArgs);
		TRACE("init kernel daemons\n");
		kernel_daemon_init();
		TRACE("init stack protector\n");
		stack_protector_init();
		arch_platform_init_post_thread(&sKernelArgs);

		TRACE("init I/O interrupts\n");
		interrupts_init_io(&sKernelArgs);
		TRACE("init VM threads\n");
		vm_init_post_thread(&sKernelArgs);
		low_resource_manager_init_post_thread();
		TRACE("init DPC\n");
		dpc_init();
		TRACE("init VFS\n");
		vfs_init(&sKernelArgs);
#if ENABLE_SWAP_SUPPORT
		TRACE("init swap support\n");
		swap_init();
#endif
		TRACE("init POSIX semaphores\n");
		realtime_sem_init();
		xsi_sem_init();
		xsi_msg_init();

		// Start a thread to finish initializing the rest of the system. Note,
		// it won't be scheduled before calling scheduler_start() (on any CPU).
		TRACE("spawning main2 thread\n");
		thread_id thread = spawn_kernel_thread(&main2, "main2",
			B_NORMAL_PRIORITY, NULL);
		resume_thread(thread);

		// We're ready to start the scheduler and enable interrupts on all CPUs.
		scheduler_enable_scheduling();

		// bring up the AP cpus in a lock step fashion
		TRACE("waking up AP cpus\n");
		sCpuRendezvous = sCpuRendezvous2 = 0;
		smp_wake_up_non_boot_cpus();
		smp_cpu_rendezvous(&sCpuRendezvous); // wait until they're booted

		// exit the kernel startup phase (mutexes, etc work from now on out)
		TRACE("exiting kernel startup\n");
		gKernelStartup = false;

		smp_cpu_rendezvous(&sCpuRendezvous2);
			// release the AP cpus to go enter the scheduler

		TRACE("starting scheduler on cpu 0 and enabling interrupts\n");
		scheduler_start();
		enable_interrupts();
	} else {
		// lets make sure we're in sync with the main cpu
		// the boot processor has probably been sending us
		// tlb sync messages all along the way, but we've
		// been ignoring them
		arch_cpu_global_TLB_invalidate();

		// this is run for each non boot processor after they've been set loose
		smp_per_cpu_init(&sKernelArgs, currentCPU);

		// wait for all other AP cpus to get to this point
		smp_cpu_rendezvous(&sCpuRendezvous);
		smp_cpu_rendezvous(&sCpuRendezvous2);

		// welcome to the machine
		scheduler_start();
		enable_interrupts();
	}

#ifdef TRACE_BOOT
	// We disable interrupts for this dprintf(), since otherwise dprintf()
	// would acquires a mutex, which is something we must not do in an idle
	// thread, or otherwise the scheduler would be seriously unhappy.
	disable_interrupts();
	TRACE("main: done... begin idle loop on cpu %d\n", currentCPU);
	enable_interrupts();
#endif

	for (;;)
		cpu_idle();

	return 0;
}


static int32
main2(void* /*unused*/)
{
	TRACE("start of main2: initializing devices\n");

#if SYSTEM_PROFILER
	start_system_profiler(SYSTEM_PROFILE_SIZE, SYSTEM_PROFILE_STACK_DEPTH,
		SYSTEM_PROFILE_INTERVAL);
#endif
	boot_splash_init(sKernelArgs.boot_splash);

	commpage_init_post_cpus();
#ifdef _COMPAT_MODE
	commpage_compat_init_post_cpus();
#endif

	TRACE("init ports\n");
	port_init(&sKernelArgs);

	TRACE("init user mutex\n");
	user_mutex_init();

	TRACE("init system notifications\n");
	system_notifications_init();

	scheduler_loadavg_init();

	TRACE("Init modules\n");
	boot_splash_set_stage(BOOT_SPLASH_STAGE_1_INIT_MODULES);
	module_init_post_threads();

	// init userland debugging
	TRACE("Init Userland debugging\n");
	init_user_debug();

	// init the messaging service
	TRACE("Init Messaging Service\n");
	init_messaging_service();

	/* bootstrap all the filesystems */
	TRACE("Bootstrap file systems\n");
	boot_splash_set_stage(BOOT_SPLASH_STAGE_2_BOOTSTRAP_FS);
	vfs_bootstrap_file_systems();

	TRACE("Init Device Manager\n");
	boot_splash_set_stage(BOOT_SPLASH_STAGE_3_INIT_DEVICES);
	device_manager_init(&sKernelArgs);

	TRACE("Add preloaded old-style drivers\n");
	legacy_driver_add_preloaded(&sKernelArgs);

	interrupts_init_post_device_manager(&sKernelArgs);

	TRACE("Mount boot file system\n");
	boot_splash_set_stage(BOOT_SPLASH_STAGE_4_MOUNT_BOOT_FS);
	vfs_mount_boot_file_system(&sKernelArgs);

#if ENABLE_SWAP_SUPPORT
	TRACE("swap_init_post_modules\n");
	swap_init_post_modules();
#endif

	// CPU specific modules may now be available
	boot_splash_set_stage(BOOT_SPLASH_STAGE_5_INIT_CPU_MODULES);
	cpu_init_post_modules(&sKernelArgs);

	TRACE("vm_init_post_modules\n");
	boot_splash_set_stage(BOOT_SPLASH_STAGE_6_INIT_VM_MODULES);
	vm_init_post_modules(&sKernelArgs);

	TRACE("debug_init_post_modules\n");
	debug_init_post_modules(&sKernelArgs);

	TRACE("device_manager_init_post_modules\n");
	device_manager_init_post_modules(&sKernelArgs);

	boot_splash_set_stage(BOOT_SPLASH_STAGE_7_RUN_BOOT_SCRIPT);
	boot_splash_uninit();
		// NOTE: We could introduce a syscall to draw more icons indicating
		// stages in the boot script itself. Then we should not free the image.
		// In that case we should copy it over to the kernel heap, so that we
		// can still free the kernel args.

	// The boot splash screen is the last user of the kernel args.
	// Note: don't confuse the kernel_args structure (which is never freed)
	// with the kernel args ranges it contains (and which are freed here).
	vm_free_kernel_args(&sKernelArgs);

	// start the init process
	{
		KPath serverPath;
		status_t status = __find_directory(B_SYSTEM_SERVERS_DIRECTORY,
			gBootDevice, false, serverPath.LockBuffer(),
			serverPath.BufferSize());
		if (status != B_OK)
			dprintf("main2: find_directory() failed: %s\n", strerror(status));
		serverPath.UnlockBuffer();
		status = serverPath.Append("/launch_daemon");
		if (status != B_OK) {
			dprintf("main2: constructing path to launch_daemon failed: %s\n",
			strerror(status));
		}

		const char* args[] = { serverPath.Path(), NULL };
		int32 argc = 1;
		thread_id thread;

		thread = load_image(argc, args, NULL);
		if (thread >= B_OK) {
			resume_thread(thread);
			TRACE("launch_daemon started\n");
		} else {
			dprintf("error starting \"%s\" error = %" B_PRId32 " \n",
				args[0], thread);
		}
	}

	return 0;
}

