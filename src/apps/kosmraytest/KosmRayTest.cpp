/*
 * Copyright 2025 KosmOS Project. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * KosmRay IPC Test Suite — exercises all kosm_ray API paths.
 */


#include <Application.h>
#include <FindDirectory.h>
#include <Font.h>
#include <KosmOS.h>
#include <Path.h>
#include <View.h>
#include <Window.h>

#include <OS.h>
#include <image.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


// ---------------------------------------------------------------------------
// Globals
// ---------------------------------------------------------------------------

static FILE* sTraceFile = NULL;
static int sPassCount = 0;
static int sFailCount = 0;

static const int kMaxLines = 300;
static char sLines[kMaxLines][256];
static int sLineCount = 0;


// ---------------------------------------------------------------------------
// Trace helpers
// ---------------------------------------------------------------------------

static void
trace(const char* fmt, ...)
{
	if (sTraceFile == NULL)
		return;
	va_list ap;
	va_start(ap, fmt);
	vfprintf(sTraceFile, fmt, ap);
	va_end(ap);
	fflush(sTraceFile);
}


static void
trace_call(const char* func, status_t result)
{
	trace("    %s -> %s (0x%08lx)\n", func, strerror(result),
		(unsigned long)result);
}


static void __attribute__((unused))
trace_call_id(const char* func, int32 result)
{
	if (result >= 0)
		trace("    %s -> id=%ld\n", func, (long)result);
	else
		trace("    %s -> %s (0x%08lx)\n", func, strerror(result),
			(unsigned long)result);
}


static void
log_line(const char* fmt, ...)
{
	if (sLineCount >= kMaxLines)
		return;
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(sLines[sLineCount], sizeof(sLines[0]), fmt, ap);
	va_end(ap);
	sLineCount++;
}


// ---------------------------------------------------------------------------
// TEST_ASSERT macro
// ---------------------------------------------------------------------------

#define TEST_ASSERT(name, condition) \
	do { \
		bigtime_t _t0 = system_time(); \
		bool _ok = (condition); \
		bigtime_t _dt = system_time() - _t0; \
		if (_ok) { \
			trace("  PASS: %s  (%lld us)\n", name, (long long)_dt); \
			sPassCount++; \
		} else { \
			trace("  FAIL: %s  (line %d, %lld us)\n", \
				name, __LINE__, (long long)_dt); \
			sFailCount++; \
		} \
	} while (0)


// ---------------------------------------------------------------------------
// Thread arg structs & functions
// ---------------------------------------------------------------------------

struct reader_args {
	kosm_ray_id		ray;
	void*			buffer;
	size_t			bufferSize;
	size_t			gotSize;
	kosm_handle_t*	handles;
	size_t			handleBufCount;
	size_t			gotHandleCount;
	uint32			flags;
	bigtime_t		timeout;
	status_t		result;
	bigtime_t		elapsed;
};


static status_t
reader_thread_func(void* data)
{
	reader_args* a = (reader_args*)data;
	a->gotSize = a->bufferSize;
	a->gotHandleCount = a->handleBufCount;
	bigtime_t t0 = system_time();
	a->result = kosm_ray_read_etc(a->ray, a->buffer, &a->gotSize,
		a->handles, &a->gotHandleCount, a->flags, a->timeout);
	a->elapsed = system_time() - t0;
	return B_OK;
}


struct writer_args {
	kosm_ray_id		ray;
	const void*		data;
	size_t			dataSize;
	const kosm_handle_t* handles;
	size_t			handleCount;
	uint32			flags;
	bigtime_t		timeout;
	bigtime_t		delay;		// snooze before writing
	status_t		result;
	bigtime_t		elapsed;
};


static status_t
writer_thread_func(void* data)
{
	writer_args* a = (writer_args*)data;
	if (a->delay > 0)
		snooze(a->delay);
	bigtime_t t0 = system_time();
	a->result = kosm_ray_write_etc(a->ray, a->data, a->dataSize,
		a->handles, a->handleCount, a->flags, a->timeout);
	a->elapsed = system_time() - t0;
	return B_OK;
}


struct close_peer_args {
	kosm_ray_id		ray;
	bigtime_t		delay;
	status_t		result;
};


static status_t
close_peer_thread_func(void* data)
{
	close_peer_args* a = (close_peer_args*)data;
	if (a->delay > 0)
		snooze(a->delay);
	a->result = kosm_close_ray(a->ray);
	return B_OK;
}


struct concurrent_writer_args {
	kosm_ray_id		ray;
	int				count;
	int				threadIndex;
	int				errors;
};


static status_t
concurrent_writer_func(void* data)
{
	concurrent_writer_args* a = (concurrent_writer_args*)data;
	a->errors = 0;
	for (int i = 0; i < a->count; i++) {
		uint8 msg[8];
		msg[0] = (uint8)a->threadIndex;
		msg[1] = (uint8)i;
		memset(msg + 2, 0, 6);
		status_t s = kosm_ray_write_etc(a->ray, msg, sizeof(msg),
			NULL, 0, B_RELATIVE_TIMEOUT, 2000000);
		if (s != B_OK)
			a->errors++;
	}
	return B_OK;
}


struct churn_args {
	int		count;
	int		errors;
};


static status_t
churn_thread_func(void* data)
{
	churn_args* a = (churn_args*)data;
	a->errors = 0;
	for (int i = 0; i < a->count; i++) {
		kosm_ray_id ep0, ep1;
		status_t s = kosm_create_ray(&ep0, &ep1, 0);
		if (s != B_OK) { a->errors++; continue; }
		uint8 msg = (uint8)i;
		s = kosm_ray_write(ep0, &msg, 1, NULL, 0, 0);
		if (s != B_OK) a->errors++;
		uint8 buf;
		size_t sz = 1;
		size_t hc = 0;
		s = kosm_ray_read(ep1, &buf, &sz, NULL, &hc, 0);
		if (s != B_OK) a->errors++;
		kosm_close_ray(ep0);
		kosm_close_ray(ep1);
	}
	return B_OK;
}


// Mobile simulation structs

struct mobile_worker_args {
	kosm_ray_id		requestRay;		// reads requests from dispatcher
	kosm_ray_id		responseRay;	// writes responses back
	int				count;
	int				processed;
	bigtime_t		maxLatency;
};


static status_t
mobile_worker_func(void* data)
{
	mobile_worker_args* a = (mobile_worker_args*)data;
	a->processed = 0;
	a->maxLatency = 0;

	for (int i = 0; i < a->count; i++) {
		uint8 buf[64];
		size_t sz = sizeof(buf);
		size_t hc = 0;
		bigtime_t t0 = system_time();
		status_t s = kosm_ray_read_etc(a->requestRay, buf, &sz,
			NULL, &hc, B_RELATIVE_TIMEOUT, 500000);
		if (s != B_OK)
			break;

		// Simulate work
		buf[0] = buf[0] + 1;

		s = kosm_ray_write(a->responseRay, buf, sz, NULL, 0, 0);
		bigtime_t dt = system_time() - t0;
		if (dt > a->maxLatency)
			a->maxLatency = dt;
		if (s == B_OK)
			a->processed++;
	}
	return B_OK;
}


struct mobile_dispatcher_args {
	kosm_ray_id*	workerRequestRays;	// array of rays, one per worker
	kosm_ray_id*	workerResponseRays;
	int				workerCount;
	int				totalRequests;
	int				dispatched;
	int				received;
	bigtime_t		maxLatency;
};


static status_t
mobile_dispatcher_func(void* data)
{
	mobile_dispatcher_args* a = (mobile_dispatcher_args*)data;
	a->dispatched = 0;
	a->received = 0;
	a->maxLatency = 0;

	for (int i = 0; i < a->totalRequests; i++) {
		int w = i % a->workerCount;
		uint8 msg[64];
		memset(msg, 0, sizeof(msg));
		msg[0] = (uint8)i;
		msg[1] = (uint8)w;

		bigtime_t t0 = system_time();
		status_t s = kosm_ray_write(a->workerRequestRays[w],
			msg, sizeof(msg), NULL, 0, 0);
		if (s != B_OK) continue;
		a->dispatched++;

		uint8 resp[64];
		size_t sz = sizeof(resp);
		size_t hc = 0;
		s = kosm_ray_read_etc(a->workerResponseRays[w], resp, &sz,
			NULL, &hc, B_RELATIVE_TIMEOUT, 500000);
		bigtime_t dt = system_time() - t0;
		if (dt > a->maxLatency)
			a->maxLatency = dt;
		if (s == B_OK)
			a->received++;
	}
	return B_OK;
}


// ---------------------------------------------------------------------------
// ========== BASIC ==========
// ---------------------------------------------------------------------------

