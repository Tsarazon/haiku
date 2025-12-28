/*
 * Copyright 2006-2008, Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Axel Dörfler, axeld@pinc-software.de
 *
 * Refactored 2025: Gen 9+ command definitions
 *
 * Legacy 2D blitter commands (XY_*) and QueueCommands class REMOVED:
 * - BLT engine deprecated in Gen 9 (Skylake)
 * - No hardware 2D acceleration
 *
 * Remaining: MI (Memory Interface) commands for future use
 * in fence registers, semaphores, and synchronization.
 *
 * See Intel PRM Vol 2a "MI_* Commands"
 */
#ifndef COMMANDS_H
#define COMMANDS_H


/*
 * Gen 9+ MI (Memory Interface) Commands
 * Per Intel PRM Vol 2a
 *
 * These may be used for:
 * - Fence register synchronization
 * - Hardware semaphores
 * - Power state management
 */

/* MI_NOOP - No operation, used for alignment */
#define MI_NOOP						(0x00 << 23)

/* MI_FLUSH - Flush pending operations */
#define MI_FLUSH					(0x04 << 23)
#define MI_FLUSH_DW					(0x26 << 23)

/* MI_WAIT_FOR_EVENT - Wait for display event */
#define MI_WAIT_FOR_EVENT			(0x03 << 23)
#define MI_WAIT_FOR_PIPE_A_VBLANK	(1 << 3)
#define MI_WAIT_FOR_PIPE_B_VBLANK	(1 << 11)
#define MI_WAIT_FOR_PIPE_C_VBLANK	(1 << 14)

/* MI_STORE_DATA_IMM - Store immediate data to memory */
#define MI_STORE_DATA_IMM			(0x20 << 23)

/* MI_LOAD_REGISTER_IMM - Load immediate to register */
#define MI_LOAD_REGISTER_IMM		(0x22 << 23)

/* MI_SEMAPHORE_WAIT - Wait on semaphore (Gen 8+) */
#define MI_SEMAPHORE_WAIT			(0x1C << 23)

/* MI_BATCH_BUFFER_START - Start batch buffer execution */
#define MI_BATCH_BUFFER_START		(0x31 << 23)


#endif	/* COMMANDS_H */
