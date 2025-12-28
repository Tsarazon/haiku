/*
 * Copyright 2006-2007, Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Axel Dörfler, axeld@pinc-software.de
 *
 * Refactored 2025: Removed Gen < 9 support
 * - Removed legacy 2D blitter (BLT engine deprecated in Gen 9)
 * - Removed ring buffer (was used for blitter commands)
 * - Engine sync functions are stubs pending Gen 9+ implementation
 *
 * TODO: Implement Gen 9+ synchronization via:
 * - Fence registers for memory synchronization
 * - GT force wake for power state management
 * - GuC for workload scheduling (requires firmware)
 *
 * See Intel PRM Vol 2a: "Command Reference"
 * See Intel PRM Vol 2c: "GT Registers"
 */


#include "accelerant.h"
#include "accelerant_protos.h"

#undef TRACE
#define TRACE(x...) _sPrintf("intel_extreme: " x)
#define ERROR(x...) _sPrintf("intel_extreme: " x)
#define CALLED() TRACE("CALLED %s\n", __PRETTY_FUNCTION__)


static engine_token sEngineToken = {1, 0, NULL};


//	#pragma mark - engine management


uint32
intel_accelerant_engine_count(void)
{
	CALLED();
	return 1;
}


status_t
intel_acquire_engine(uint32 capabilities, uint32 maxWait, sync_token* syncToken,
	engine_token** _engineToken)
{
	CALLED();
	*_engineToken = &sEngineToken;

	if (acquire_lock(&gInfo->shared_info->engine_lock) != B_OK)
		return B_ERROR;

	/*
	 * TODO: Gen 9+ context management
	 * - GuC context allocation
	 * - Hardware context setup
	 * See Intel PRM Vol 2a: "Logical Ring Context"
	 */

	if (syncToken)
		intel_sync_to_token(syncToken);

	return B_OK;
}


status_t
intel_release_engine(engine_token* engineToken, sync_token* syncToken)
{
	CALLED();
	if (syncToken != NULL)
		syncToken->engine_id = engineToken->engine_id;

	/*
	 * TODO: Gen 9+ context cleanup
	 */

	release_lock(&gInfo->shared_info->engine_lock);
	return B_OK;
}


void
intel_wait_engine_idle(void)
{
	CALLED();

	/*
	 * TODO: Gen 9+ GPU synchronization
	 *
	 * Options for implementation:
	 * 1. Fence registers - for memory operation completion
	 *    See Intel PRM Vol 2c: "FENCE_REG" (0x100000+)
	 *
	 * 2. GT Force Wake - ensure GT is powered for register access
	 *    See Intel PRM Vol 2c: "FORCEWAKE" (0xA188)
	 *
	 * 3. Ring buffer IDLE bit - if using legacy ring submission
	 *    See Intel PRM Vol 2c: "RING_MI_MODE"
	 *
	 * 4. Hardware semaphores - for cross-engine sync
	 *    See Intel PRM Vol 2a: "MI_SEMAPHORE_WAIT"
	 *
	 * For now, this is a no-op since we don't submit GPU commands.
	 * When plane-based overlay or other GPU features are added,
	 * proper synchronization will be needed.
	 */
}


status_t
intel_get_sync_token(engine_token* engineToken, sync_token* syncToken)
{
	CALLED();

	/*
	 * TODO: Gen 9+ hardware sync tokens
	 * Could use MI_STORE_DATA_IMM to write sequence numbers
	 * to memory, then compare in intel_sync_to_token()
	 */

	return B_OK;
}


status_t
intel_sync_to_token(sync_token* syncToken)
{
	CALLED();

	/*
	 * TODO: Wait for hardware to reach sync token value
	 * Compare memory location written by MI_STORE_DATA_IMM
	 */

	intel_wait_engine_idle();
	return B_OK;
}
