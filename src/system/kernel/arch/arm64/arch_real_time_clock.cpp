/*
 * Copyright 2019-2025 Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 */
#include <arch/real_time_clock.h>

#include <real_time_clock.h>
#include <real_time_data.h>
#include <smp.h>
#include <arch/cpu.h>
#include <arch/timer.h>
#include <atomic.h>


status_t
arch_rtc_init(kernel_args *args, struct real_time_data *data)
{
	// ARM Generic Timer as time source
	// Read timer frequency
	uint64 freq;
	asm("mrs %0, cntfrq_el0" : "=r"(freq));

	// Calculate conversion factor for userspace
	// This allows system_time() to be computed in userspace
	data->arch_data.system_time_conversion_factor =
		(1000000LL << 32) / freq;

	data->arch_data.system_time_offset = 0;

	return B_OK;
}


uint32
arch_rtc_get_hw_time(void)
{
	// Read current counter value from Generic Timer
	uint64 counter;
	asm("mrs %0, cntpct_el0" : "=r"(counter));

	// Read frequency
	uint64 freq;
	asm("mrs %0, cntfrq_el0" : "=r"(freq));

	// Convert to seconds
	return (uint32)(counter / freq);
}


void
arch_rtc_set_hw_time(uint32 seconds)
{
	// ARM Generic Timer is read-only
	// RTC setting would require separate RTC device
	// For now, do nothing
}


void
arch_rtc_set_system_time_offset(struct real_time_data *data, bigtime_t offset)
{
	atomic_set64(&data->arch_data.system_time_offset, offset);
}


bigtime_t
arch_rtc_get_system_time_offset(struct real_time_data *data)
{
	return atomic_get64(&data->arch_data.system_time_offset);
}