static void
test_create_close()
{
	trace("\n--- test_create_close ---\n");
	bigtime_t t0 = system_time();

	kosm_ray_id ep0 = -1, ep1 = -1;
	status_t s = kosm_create_ray(&ep0, &ep1, 0);
	trace("    kosm_create_ray -> ep0=%ld, ep1=%ld, %s\n",
		(long)ep0, (long)ep1, strerror(s));
	TEST_ASSERT("create returns B_OK", s == B_OK);
	TEST_ASSERT("ep0 valid", ep0 >= 0);
	TEST_ASSERT("ep1 valid", ep1 >= 0);

	s = kosm_close_ray(ep0);
	trace_call("close ep0", s);
	TEST_ASSERT("close ep0", s == B_OK);

	s = kosm_close_ray(ep1);
	trace_call("close ep1", s);
	TEST_ASSERT("close ep1", s == B_OK);

	s = kosm_close_ray(ep0);
	trace_call("double close ep0", s);
	TEST_ASSERT("double close fails", s != B_OK);

	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


static void
test_write_read()
{
	trace("\n--- test_write_read ---\n");
	bigtime_t t0 = system_time();

	kosm_ray_id ep0, ep1;
	status_t s = kosm_create_ray(&ep0, &ep1, 0);
	TEST_ASSERT("create", s == B_OK);

	uint8 sendData[64];
	for (int i = 0; i < 64; i++)
		sendData[i] = (uint8)(i * 3 + 7);

	s = kosm_ray_write(ep0, sendData, sizeof(sendData), NULL, 0, 0);
	trace_call("write 64 bytes", s);
	TEST_ASSERT("write succeeds", s == B_OK);

	uint8 recvData[64];
	memset(recvData, 0, sizeof(recvData));
	size_t sz = sizeof(recvData);
	size_t hc = 0;
	s = kosm_ray_read(ep1, recvData, &sz, NULL, &hc, 0);
	trace_call("read", s);
	TEST_ASSERT("read succeeds", s == B_OK);
	TEST_ASSERT("read size matches", sz == 64);
	TEST_ASSERT("data matches", memcmp(sendData, recvData, 64) == 0);

	kosm_close_ray(ep0);
	kosm_close_ray(ep1);
	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


static void
test_bidirectional()
{
	trace("\n--- test_bidirectional ---\n");
	bigtime_t t0 = system_time();

	kosm_ray_id ep0, ep1;
	status_t s = kosm_create_ray(&ep0, &ep1, 0);
	TEST_ASSERT("create", s == B_OK);

	// ep0 -> ep1
	uint32 val1 = 0xDEADBEEF;
	s = kosm_ray_write(ep0, &val1, sizeof(val1), NULL, 0, 0);
	TEST_ASSERT("write ep0->ep1", s == B_OK);

	uint32 recv1 = 0;
	size_t sz = sizeof(recv1);
	size_t hc = 0;
	s = kosm_ray_read(ep1, &recv1, &sz, NULL, &hc, 0);
	TEST_ASSERT("read on ep1", s == B_OK);
	TEST_ASSERT("data ep0->ep1", recv1 == 0xDEADBEEF);

	// ep1 -> ep0
	uint32 val2 = 0xCAFEBABE;
	s = kosm_ray_write(ep1, &val2, sizeof(val2), NULL, 0, 0);
	TEST_ASSERT("write ep1->ep0", s == B_OK);

	uint32 recv2 = 0;
	sz = sizeof(recv2);
	hc = 0;
	s = kosm_ray_read(ep0, &recv2, &sz, NULL, &hc, 0);
	TEST_ASSERT("read on ep0", s == B_OK);
	TEST_ASSERT("data ep1->ep0", recv2 == 0xCAFEBABE);

	kosm_close_ray(ep0);
	kosm_close_ray(ep1);
	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


static void
test_zero_length()
{
	trace("\n--- test_zero_length ---\n");
	bigtime_t t0 = system_time();

	kosm_ray_id ep0, ep1;
	status_t s = kosm_create_ray(&ep0, &ep1, 0);
	TEST_ASSERT("create", s == B_OK);

	s = kosm_ray_write(ep0, NULL, 0, NULL, 0, 0);
	trace_call("write zero-length", s);
	TEST_ASSERT("write zero-length", s == B_OK);

	uint8 buf[4];
	size_t sz = sizeof(buf);
	size_t hc = 0;
	s = kosm_ray_read(ep1, buf, &sz, NULL, &hc, 0);
	trace_call("read", s);
	TEST_ASSERT("read succeeds", s == B_OK);
	TEST_ASSERT("read size is 0", sz == 0);

	kosm_close_ray(ep0);
	kosm_close_ray(ep1);
	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


static void
test_multiple_messages()
{
	trace("\n--- test_multiple_messages ---\n");
	bigtime_t t0 = system_time();

	kosm_ray_id ep0, ep1;
	status_t s = kosm_create_ray(&ep0, &ep1, 0);
	TEST_ASSERT("create", s == B_OK);

	// Write 10 messages
	for (int i = 0; i < 10; i++) {
		uint32 val = (uint32)(i * 1000 + 42);
		s = kosm_ray_write(ep0, &val, sizeof(val), NULL, 0, 0);
		if (s != B_OK)
			trace("    write[%d] failed: %s\n", i, strerror(s));
	}
	TEST_ASSERT("all 10 writes succeed", s == B_OK);

	// Read 10 messages, verify FIFO order
	bool allOK = true;
	for (int i = 0; i < 10; i++) {
		uint32 val = 0;
		size_t sz = sizeof(val);
		size_t hc = 0;
		s = kosm_ray_read(ep1, &val, &sz, NULL, &hc, 0);
		uint32 expected = (uint32)(i * 1000 + 42);
		if (s != B_OK || val != expected) {
			trace("    read[%d]: got=%lu, expected=%lu, s=%s\n",
				i, (unsigned long)val, (unsigned long)expected, strerror(s));
			allOK = false;
		}
	}
	TEST_ASSERT("FIFO order preserved", allOK);

	kosm_close_ray(ep0);
	kosm_close_ray(ep1);
	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


static void
test_get_ray_info()
{
	trace("\n--- test_get_ray_info ---\n");
	bigtime_t t0 = system_time();

	kosm_ray_id ep0, ep1;
	status_t s = kosm_create_ray(&ep0, &ep1, 0);
	TEST_ASSERT("create", s == B_OK);

	s = kosm_ray_set_qos(ep0, KOSM_QOS_USER_INTERACTIVE);
	TEST_ASSERT("set qos", s == B_OK);

	// Write 3 messages to ep0 (they queue on ep1's read side)
	for (int i = 0; i < 3; i++) {
		uint32 val = i;
		kosm_ray_write(ep0, &val, sizeof(val), NULL, 0, 0);
	}

	kosm_ray_info info;
	memset(&info, 0, sizeof(info));
	s = kosm_get_ray_info(ep0, &info);
	trace_call("get_ray_info(ep0)", s);
	trace("    info: ray=%ld, peer=%ld, team=%ld, queue=%lu, qos=%u\n",
		(long)info.ray, (long)info.peer, (long)info.team,
		(unsigned long)info.queue_count, info.qos_class);

	TEST_ASSERT("get_info succeeds", s == B_OK);
	TEST_ASSERT("info.ray matches", info.ray == ep0);
	TEST_ASSERT("info.peer matches", info.peer == ep1);
	TEST_ASSERT("info.qos_class", info.qos_class == KOSM_QOS_USER_INTERACTIVE);

	// Check ep1 queue count (messages land on receiver side)
	kosm_ray_info info1;
	memset(&info1, 0, sizeof(info1));
	s = kosm_get_ray_info(ep1, &info1);
	trace("    ep1 info: queue_count=%lu\n", (unsigned long)info1.queue_count);
	TEST_ASSERT("ep1 queue_count is 3", info1.queue_count == 3);

	kosm_close_ray(ep0);
	kosm_close_ray(ep1);
	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


static void
test_read_truncation()
{
	trace("\n--- test_read_truncation ---\n");
	bigtime_t t0 = system_time();

	kosm_ray_id ep0, ep1;
	status_t s = kosm_create_ray(&ep0, &ep1, 0);
	TEST_ASSERT("create", s == B_OK);

	uint8 bigData[256];
	for (int i = 0; i < 256; i++)
		bigData[i] = (uint8)i;

	s = kosm_ray_write(ep0, bigData, sizeof(bigData), NULL, 0, 0);
	TEST_ASSERT("write 256 bytes", s == B_OK);

	uint8 smallBuf[64];
	memset(smallBuf, 0xFF, sizeof(smallBuf));
	size_t sz = sizeof(smallBuf);
	size_t hc = 0;
	s = kosm_ray_read(ep1, smallBuf, &sz, NULL, &hc, 0);
	trace("    read with 64-byte buffer: s=%s, dataSize=%zu\n",
		strerror(s), sz);
	TEST_ASSERT("read succeeds", s == B_OK);
	TEST_ASSERT("reported size is true size (256)", sz == 256);
	TEST_ASSERT("first 64 bytes correct", memcmp(smallBuf, bigData, 64) == 0);

	kosm_close_ray(ep0);
	kosm_close_ray(ep1);
	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


// ---------------------------------------------------------------------------
// ========== LIMITS ==========
// ---------------------------------------------------------------------------

static void
test_max_data_size()
{
	trace("\n--- test_max_data_size ---\n");
	bigtime_t t0 = system_time();

	kosm_ray_id ep0, ep1;
	status_t s = kosm_create_ray(&ep0, &ep1, 0);
	TEST_ASSERT("create", s == B_OK);

	// Write exactly 256KB
	size_t maxSize = KOSM_RAY_MAX_DATA_SIZE;
	uint8* bigBuf = (uint8*)malloc(maxSize + 1);
	TEST_ASSERT("alloc buffer", bigBuf != NULL);

	for (size_t i = 0; i < maxSize; i++)
		bigBuf[i] = (uint8)(i & 0xFF);

	s = kosm_ray_write(ep0, bigBuf, maxSize, NULL, 0, 0);
	trace_call("write 256KB", s);
	TEST_ASSERT("write max size succeeds", s == B_OK);

	// Read it back
	uint8* recvBuf = (uint8*)malloc(maxSize);
	size_t sz = maxSize;
	size_t hc = 0;
	s = kosm_ray_read(ep1, recvBuf, &sz, NULL, &hc, 0);
	TEST_ASSERT("read 256KB", s == B_OK);
	TEST_ASSERT("size matches", sz == maxSize);
	TEST_ASSERT("data integrity", memcmp(bigBuf, recvBuf, maxSize) == 0);

	// Write 256KB + 1 -> TOO_LARGE
	s = kosm_ray_write(ep0, bigBuf, maxSize + 1, NULL, 0, 0);
	trace_call("write 256KB+1", s);
	TEST_ASSERT("over-size fails KOSM_RAY_TOO_LARGE",
		s == KOSM_RAY_TOO_LARGE);

	free(bigBuf);
	free(recvBuf);
	kosm_close_ray(ep0);
	kosm_close_ray(ep1);
	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


static void
test_max_handles()
{
	trace("\n--- test_max_handles ---\n");
	bigtime_t t0 = system_time();

	kosm_ray_id ep0, ep1;
	status_t s = kosm_create_ray(&ep0, &ep1, 0);
	TEST_ASSERT("create", s == B_OK);

	// Create 65 mutexes as handles
	kosm_handle_t handles[65];
	kosm_mutex_id mutexes[65];
	for (int i = 0; i < 65; i++) {
		char name[32];
		snprintf(name, sizeof(name), "htest_%d", i);
		mutexes[i] = kosm_create_mutex(name, 0);
		handles[i].type = KOSM_HANDLE_MUTEX;
		handles[i].id = mutexes[i];
	}

	// Send exactly 64 handles (KOSM_RAY_COPY_HANDLES to keep them)
	uint8 msg = 0x42;
	s = kosm_ray_write(ep0, &msg, 1, handles, KOSM_RAY_MAX_HANDLES,
		KOSM_RAY_COPY_HANDLES);
	trace_call("write 64 handles", s);
	TEST_ASSERT("64 handles succeeds", s == B_OK);

	// Drain the message
	uint8 dummy;
	size_t sz = 1;
	kosm_handle_t recvH[64];
	size_t rhc = 64;
	kosm_ray_read(ep1, &dummy, &sz, recvH, &rhc, 0);

	// Send 65 handles -> TOO_MANY_HANDLES
	s = kosm_ray_write(ep0, &msg, 1, handles, 65, KOSM_RAY_COPY_HANDLES);
	trace_call("write 65 handles", s);
	TEST_ASSERT("65 handles fails KOSM_RAY_TOO_MANY_HANDLES",
		s == KOSM_RAY_TOO_MANY_HANDLES);

	for (int i = 0; i < 65; i++)
		kosm_delete_mutex(mutexes[i]);
	kosm_close_ray(ep0);
	kosm_close_ray(ep1);
	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


static void
test_queue_full()
{
	trace("\n--- test_queue_full ---\n");
	bigtime_t t0 = system_time();

	kosm_ray_id ep0, ep1;
	status_t s = kosm_create_ray(&ep0, &ep1, 0);
	TEST_ASSERT("create", s == B_OK);

	// Fill queue with 256 messages
	int written = 0;
	for (int i = 0; i < KOSM_RAY_MAX_QUEUE_MESSAGES; i++) {
		uint8 msg = (uint8)i;
		s = kosm_ray_write(ep0, &msg, 1, NULL, 0, 0);
		if (s == B_OK)
			written++;
		else
			break;
	}
	trace("    filled %d/%d messages\n", written, KOSM_RAY_MAX_QUEUE_MESSAGES);
	TEST_ASSERT("filled 256 messages", written == KOSM_RAY_MAX_QUEUE_MESSAGES);

	// 257th should fail
	uint8 extra = 0xFF;
	s = kosm_ray_write(ep0, &extra, 1, NULL, 0, 0);
	trace_call("write 257th", s);
	TEST_ASSERT("queue full returns WOULD_BLOCK", s == B_WOULD_BLOCK);

	// Read one to free space
	uint8 buf;
	size_t sz = 1;
	size_t hc = 0;
	kosm_ray_read(ep1, &buf, &sz, NULL, &hc, 0);

	// Now write should succeed again
	s = kosm_ray_write(ep0, &extra, 1, NULL, 0, 0);
	trace_call("write after drain", s);
	TEST_ASSERT("write after drain succeeds", s == B_OK);

	// Drain remaining
	for (int i = 0; i < KOSM_RAY_MAX_QUEUE_MESSAGES; i++) {
		sz = 1;
		hc = 0;
		kosm_ray_read(ep1, &buf, &sz, NULL, &hc, 0);
	}

	kosm_close_ray(ep0);
	kosm_close_ray(ep1);
	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


static void
test_invalid_id()
{
	trace("\n--- test_invalid_id ---\n");
	bigtime_t t0 = system_time();

	status_t s = kosm_close_ray(-1);
	trace_call("close(-1)", s);
	TEST_ASSERT("close(-1) fails", s != B_OK);

	s = kosm_close_ray(99999);
	trace_call("close(99999)", s);
	TEST_ASSERT("close(99999) fails", s != B_OK);

	uint8 buf[4];
	s = kosm_ray_write(-1, buf, 4, NULL, 0, 0);
	trace_call("write(-1)", s);
	TEST_ASSERT("write(-1) fails", s != B_OK);

	size_t sz = 4;
	size_t hc = 0;
	s = kosm_ray_read(-1, buf, &sz, NULL, &hc, 0);
	trace_call("read(-1)", s);
	TEST_ASSERT("read(-1) fails", s != B_OK);

	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


static void
test_double_close()
{
	trace("\n--- test_double_close ---\n");
	bigtime_t t0 = system_time();

	kosm_ray_id ep0, ep1;
	status_t s = kosm_create_ray(&ep0, &ep1, 0);
	TEST_ASSERT("create", s == B_OK);

	s = kosm_close_ray(ep0);
	trace_call("close ep0 first", s);
	TEST_ASSERT("first close succeeds", s == B_OK);

	s = kosm_close_ray(ep0);
	trace_call("close ep0 second", s);
	TEST_ASSERT("second close fails", s != B_OK);

	s = kosm_close_ray(ep1);
	trace_call("close ep1", s);
	TEST_ASSERT("close ep1 succeeds", s == B_OK);

	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


// ---------------------------------------------------------------------------
// ========== PEER CLOSE ==========
// ---------------------------------------------------------------------------

static void
test_write_after_peer_close()
{
	trace("\n--- test_write_after_peer_close ---\n");
	bigtime_t t0 = system_time();

	kosm_ray_id ep0, ep1;
	status_t s = kosm_create_ray(&ep0, &ep1, 0);
	TEST_ASSERT("create", s == B_OK);

	kosm_close_ray(ep1);
	trace("    closed ep1 (peer)\n");

	uint32 val = 123;
	s = kosm_ray_write(ep0, &val, sizeof(val), NULL, 0, 0);
	trace_call("write after peer close", s);
	TEST_ASSERT("write returns PEER_CLOSED_ERROR",
		s == KOSM_RAY_PEER_CLOSED_ERROR);

	kosm_close_ray(ep0);
	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


static void
test_read_empty_after_peer_close()
{
	trace("\n--- test_read_empty_after_peer_close ---\n");
	bigtime_t t0 = system_time();

	kosm_ray_id ep0, ep1;
	status_t s = kosm_create_ray(&ep0, &ep1, 0);
	TEST_ASSERT("create", s == B_OK);

	kosm_close_ray(ep0);
	trace("    closed ep0 (sender, no messages)\n");

	uint8 buf[4];
	size_t sz = sizeof(buf);
	size_t hc = 0;
	s = kosm_ray_read(ep1, buf, &sz, NULL, &hc, 0);
	trace_call("read from empty + peer closed", s);
	TEST_ASSERT("read returns PEER_CLOSED_ERROR",
		s == KOSM_RAY_PEER_CLOSED_ERROR);

	kosm_close_ray(ep1);
	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


static void
test_drain_after_peer_close()
{
	trace("\n--- test_drain_after_peer_close ---\n");
	bigtime_t t0 = system_time();

	kosm_ray_id ep0, ep1;
	status_t s = kosm_create_ray(&ep0, &ep1, 0);
	TEST_ASSERT("create", s == B_OK);

	// Write 3 messages then close sender
	for (int i = 0; i < 3; i++) {
		uint32 val = (uint32)(i + 100);
		kosm_ray_write(ep0, &val, sizeof(val), NULL, 0, 0);
	}
	kosm_close_ray(ep0);
	trace("    closed ep0 after 3 messages\n");

	// Drain all 3 — should succeed
	bool drainOK = true;
	for (int i = 0; i < 3; i++) {
		uint32 val = 0;
		size_t sz = sizeof(val);
		size_t hc = 0;
		s = kosm_ray_read(ep1, &val, &sz, NULL, &hc, 0);
		trace("    drain[%d]: val=%lu, s=%s\n", i, (unsigned long)val,
			strerror(s));
		if (s != B_OK || val != (uint32)(i + 100))
			drainOK = false;
	}
	TEST_ASSERT("drain 3 messages succeeds", drainOK);

	// 4th read -> PEER_CLOSED
	uint32 dummy;
	size_t sz = sizeof(dummy);
	size_t hc = 0;
	s = kosm_ray_read(ep1, &dummy, &sz, NULL, &hc, 0);
	trace_call("read 4th (empty, peer gone)", s);
	TEST_ASSERT("4th read returns PEER_CLOSED_ERROR",
		s == KOSM_RAY_PEER_CLOSED_ERROR);

	kosm_close_ray(ep1);
	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


static void
test_close_wakes_blocked_reader()
{
	trace("\n--- test_close_wakes_blocked_reader ---\n");
	bigtime_t t0 = system_time();

	kosm_ray_id ep0, ep1;
	status_t s = kosm_create_ray(&ep0, &ep1, 0);
	TEST_ASSERT("create", s == B_OK);

	// Spawn reader thread that blocks on ep1
	reader_args rargs;
	memset(&rargs, 0, sizeof(rargs));
	uint8 buf[64];
	rargs.ray = ep1;
	rargs.buffer = buf;
	rargs.bufferSize = sizeof(buf);
	rargs.timeout = 2000000;	// 2 seconds max
	rargs.flags = B_RELATIVE_TIMEOUT;

	thread_id tid = spawn_thread(reader_thread_func, "blocked_reader",
		B_NORMAL_PRIORITY, &rargs);
	resume_thread(tid);
	snooze(50000);	// let reader block

	// Close peer
	kosm_close_ray(ep0);
	trace("    closed ep0 while reader blocked on ep1\n");

	status_t exitVal;
	wait_for_thread(tid, &exitVal);
	trace("    reader result: %s, elapsed=%lld us\n",
		strerror(rargs.result), (long long)rargs.elapsed);

	TEST_ASSERT("reader unblocked", rargs.elapsed < 1000000);
	TEST_ASSERT("reader got peer closed error",
		rargs.result == KOSM_RAY_PEER_CLOSED_ERROR);

	kosm_close_ray(ep1);
	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


static void
test_close_wakes_blocked_writer()
{
	trace("\n--- test_close_wakes_blocked_writer ---\n");
	bigtime_t t0 = system_time();

	kosm_ray_id ep0, ep1;
	status_t s = kosm_create_ray(&ep0, &ep1, 0);
	TEST_ASSERT("create", s == B_OK);

	// Fill queue
	for (int i = 0; i < KOSM_RAY_MAX_QUEUE_MESSAGES; i++) {
		uint8 msg = (uint8)i;
		kosm_ray_write(ep0, &msg, 1, NULL, 0, 0);
	}
	trace("    filled queue with %d messages\n", KOSM_RAY_MAX_QUEUE_MESSAGES);

	// Spawn writer thread that blocks
	uint8 extraMsg = 0xFF;
	writer_args wargs;
	memset(&wargs, 0, sizeof(wargs));
	wargs.ray = ep0;
	wargs.data = &extraMsg;
	wargs.dataSize = 1;
	wargs.flags = B_RELATIVE_TIMEOUT;
	wargs.timeout = 2000000;	// 2 seconds max
	wargs.delay = 0;

	thread_id tid = spawn_thread(writer_thread_func, "blocked_writer",
		B_NORMAL_PRIORITY, &wargs);
	resume_thread(tid);
	snooze(50000);	// let writer block

	// Close the peer (reader side)
	kosm_close_ray(ep1);
	trace("    closed ep1 while writer blocked on ep0\n");

	status_t exitVal;
	wait_for_thread(tid, &exitVal);
	trace("    writer result: %s, elapsed=%lld us\n",
		strerror(wargs.result), (long long)wargs.elapsed);

	TEST_ASSERT("writer unblocked", wargs.elapsed < 1000000);
	TEST_ASSERT("writer got error", wargs.result != B_OK);

	kosm_close_ray(ep0);
	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


// ---------------------------------------------------------------------------
// ========== HANDLES ==========
// ---------------------------------------------------------------------------

static void
test_handle_ray_move()
{
	trace("\n--- test_handle_ray_move ---\n");
	bigtime_t t0 = system_time();

	kosm_ray_id ep0, ep1;
	status_t s = kosm_create_ray(&ep0, &ep1, 0);
	TEST_ASSERT("create transport pair", s == B_OK);

	// Create pair B to send as handle
	kosm_ray_id bep0, bep1;
	s = kosm_create_ray(&bep0, &bep1, 0);
	TEST_ASSERT("create handle pair", s == B_OK);

	kosm_handle_t h;
	h.type = KOSM_HANDLE_RAY;
	h.id = bep0;

	uint8 msg = 0x01;
	s = kosm_ray_write(ep0, &msg, 1, &h, 1, 0);
	trace_call("write with ray handle (move)", s);
	TEST_ASSERT("write with ray handle", s == B_OK);

	// Read on other side
	uint8 recvMsg;
	size_t sz = 1;
	kosm_handle_t recvH;
	size_t rhc = 1;
	s = kosm_ray_read(ep1, &recvMsg, &sz, &recvH, &rhc, 0);
	trace("    read: s=%s, handleCount=%zu, handle type=%u id=%ld\n",
		strerror(s), rhc, recvH.type, (long)recvH.id);
	TEST_ASSERT("read succeeds", s == B_OK);
	TEST_ASSERT("got 1 handle", rhc == 1);
	TEST_ASSERT("handle type is RAY", recvH.type == KOSM_HANDLE_RAY);
	TEST_ASSERT("handle id matches", recvH.id == bep0);

	kosm_close_ray(bep0);
	kosm_close_ray(bep1);
	kosm_close_ray(ep0);
	kosm_close_ray(ep1);
	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


static void
test_handle_mutex()
{
	trace("\n--- test_handle_mutex ---\n");
	bigtime_t t0 = system_time();

	kosm_ray_id ep0, ep1;
	status_t s = kosm_create_ray(&ep0, &ep1, 0);
	TEST_ASSERT("create", s == B_OK);

	kosm_mutex_id mid = kosm_create_mutex("ray_handle_test", 0);
	TEST_ASSERT("create mutex", mid >= 0);

	kosm_handle_t h;
	h.type = KOSM_HANDLE_MUTEX;
	h.id = mid;

	uint8 msg = 0x02;
	s = kosm_ray_write(ep0, &msg, 1, &h, 1, KOSM_RAY_COPY_HANDLES);
	trace_call("write with mutex handle", s);
	TEST_ASSERT("write with mutex handle", s == B_OK);

	uint8 recvMsg;
	size_t sz = 1;
	kosm_handle_t recvH;
	size_t rhc = 1;
	s = kosm_ray_read(ep1, &recvMsg, &sz, &recvH, &rhc, 0);
	TEST_ASSERT("read succeeds", s == B_OK);
	TEST_ASSERT("got 1 handle", rhc == 1);
	TEST_ASSERT("handle type MUTEX", recvH.type == KOSM_HANDLE_MUTEX);
	TEST_ASSERT("handle id matches", recvH.id == mid);

	kosm_delete_mutex(mid);
	kosm_close_ray(ep0);
	kosm_close_ray(ep1);
	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


static void
test_handle_area_move()
{
	trace("\n--- test_handle_area_move ---\n");
	bigtime_t t0 = system_time();

	kosm_ray_id ep0, ep1;
	status_t s = kosm_create_ray(&ep0, &ep1, 0);
	TEST_ASSERT("create", s == B_OK);

	void* addr;
	area_id aid = create_area("ray_area_test", &addr, B_ANY_ADDRESS,
		B_PAGE_SIZE, B_NO_LOCK, B_READ_AREA | B_WRITE_AREA);
	trace("    create_area -> id=%ld\n", (long)aid);
	TEST_ASSERT("create area", aid >= 0);

	kosm_handle_t h;
	h.type = KOSM_HANDLE_AREA;
	h.id = aid;

	uint8 msg = 0x03;
	s = kosm_ray_write(ep0, &msg, 1, &h, 1, KOSM_RAY_COPY_HANDLES);
	trace_call("write with area handle", s);
	TEST_ASSERT("write with area handle", s == B_OK);

	uint8 recvMsg;
	size_t sz = 1;
	kosm_handle_t recvH;
	size_t rhc = 1;
	s = kosm_ray_read(ep1, &recvMsg, &sz, &recvH, &rhc, 0);
	TEST_ASSERT("read succeeds", s == B_OK);
	TEST_ASSERT("got 1 handle", rhc == 1);
	TEST_ASSERT("handle type AREA", recvH.type == KOSM_HANDLE_AREA);
	trace("    received area handle id=%ld (original=%ld)\n",
		(long)recvH.id, (long)aid);

	// Clean up - delete original area (clone if any is separate)
	delete_area(aid);
	if (recvH.id != aid)
		delete_area(recvH.id);
	kosm_close_ray(ep0);
	kosm_close_ray(ep1);
	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


static void
test_handle_sem_move()
{
	trace("\n--- test_handle_sem_move ---\n");
	bigtime_t t0 = system_time();

	kosm_ray_id ep0, ep1;
	status_t s = kosm_create_ray(&ep0, &ep1, 0);
	TEST_ASSERT("create", s == B_OK);

	sem_id sid = create_sem(1, "ray_sem_test");
	TEST_ASSERT("create sem", sid >= 0);

	kosm_handle_t h;
	h.type = KOSM_HANDLE_SEM;
	h.id = sid;

	uint8 msg = 0x04;
	s = kosm_ray_write(ep0, &msg, 1, &h, 1, KOSM_RAY_COPY_HANDLES);
	trace_call("write with sem handle", s);
	TEST_ASSERT("write with sem handle", s == B_OK);

	uint8 recvMsg;
	size_t sz = 1;
	kosm_handle_t recvH;
	size_t rhc = 1;
	s = kosm_ray_read(ep1, &recvMsg, &sz, &recvH, &rhc, 0);
	TEST_ASSERT("read succeeds", s == B_OK);
	TEST_ASSERT("got 1 handle", rhc == 1);
	TEST_ASSERT("handle type SEM", recvH.type == KOSM_HANDLE_SEM);
	TEST_ASSERT("handle id matches", recvH.id == sid);

	delete_sem(sid);
	kosm_close_ray(ep0);
	kosm_close_ray(ep1);
	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


static void
test_handle_fd_not_supported()
{
	trace("\n--- test_handle_fd_not_supported ---\n");
	bigtime_t t0 = system_time();

	kosm_ray_id ep0, ep1;
	status_t s = kosm_create_ray(&ep0, &ep1, 0);
	TEST_ASSERT("create", s == B_OK);

	kosm_handle_t h;
	h.type = KOSM_HANDLE_FD;
	h.id = 1;	// stdout

	uint8 msg = 0x05;
	s = kosm_ray_write(ep0, &msg, 1, &h, 1, 0);
	trace_call("write with FD handle", s);
	TEST_ASSERT("FD handle not supported", s != B_OK);

	kosm_close_ray(ep0);
	kosm_close_ray(ep1);
	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


static void
test_handle_invalid()
{
	trace("\n--- test_handle_invalid ---\n");
	bigtime_t t0 = system_time();

	kosm_ray_id ep0, ep1;
	status_t s = kosm_create_ray(&ep0, &ep1, 0);
	TEST_ASSERT("create", s == B_OK);

	// Unknown type
	kosm_handle_t h;
	h.type = 0xFF;
	h.id = 1;

	uint8 msg = 0x06;
	s = kosm_ray_write(ep0, &msg, 1, &h, 1, 0);
	trace_call("write with unknown handle type", s);
	TEST_ASSERT("unknown handle type fails", s != B_OK);

	// Bad mutex id
	h.type = KOSM_HANDLE_MUTEX;
	h.id = 999999;
	s = kosm_ray_write(ep0, &msg, 1, &h, 1, 0);
	trace_call("write with bad mutex id", s);
	TEST_ASSERT("bad handle id fails", s != B_OK);

	kosm_close_ray(ep0);
	kosm_close_ray(ep1);
	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


// ---------------------------------------------------------------------------
// ========== PEEK & FLAGS ==========
// ---------------------------------------------------------------------------

static void
test_peek()
{
	trace("\n--- test_peek ---\n");
	bigtime_t t0 = system_time();

	kosm_ray_id ep0, ep1;
	status_t s = kosm_create_ray(&ep0, &ep1, 0);
	TEST_ASSERT("create", s == B_OK);

	uint32 val = 0xBAADF00D;
	s = kosm_ray_write(ep0, &val, sizeof(val), NULL, 0, 0);
	TEST_ASSERT("write", s == B_OK);

	// Peek
	uint32 peek1 = 0;
	size_t sz = sizeof(peek1);
	size_t hc = 0;
	s = kosm_ray_read(ep1, &peek1, &sz, NULL, &hc, KOSM_RAY_PEEK);
	trace("    peek1: val=0x%08lx, s=%s\n", (unsigned long)peek1, strerror(s));
	TEST_ASSERT("peek succeeds", s == B_OK);
	TEST_ASSERT("peek data correct", peek1 == 0xBAADF00D);

	// Peek again — same data
	uint32 peek2 = 0;
	sz = sizeof(peek2);
	hc = 0;
	s = kosm_ray_read(ep1, &peek2, &sz, NULL, &hc, KOSM_RAY_PEEK);
	TEST_ASSERT("peek again succeeds", s == B_OK);
	TEST_ASSERT("peek again same data", peek2 == 0xBAADF00D);

	// Real read — dequeues
	uint32 real = 0;
	sz = sizeof(real);
	hc = 0;
	s = kosm_ray_read(ep1, &real, &sz, NULL, &hc, 0);
	TEST_ASSERT("real read succeeds", s == B_OK);
	TEST_ASSERT("real read correct", real == 0xBAADF00D);

	// Queue should be empty now
	uint32 empty = 0;
	sz = sizeof(empty);
	hc = 0;
	s = kosm_ray_read(ep1, &empty, &sz, NULL, &hc, 0);
	trace_call("read after dequeue", s);
	TEST_ASSERT("queue empty after real read", s == B_WOULD_BLOCK);

	kosm_close_ray(ep0);
	kosm_close_ray(ep1);
	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


static void
test_copy_handles()
{
	trace("\n--- test_copy_handles ---\n");
	bigtime_t t0 = system_time();

	kosm_ray_id ep0, ep1;
	status_t s = kosm_create_ray(&ep0, &ep1, 0);
	TEST_ASSERT("create", s == B_OK);

	kosm_mutex_id mid = kosm_create_mutex("copy_test", 0);
	TEST_ASSERT("create mutex", mid >= 0);

	kosm_handle_t h;
	h.type = KOSM_HANDLE_MUTEX;
	h.id = mid;

	uint8 msg = 0x10;
	s = kosm_ray_write(ep0, &msg, 1, &h, 1, KOSM_RAY_COPY_HANDLES);
	trace_call("write with COPY_HANDLES", s);
	TEST_ASSERT("write with copy handles", s == B_OK);

	// Sender should still be able to use the mutex
	s = kosm_acquire_mutex(mid);
	TEST_ASSERT("sender can still acquire mutex", s == B_OK);
	kosm_release_mutex(mid);

	// Read the handle on the other side
	uint8 recvMsg;
	size_t sz = 1;
	kosm_handle_t recvH;
	size_t rhc = 1;
	s = kosm_ray_read(ep1, &recvMsg, &sz, &recvH, &rhc, 0);
	TEST_ASSERT("read succeeds", s == B_OK);
	TEST_ASSERT("handle type correct", recvH.type == KOSM_HANDLE_MUTEX);

	kosm_delete_mutex(mid);
	kosm_close_ray(ep0);
	kosm_close_ray(ep1);
	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


static void
test_timed_read_write()
{
	trace("\n--- test_timed_read_write ---\n");
	bigtime_t t0 = system_time();

	kosm_ray_id ep0, ep1;
	status_t s = kosm_create_ray(&ep0, &ep1, 0);
	TEST_ASSERT("create", s == B_OK);

	// Blocking read with timeout on empty queue
	uint8 buf[4];
	size_t sz = sizeof(buf);
	size_t hc = 0;
	bigtime_t rt0 = system_time();
	s = kosm_ray_read_etc(ep1, buf, &sz, NULL, &hc, B_RELATIVE_TIMEOUT,
		100000);
	bigtime_t elapsed = system_time() - rt0;
	trace("    timed read: s=%s, elapsed=%lld us\n", strerror(s),
		(long long)elapsed);
	TEST_ASSERT("timed read returns TIMED_OUT", s == B_TIMED_OUT);
	TEST_ASSERT("timeout accuracy (drift < 50ms)",
		elapsed >= 90000 && elapsed < 150000);

	kosm_close_ray(ep0);
	kosm_close_ray(ep1);
	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


// ---------------------------------------------------------------------------
// ========== QOS ==========
// ---------------------------------------------------------------------------

static void
test_qos_set_get()
{
	trace("\n--- test_qos_set_get ---\n");
	bigtime_t t0 = system_time();

	kosm_ray_id ep0, ep1;
	status_t s = kosm_create_ray(&ep0, &ep1, 0);
	TEST_ASSERT("create", s == B_OK);

	uint8 qosValues[] = {
		KOSM_QOS_DEFAULT, KOSM_QOS_UTILITY,
		KOSM_QOS_USER_INITIATED, KOSM_QOS_USER_INTERACTIVE
	};

	bool allOK = true;
	for (int i = 0; i < 4; i++) {
		s = kosm_ray_set_qos(ep0, qosValues[i]);
		if (s != B_OK) { allOK = false; continue; }

		kosm_ray_info info;
		memset(&info, 0, sizeof(info));
		kosm_get_ray_info(ep0, &info);
		trace("    set qos=%u, got=%u\n", qosValues[i], info.qos_class);
		if (info.qos_class != qosValues[i])
			allOK = false;
	}
	TEST_ASSERT("all QoS values set/get correctly", allOK);

	kosm_close_ray(ep0);
	kosm_close_ray(ep1);
	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


static void
test_qos_invalid()
{
	trace("\n--- test_qos_invalid ---\n");
	bigtime_t t0 = system_time();

	kosm_ray_id ep0, ep1;
	status_t s = kosm_create_ray(&ep0, &ep1, 0);
	TEST_ASSERT("create", s == B_OK);

	s = kosm_ray_set_qos(ep0, 4);
	trace_call("set_qos(4)", s);
	TEST_ASSERT("qos 4 fails", s == B_BAD_VALUE);

	s = kosm_ray_set_qos(ep0, 255);
	trace_call("set_qos(255)", s);
	TEST_ASSERT("qos 255 fails", s == B_BAD_VALUE);

	kosm_close_ray(ep0);
	kosm_close_ray(ep1);
	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


static void
test_qos_in_message()
{
	trace("\n--- test_qos_in_message ---\n");
	bigtime_t t0 = system_time();

	kosm_ray_id ep0, ep1;
	status_t s = kosm_create_ray(&ep0, &ep1, 0);
	TEST_ASSERT("create", s == B_OK);

	s = kosm_ray_set_qos(ep0, KOSM_QOS_USER_INTERACTIVE);
	TEST_ASSERT("set qos on sender", s == B_OK);

	uint32 val = 42;
	s = kosm_ray_write(ep0, &val, sizeof(val), NULL, 0, 0);
	TEST_ASSERT("write", s == B_OK);

	// Verify sender's qos via info
	kosm_ray_info info;
	memset(&info, 0, sizeof(info));
	kosm_get_ray_info(ep0, &info);
	trace("    ep0 qos_class=%u\n", info.qos_class);
	TEST_ASSERT("sender qos is USER_INTERACTIVE",
		info.qos_class == KOSM_QOS_USER_INTERACTIVE);

	// Read the message
	uint32 recv;
	size_t sz = sizeof(recv);
	size_t hc = 0;
	s = kosm_ray_read(ep1, &recv, &sz, NULL, &hc, 0);
	TEST_ASSERT("read", s == B_OK);

	kosm_close_ray(ep0);
	kosm_close_ray(ep1);
	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


// ---------------------------------------------------------------------------
// ========== WAIT ==========
// ---------------------------------------------------------------------------

static void
test_wait_readable()
{
	trace("\n--- test_wait_readable ---\n");
	bigtime_t t0 = system_time();

	kosm_ray_id ep0, ep1;
	status_t s = kosm_create_ray(&ep0, &ep1, 0);
	TEST_ASSERT("create", s == B_OK);

	// Spawn thread that writes after 50ms
	uint32 val = 0xF00D;
	writer_args wargs;
	memset(&wargs, 0, sizeof(wargs));
	wargs.ray = ep0;
	wargs.data = &val;
	wargs.dataSize = sizeof(val);
	wargs.timeout = 0;
	wargs.delay = 50000;

	thread_id tid = spawn_thread(writer_thread_func, "delayed_writer",
		B_NORMAL_PRIORITY, &wargs);
	resume_thread(tid);

	// Wait for readable
	uint32 observed = 0;
	bigtime_t wt0 = system_time();
	s = kosm_ray_wait(ep1, KOSM_RAY_READABLE, &observed,
		B_RELATIVE_TIMEOUT, 2000000);
	bigtime_t wdt = system_time() - wt0;
	trace("    wait: s=%s, observed=0x%lx, elapsed=%lld us\n",
		strerror(s), (unsigned long)observed, (long long)wdt);

	status_t exitVal;
	wait_for_thread(tid, &exitVal);

	TEST_ASSERT("wait returns B_OK", s == B_OK);
	TEST_ASSERT("READABLE observed",
		(observed & KOSM_RAY_READABLE) != 0);
	TEST_ASSERT("waited ~50ms", wdt >= 30000 && wdt < 500000);

	// Drain
	uint32 drain;
	size_t dsz = sizeof(drain);
	size_t dhc = 0;
	kosm_ray_read(ep1, &drain, &dsz, NULL, &dhc, 0);

	kosm_close_ray(ep0);
	kosm_close_ray(ep1);
	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


static void
test_wait_peer_closed()
{
	trace("\n--- test_wait_peer_closed ---\n");
	bigtime_t t0 = system_time();

	kosm_ray_id ep0, ep1;
	status_t s = kosm_create_ray(&ep0, &ep1, 0);
	TEST_ASSERT("create", s == B_OK);

	// Spawn thread that closes ep0 after 50ms
	close_peer_args cargs;
	cargs.ray = ep0;
	cargs.delay = 50000;
	cargs.result = B_ERROR;

	thread_id tid = spawn_thread(close_peer_thread_func, "closer",
		B_NORMAL_PRIORITY, &cargs);
	resume_thread(tid);

	uint32 observed = 0;
	bigtime_t wt0 = system_time();
	s = kosm_ray_wait(ep1, KOSM_RAY_PEER_CLOSED, &observed,
		B_RELATIVE_TIMEOUT, 2000000);
	bigtime_t wdt = system_time() - wt0;
	trace("    wait: s=%s, observed=0x%lx, elapsed=%lld us\n",
		strerror(s), (unsigned long)observed, (long long)wdt);

	status_t exitVal;
	wait_for_thread(tid, &exitVal);

	TEST_ASSERT("wait returns B_OK", s == B_OK);
	TEST_ASSERT("PEER_CLOSED observed",
		(observed & KOSM_RAY_PEER_CLOSED) != 0);

	kosm_close_ray(ep1);
	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


static void
test_wait_already_satisfied()
{
	trace("\n--- test_wait_already_satisfied ---\n");
	bigtime_t t0 = system_time();

	kosm_ray_id ep0, ep1;
	status_t s = kosm_create_ray(&ep0, &ep1, 0);
	TEST_ASSERT("create", s == B_OK);

	// Write message first
	uint32 val = 1;
	kosm_ray_write(ep0, &val, sizeof(val), NULL, 0, 0);

	// Wait for READABLE — should return immediately
	uint32 observed = 0;
	bigtime_t wt0 = system_time();
	s = kosm_ray_wait(ep1, KOSM_RAY_READABLE, &observed,
		B_RELATIVE_TIMEOUT, 1000000);
	bigtime_t wdt = system_time() - wt0;
	trace("    wait (already readable): s=%s, elapsed=%lld us\n",
		strerror(s), (long long)wdt);
	TEST_ASSERT("returns immediately", wdt < 10000);
	TEST_ASSERT("READABLE set", (observed & KOSM_RAY_READABLE) != 0);

	// Drain
	uint32 drain;
	size_t dsz = sizeof(drain);
	size_t dhc = 0;
	kosm_ray_read(ep1, &drain, &dsz, NULL, &dhc, 0);

	kosm_close_ray(ep0);
	kosm_close_ray(ep1);
	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


static void
test_wait_timeout()
{
	trace("\n--- test_wait_timeout ---\n");
	bigtime_t t0 = system_time();

	kosm_ray_id ep0, ep1;
	status_t s = kosm_create_ray(&ep0, &ep1, 0);
	TEST_ASSERT("create", s == B_OK);

	uint32 observed = 0;
	bigtime_t wt0 = system_time();
	s = kosm_ray_wait(ep1, KOSM_RAY_READABLE, &observed,
		B_RELATIVE_TIMEOUT, 100000);
	bigtime_t wdt = system_time() - wt0;
	trace("    wait timeout: s=%s, elapsed=%lld us\n",
		strerror(s), (long long)wdt);
	TEST_ASSERT("returns TIMED_OUT", s == B_TIMED_OUT);
	TEST_ASSERT("elapsed ~100ms", wdt >= 90000 && wdt < 200000);

	kosm_close_ray(ep0);
	kosm_close_ray(ep1);
	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


// ---------------------------------------------------------------------------
// ========== STRESS ==========
// ---------------------------------------------------------------------------

static void
test_stress_throughput()
{
	trace("\n--- test_stress_throughput ---\n");
	bigtime_t t0 = system_time();

	kosm_ray_id ep0, ep1;
	status_t s = kosm_create_ray(&ep0, &ep1, 0);
	TEST_ASSERT("create", s == B_OK);

	const int kCycles = 10000;
	uint8 sendBuf[64];
	uint8 recvBuf[64];
	memset(sendBuf, 0xAA, sizeof(sendBuf));

	int errors = 0;
	bigtime_t start = system_time();
	for (int i = 0; i < kCycles; i++) {
		sendBuf[0] = (uint8)(i & 0xFF);
		s = kosm_ray_write(ep0, sendBuf, sizeof(sendBuf), NULL, 0, 0);
		if (s != B_OK) { errors++; continue; }

		size_t sz = sizeof(recvBuf);
		size_t hc = 0;
		s = kosm_ray_read(ep1, recvBuf, &sz, NULL, &hc, 0);
		if (s != B_OK) errors++;
	}
	bigtime_t elapsed = system_time() - start;
	double opsPerSec = (double)kCycles * 1000000.0 / (double)elapsed;

	trace("    %d write/read cycles in %lld us\n", kCycles,
		(long long)elapsed);
	trace("    throughput: %.0f round-trips/sec\n", opsPerSec);
	trace("    errors: %d\n", errors);

	TEST_ASSERT("no errors", errors == 0);
	TEST_ASSERT("throughput > 10000 rt/s", opsPerSec > 10000);

	kosm_close_ray(ep0);
	kosm_close_ray(ep1);
	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


static void
test_stress_concurrent_writers()
{
	trace("\n--- test_stress_concurrent_writers ---\n");
	bigtime_t t0 = system_time();

	kosm_ray_id ep0, ep1;
	status_t s = kosm_create_ray(&ep0, &ep1, 0);
	TEST_ASSERT("create", s == B_OK);

	const int kWriterCount = 4;
	const int kMsgsPerWriter = 100;
	concurrent_writer_args wargs[kWriterCount];
	thread_id tids[kWriterCount];

	for (int i = 0; i < kWriterCount; i++) {
		wargs[i].ray = ep0;
		wargs[i].count = kMsgsPerWriter;
		wargs[i].threadIndex = i;
		wargs[i].errors = 0;
		char name[32];
		snprintf(name, sizeof(name), "cwriter_%d", i);
		tids[i] = spawn_thread(concurrent_writer_func, name,
			B_NORMAL_PRIORITY, &wargs[i]);
		resume_thread(tids[i]);
	}

	// Read all messages
	int totalRead = 0;
	int readErrors = 0;
	for (int i = 0; i < kWriterCount * kMsgsPerWriter; i++) {
		uint8 buf[8];
		size_t sz = sizeof(buf);
		size_t hc = 0;
		s = kosm_ray_read_etc(ep1, buf, &sz, NULL, &hc,
			B_RELATIVE_TIMEOUT, 2000000);
		if (s == B_OK)
			totalRead++;
		else
			readErrors++;
	}

	int totalWriteErrors = 0;
	for (int i = 0; i < kWriterCount; i++) {
		status_t exitVal;
		wait_for_thread(tids[i], &exitVal);
		totalWriteErrors += wargs[i].errors;
		trace("    writer %d: errors=%d\n", i, wargs[i].errors);
	}

	trace("    totalRead=%d, readErrors=%d, writeErrors=%d\n",
		totalRead, readErrors, totalWriteErrors);
	TEST_ASSERT("all messages read",
		totalRead == kWriterCount * kMsgsPerWriter);
	TEST_ASSERT("no write errors", totalWriteErrors == 0);

	kosm_close_ray(ep0);
	kosm_close_ray(ep1);
	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


static void
test_stress_create_close_churn()
{
	trace("\n--- test_stress_create_close_churn ---\n");
	bigtime_t t0 = system_time();

	const int kThreads = 8;
	const int kCyclesPerThread = 100;
	churn_args cargs[kThreads];
	thread_id tids[kThreads];

	for (int i = 0; i < kThreads; i++) {
		cargs[i].count = kCyclesPerThread;
		cargs[i].errors = 0;
		char name[32];
		snprintf(name, sizeof(name), "churn_%d", i);
		tids[i] = spawn_thread(churn_thread_func, name,
			B_NORMAL_PRIORITY, &cargs[i]);
		resume_thread(tids[i]);
	}

	int totalErrors = 0;
	for (int i = 0; i < kThreads; i++) {
		status_t exitVal;
		wait_for_thread(tids[i], &exitVal);
		totalErrors += cargs[i].errors;
		trace("    thread %d: errors=%d\n", i, cargs[i].errors);
	}

	trace("    total churn cycles=%d, errors=%d\n",
		kThreads * kCyclesPerThread, totalErrors);
	TEST_ASSERT("create/close churn no errors", totalErrors == 0);

	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


static void
test_stress_large_payload()
{
	trace("\n--- test_stress_large_payload ---\n");
	bigtime_t t0 = system_time();

	kosm_ray_id ep0, ep1;
	status_t s = kosm_create_ray(&ep0, &ep1, 0);
	TEST_ASSERT("create", s == B_OK);

	const int kCycles = 100;
	const size_t kPayloadSize = 64 * 1024;	// 64KB
	uint8* sendBuf = (uint8*)malloc(kPayloadSize);
	uint8* recvBuf = (uint8*)malloc(kPayloadSize);
	TEST_ASSERT("alloc buffers", sendBuf != NULL && recvBuf != NULL);

	int errors = 0;
	bigtime_t start = system_time();
	for (int i = 0; i < kCycles; i++) {
		// Fill with pattern
		memset(sendBuf, (uint8)(i & 0xFF), kPayloadSize);
		sendBuf[0] = (uint8)i;

		s = kosm_ray_write(ep0, sendBuf, kPayloadSize, NULL, 0, 0);
		if (s != B_OK) { errors++; continue; }

		memset(recvBuf, 0, kPayloadSize);
		size_t sz = kPayloadSize;
		size_t hc = 0;
		s = kosm_ray_read(ep1, recvBuf, &sz, NULL, &hc, 0);
		if (s != B_OK || sz != kPayloadSize
			|| memcmp(sendBuf, recvBuf, kPayloadSize) != 0)
			errors++;
	}
	bigtime_t elapsed = system_time() - start;

	trace("    %d x 64KB cycles in %lld us, errors=%d\n",
		kCycles, (long long)elapsed, errors);
	double mbps = (double)kCycles * kPayloadSize / (double)elapsed;
	trace("    throughput: %.1f MB/s\n", mbps);

	TEST_ASSERT("no errors", errors == 0);

	free(sendBuf);
	free(recvBuf);
	kosm_close_ray(ep0);
	kosm_close_ray(ep1);
	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


// ---------------------------------------------------------------------------
// ========== MOBILE SIM ==========
// ---------------------------------------------------------------------------

static void
test_mobile_ipc_simulation()
{
	trace("\n--- test_mobile_ipc_simulation ---\n");
	bigtime_t t0 = system_time();

	const int kWorkerCount = 3;
	const int kRequestsPerWorker = 200;
	const int kTotalRequests = kWorkerCount * kRequestsPerWorker;

	// Create ray pairs for each worker: request + response
	kosm_ray_id reqSend[kWorkerCount], reqRecv[kWorkerCount];
	kosm_ray_id respSend[kWorkerCount], respRecv[kWorkerCount];

	bool createOK = true;
	for (int i = 0; i < kWorkerCount; i++) {
		if (kosm_create_ray(&reqSend[i], &reqRecv[i], 0) != B_OK)
			createOK = false;
		if (kosm_create_ray(&respSend[i], &respRecv[i], 0) != B_OK)
			createOK = false;
	}
	TEST_ASSERT("create all ray pairs", createOK);

	// Start workers
	mobile_worker_args wargs[kWorkerCount];
	thread_id wTids[kWorkerCount];
	for (int i = 0; i < kWorkerCount; i++) {
		wargs[i].requestRay = reqRecv[i];
		wargs[i].responseRay = respSend[i];
		wargs[i].count = kRequestsPerWorker;
		char name[32];
		snprintf(name, sizeof(name), "worker_%d", i);
		wTids[i] = spawn_thread(mobile_worker_func, name,
			B_NORMAL_PRIORITY, &wargs[i]);
		resume_thread(wTids[i]);
	}

	// Start dispatcher
	mobile_dispatcher_args dargs;
	dargs.workerRequestRays = reqSend;
	dargs.workerResponseRays = respRecv;
	dargs.workerCount = kWorkerCount;
	dargs.totalRequests = kTotalRequests;

	thread_id dTid = spawn_thread(mobile_dispatcher_func, "dispatcher",
		B_URGENT_DISPLAY_PRIORITY, &dargs);
	resume_thread(dTid);

	// Wait for all
	status_t exitVal;
	wait_for_thread(dTid, &exitVal);
	for (int i = 0; i < kWorkerCount; i++)
		wait_for_thread(wTids[i], &exitVal);

	trace("    dispatcher: dispatched=%d, received=%d, maxLatency=%lld us\n",
		dargs.dispatched, dargs.received, (long long)dargs.maxLatency);
	for (int i = 0; i < kWorkerCount; i++) {
		trace("    worker %d: processed=%d, maxLatency=%lld us\n",
			i, wargs[i].processed, (long long)wargs[i].maxLatency);
	}

	int totalProcessed = 0;
	for (int i = 0; i < kWorkerCount; i++)
		totalProcessed += wargs[i].processed;

	TEST_ASSERT("all requests dispatched", dargs.dispatched == kTotalRequests);
	TEST_ASSERT("all responses received", dargs.received == kTotalRequests);
	TEST_ASSERT("dispatcher max latency < 100ms", dargs.maxLatency < 100000);

	// Cleanup
	for (int i = 0; i < kWorkerCount; i++) {
		kosm_close_ray(reqSend[i]);
		kosm_close_ray(reqRecv[i]);
		kosm_close_ray(respSend[i]);
		kosm_close_ray(respRecv[i]);
	}
	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


// ---------------------------------------------------------------------------
// SELECT tests
// ---------------------------------------------------------------------------


static void
test_select_readable()
{
	trace("\n--- test_select_readable ---\n");
	bigtime_t t0 = system_time();

	kosm_ray_id ep0, ep1;
	status_t s = kosm_create_ray(&ep0, &ep1, 0);
	TEST_ASSERT("create", s == B_OK);

	// Write a message on ep0 so ep1 becomes readable
	uint8 msg = 0xAA;
	s = kosm_ray_write(ep0, &msg, 1, NULL, 0, 0);
	TEST_ASSERT("write", s == B_OK);

	// wait_for_objects on ep1 with B_EVENT_READ
	object_wait_info info;
	info.object = ep1;
	info.type = B_OBJECT_TYPE_KOSM_RAY;
	info.events = B_EVENT_READ;

	ssize_t result = wait_for_objects(&info, 1);
	trace("    wait_for_objects: result=%zd, events=0x%04x\n",
		result, info.events);
	TEST_ASSERT("wait returns >= 0", result >= 0);
	TEST_ASSERT("B_EVENT_READ set", (info.events & B_EVENT_READ) != 0);

	kosm_close_ray(ep0);
	kosm_close_ray(ep1);
	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


static void
test_select_writable()
{
	trace("\n--- test_select_writable ---\n");
	bigtime_t t0 = system_time();

	kosm_ray_id ep0, ep1;
	status_t s = kosm_create_ray(&ep0, &ep1, 0);
	TEST_ASSERT("create", s == B_OK);

	// Empty queue — ep0 should be writable (peer has space)
	object_wait_info info;
	info.object = ep0;
	info.type = B_OBJECT_TYPE_KOSM_RAY;
	info.events = B_EVENT_WRITE;

	ssize_t result = wait_for_objects(&info, 1);
	trace("    wait_for_objects (writable): result=%zd, events=0x%04x\n",
		result, info.events);
	TEST_ASSERT("wait returns >= 0", result >= 0);
	TEST_ASSERT("B_EVENT_WRITE set", (info.events & B_EVENT_WRITE) != 0);

	kosm_close_ray(ep0);
	kosm_close_ray(ep1);
	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


static void
test_select_peer_closed()
{
	trace("\n--- test_select_peer_closed ---\n");
	bigtime_t t0 = system_time();

	kosm_ray_id ep0, ep1;
	status_t s = kosm_create_ray(&ep0, &ep1, 0);
	TEST_ASSERT("create", s == B_OK);

	// Close ep0, then select on ep1
	kosm_close_ray(ep0);

	object_wait_info info;
	info.object = ep1;
	info.type = B_OBJECT_TYPE_KOSM_RAY;
	info.events = B_EVENT_READ;

	ssize_t result = wait_for_objects(&info, 1);
	trace("    wait_for_objects (peer closed): result=%zd, events=0x%04x\n",
		result, info.events);
	TEST_ASSERT("wait returns >= 0", result >= 0);
	TEST_ASSERT("B_EVENT_DISCONNECTED set",
		(info.events & B_EVENT_DISCONNECTED) != 0);

	kosm_close_ray(ep1);
	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


static void
test_select_multiple_rays()
{
	trace("\n--- test_select_multiple_rays ---\n");
	bigtime_t t0 = system_time();

	// Create 3 ray pairs
	kosm_ray_id ep0[3], ep1[3];
	for (int i = 0; i < 3; i++) {
		status_t s = kosm_create_ray(&ep0[i], &ep1[i], 0);
		TEST_ASSERT("create pair", s == B_OK);
	}

	// Write only on pair 1's ep0 (making pair 1's ep1 readable)
	uint8 msg = 0xBB;
	status_t s = kosm_ray_write(ep0[1], &msg, 1, NULL, 0, 0);
	TEST_ASSERT("write to pair 1", s == B_OK);

	// Wait on all 3 ep1s for B_EVENT_READ
	object_wait_info infos[3];
	for (int i = 0; i < 3; i++) {
		infos[i].object = ep1[i];
		infos[i].type = B_OBJECT_TYPE_KOSM_RAY;
		infos[i].events = B_EVENT_READ;
	}

	ssize_t result = wait_for_objects(infos, 3);
	trace("    wait_for_objects (3 rays): result=%zd\n", result);
	for (int i = 0; i < 3; i++)
		trace("      ray[%d] events=0x%04x\n", i, infos[i].events);

	TEST_ASSERT("wait returns >= 0", result >= 0);
	TEST_ASSERT("pair 0 not readable",
		(infos[0].events & B_EVENT_READ) == 0);
	TEST_ASSERT("pair 1 readable",
		(infos[1].events & B_EVENT_READ) != 0);
	TEST_ASSERT("pair 2 not readable",
		(infos[2].events & B_EVENT_READ) == 0);

	for (int i = 0; i < 3; i++) {
		kosm_close_ray(ep0[i]);
		kosm_close_ray(ep1[i]);
	}
	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


static void
test_select_timeout()
{
	trace("\n--- test_select_timeout ---\n");
	bigtime_t t0 = system_time();

	kosm_ray_id ep0, ep1;
	status_t s = kosm_create_ray(&ep0, &ep1, 0);
	TEST_ASSERT("create", s == B_OK);

	// Empty queue — B_EVENT_READ should not fire within 100ms
	object_wait_info info;
	info.object = ep1;
	info.type = B_OBJECT_TYPE_KOSM_RAY;
	info.events = B_EVENT_READ;

	bigtime_t before = system_time();
	ssize_t result = wait_for_objects_etc(&info, 1,
		B_RELATIVE_TIMEOUT, 100000);
	bigtime_t elapsed = system_time() - before;

	trace("    wait_for_objects_etc (100ms timeout): result=%zd, "
		"elapsed=%lld us, events=0x%04x\n",
		result, (long long)elapsed, info.events);
	TEST_ASSERT("returns B_TIMED_OUT", result == B_TIMED_OUT);
	TEST_ASSERT("elapsed >= 80ms", elapsed >= 80000);
	TEST_ASSERT("elapsed < 500ms", elapsed < 500000);

	kosm_close_ray(ep0);
	kosm_close_ray(ep1);
	trace("  total: %lld us\n", (long long)(system_time() - t0));
}


// ---------------------------------------------------------------------------
// GUI
// ---------------------------------------------------------------------------

class ResultView : public BView {
public:
	ResultView()
		: BView(BRect(0, 0, 699, 999), "ResultView",
			B_FOLLOW_ALL, B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE)
	{
		SetViewColor(30, 30, 40);
	}

	virtual void Draw(BRect updateRect)
	{
		SetFont(be_fixed_font);
		SetFontSize(11.0f);

		float lineHeight = 14.0f;
		float y = 16.0f;

		for (int i = 0; i < sLineCount && i < kMaxLines; i++) {
			const char* line = sLines[i];

			if (strstr(line, "FAIL"))
				SetHighColor(255, 80, 80);
			else if (strstr(line, "PASS"))
				SetHighColor(80, 220, 80);
			else if (strstr(line, "==="))
				SetHighColor(255, 220, 100);
			else if (strstr(line, "---"))
				SetHighColor(130, 170, 255);
			else if (strstr(line, "STRESS") || strstr(line, "MOBILE")
				|| strstr(line, "Select"))
				SetHighColor(255, 180, 60);
			else
				SetHighColor(200, 200, 200);

			DrawString(line, BPoint(10, y));
			y += lineHeight;
		}
	}
};


class ResultWindow : public BWindow {
public:
	ResultWindow()
		: BWindow(BRect(80, 50, 780, 1050), "KosmRay Test Suite",
			B_TITLED_WINDOW, B_QUIT_ON_WINDOW_CLOSE)
	{
		AddChild(new ResultView());
	}
};


class KosmRayTestApp : public BApplication {
public:
	KosmRayTestApp()
		: BApplication("application/x-vnd.KosmOS-RayTest")
	{
	}

	virtual void ReadyToRun()
	{
		_OpenTrace();
		_RunTests();
		(new ResultWindow())->Show();
	}

private:
	void _OpenTrace()
	{
		BPath path;
		if (find_directory(B_DESKTOP_DIRECTORY, &path) != B_OK)
			return;
		path.Append("kosm_ray_trace.log");
		sTraceFile = fopen(path.Path(), "w");
		if (sTraceFile == NULL)
			return;

		time_t now = time(NULL);
		struct tm* t = localtime(&now);
		char dateBuf[64];
		strftime(dateBuf, sizeof(dateBuf), "%b %e %Y %H:%M:%S", t);

		trace("# KosmRay test trace\n");
		trace("# date: %s\n", dateBuf);
		trace("# system_time at start: %lld us\n",
			(long long)system_time());
		trace("# main thread: %ld, team: %ld\n",
			(long)find_thread(NULL), (long)find_thread(NULL));
		trace("#\n");
	}

	void _RunTest(const char* name, void (*func)(), const char* category)
	{
		int p0 = sPassCount, f0 = sFailCount;
		bigtime_t t0 = system_time();
		func();
		bigtime_t dt = system_time() - t0;
		int dp = sPassCount - p0;
		int df = sFailCount - f0;
		int total = dp + df;

		if (df == 0) {
			log_line("  PASS  %s  (%d checks, %lld us)",
				name, total, (long long)dt);
			trace("  [%s] PASS: %s  (%d checks, %lld us)\n",
				category, name, total, (long long)dt);
		} else {
			log_line("  FAIL  %s  (%d/%d failed, %lld us)",
				name, df, total, (long long)dt);
			trace("  [%s] FAIL: %s  (%d/%d failed, %lld us)\n",
				category, name, df, total, (long long)dt);
		}
	}

	void _RunTests()
	{
		bigtime_t totalStart = system_time();

		// --- BASIC ---
		log_line("--- Basic ---");
		trace("\n# ========== BASIC ==========\n");
		_RunTest("create/close", test_create_close, "basic");
		_RunTest("write/read", test_write_read, "basic");
		_RunTest("bidirectional", test_bidirectional, "basic");
		_RunTest("zero-length", test_zero_length, "basic");
		_RunTest("multiple messages", test_multiple_messages, "basic");
		_RunTest("get_ray_info", test_get_ray_info, "basic");
		_RunTest("read truncation", test_read_truncation, "basic");

		// --- LIMITS ---
		log_line("");
		log_line("--- Limits ---");
		trace("\n# ========== LIMITS ==========\n");
		_RunTest("max data size", test_max_data_size, "limits");
		_RunTest("max handles", test_max_handles, "limits");
		_RunTest("queue full", test_queue_full, "limits");
		_RunTest("invalid id", test_invalid_id, "limits");
		_RunTest("double close", test_double_close, "limits");

		// --- PEER CLOSE ---
		log_line("");
		log_line("--- Peer Close ---");
		trace("\n# ========== PEER CLOSE ==========\n");
		_RunTest("write after peer close", test_write_after_peer_close,
			"peer");
		_RunTest("read empty after peer close",
			test_read_empty_after_peer_close, "peer");
		_RunTest("drain after peer close", test_drain_after_peer_close,
			"peer");
		_RunTest("close wakes blocked reader",
			test_close_wakes_blocked_reader, "peer");
		_RunTest("close wakes blocked writer",
			test_close_wakes_blocked_writer, "peer");

		// --- HANDLES ---
		log_line("");
		log_line("--- Handles ---");
		trace("\n# ========== HANDLES ==========\n");
		_RunTest("ray handle move", test_handle_ray_move, "handles");
		_RunTest("mutex handle", test_handle_mutex, "handles");
		_RunTest("area handle", test_handle_area_move, "handles");
		_RunTest("sem handle", test_handle_sem_move, "handles");
		_RunTest("FD not supported", test_handle_fd_not_supported,
			"handles");
		_RunTest("invalid handle", test_handle_invalid, "handles");

		// --- PEEK & FLAGS ---
		log_line("");
		log_line("--- Peek & Flags ---");
		trace("\n# ========== PEEK & FLAGS ==========\n");
		_RunTest("peek", test_peek, "flags");
		_RunTest("copy handles", test_copy_handles, "flags");
		_RunTest("timed read/write", test_timed_read_write, "flags");

		// --- QOS ---
		log_line("");
		log_line("--- QoS ---");
		trace("\n# ========== QOS ==========\n");
		_RunTest("set/get QoS", test_qos_set_get, "qos");
		_RunTest("invalid QoS", test_qos_invalid, "qos");
		_RunTest("QoS in message", test_qos_in_message, "qos");

		// --- WAIT ---
		log_line("");
		log_line("--- Wait ---");
		trace("\n# ========== WAIT ==========\n");
		_RunTest("wait readable", test_wait_readable, "wait");
		_RunTest("wait peer closed", test_wait_peer_closed, "wait");
		_RunTest("wait already satisfied", test_wait_already_satisfied,
			"wait");
		_RunTest("wait timeout", test_wait_timeout, "wait");

		// --- STRESS ---
		log_line("");
		log_line("--- STRESS ---");
		trace("\n# ========== STRESS ==========\n");
		_RunTest("throughput", test_stress_throughput, "stress");
		_RunTest("concurrent writers", test_stress_concurrent_writers,
			"stress");
		_RunTest("create/close churn", test_stress_create_close_churn,
			"stress");
		_RunTest("large payload", test_stress_large_payload, "stress");

		// --- MOBILE SIM ---
		log_line("");
		log_line("--- MOBILE SIM ---");
		trace("\n# ========== MOBILE SIM ==========\n");
		_RunTest("IPC pipeline 3 workers",
			test_mobile_ipc_simulation, "mobile");

		// --- SELECT ---
		log_line("");
		log_line("--- Select ---");
		trace("\n# ========== SELECT ==========\n");
		_RunTest("select readable", test_select_readable, "select");
		_RunTest("select writable", test_select_writable, "select");
		_RunTest("select peer closed", test_select_peer_closed, "select");
		_RunTest("select multiple rays", test_select_multiple_rays,
			"select");
		_RunTest("select timeout", test_select_timeout, "select");

		// --- RESULTS ---
		bigtime_t totalTime = system_time() - totalStart;
		log_line("");
		log_line("=== Results: %d passed, %d failed (%lld us total) ===",
			sPassCount, sFailCount, (long long)totalTime);

		trace("\n# ================================\n");
		trace("# FINAL: %d passed, %d failed\n", sPassCount, sFailCount);
		trace("# total time: %lld us\n", (long long)totalTime);
		trace("# ================================\n");

		if (sTraceFile != NULL) {
			fclose(sTraceFile);
			sTraceFile = NULL;
		}
		log_line("Trace: ~/Desktop/kosm_ray_trace.log");
	}
};


// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int
main()
{
	KosmRayTestApp app;
	app.Run();
	return 0;
}
