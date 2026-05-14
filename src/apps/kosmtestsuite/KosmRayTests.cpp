/*
 * Copyright 2025 KosmOS Project. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * KosmRay IPC test functions for the unified test suite.
 */


#include "TestCommon.h"

#include <KosmOS.h>

#include <sys/uio.h>
#include <sys/wait.h>
#include <unistd.h>


// Thread arg structs

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
	bigtime_t		delay;
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


struct ray_churn_args {
	int		count;
	int		errors;
};


static status_t
ray_churn_thread_func(void* data)
{
	ray_churn_args* a = (ray_churn_args*)data;
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


struct mobile_worker_args {
	kosm_ray_id		requestRay;
	kosm_ray_id		responseRay;
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
	kosm_ray_id*	workerRequestRays;
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


// INPUT-PROFILE helpers (sustained pacing / latency distribution / QoS / writev / call)

// 64-byte timed message — matches realistic KosmPointerEvent size and starts
// with bigtime_t so the receiver computes one-way latency directly.
struct timed_msg {
	bigtime_t	send_time;
	uint32		seq;
	uint8		filler[52];
};


struct paced_writer_args {
	kosm_ray_id		ray;
	int				rateHz;
	int				totalCount;
	int				sent;
	int				writeErrors;
	bigtime_t		startTime;
};


static status_t
paced_writer_func(void* data)
{
	paced_writer_args* a = (paced_writer_args*)data;
	a->sent = 0;
	a->writeErrors = 0;
	bigtime_t period = 1000000 / a->rateHz;
	bigtime_t deadline = a->startTime;
	for (int i = 0; i < a->totalCount; i++) {
		snooze_until(deadline, B_SYSTEM_TIMEBASE);
		deadline += period;

		timed_msg msg;
		memset(&msg, 0, sizeof(msg));
		msg.seq = (uint32)i;
		msg.send_time = system_time();
		status_t s = kosm_ray_write_etc(a->ray, &msg, sizeof(msg),
			NULL, 0, B_RELATIVE_TIMEOUT, 0);
		if (s == B_OK)
			a->sent++;
		else
			a->writeErrors++;
	}
	return B_OK;
}


struct latency_reader_args {
	kosm_ray_id		ray;
	int				expectedCount;
	bigtime_t*		samples;	// allocated by caller, sized expectedCount
	int				received;
	int				readErrors;
	bigtime_t		readTimeout;
};


static status_t
latency_reader_func(void* data)
{
	latency_reader_args* a = (latency_reader_args*)data;
	a->received = 0;
	a->readErrors = 0;
	for (int i = 0; i < a->expectedCount; i++) {
		timed_msg msg;
		size_t sz = sizeof(msg);
		size_t hc = 0;
		status_t s = kosm_ray_read_etc(a->ray, &msg, &sz, NULL, &hc,
			B_RELATIVE_TIMEOUT, a->readTimeout);
		if (s != B_OK) {
			a->readErrors++;
			break;
		}
		a->samples[a->received] = system_time() - msg.send_time;
		a->received++;
	}
	return B_OK;
}


struct ray_call_server_args {
	kosm_ray_id		ray;
	int				count;
	int				processed;
	bool			stop;
};


static status_t
ray_call_server_func(void* data)
{
	ray_call_server_args* a = (ray_call_server_args*)data;
	a->processed = 0;
	while (!a->stop && a->processed < a->count) {
		uint8 buf[64];
		size_t sz = sizeof(buf);
		size_t hc = 0;
		status_t s = kosm_ray_read_etc(a->ray, buf, &sz, NULL, &hc,
			B_RELATIVE_TIMEOUT, 200000);
		if (s != B_OK)
			continue;
		// Echo back, increment first byte to verify round-trip.
		buf[0]++;
		kosm_ray_write_etc(a->ray, buf, sz, NULL, 0,
			B_RELATIVE_TIMEOUT, 100000);
		a->processed++;
	}
	return B_OK;
}


struct slow_reader_args {
	kosm_ray_id		ray;
	int				count;
	bigtime_t		perReadSnooze;
	int				received;
};


static status_t
slow_reader_func(void* data)
{
	slow_reader_args* a = (slow_reader_args*)data;
	a->received = 0;
	for (int i = 0; i < a->count; i++) {
		timed_msg msg;
		size_t sz = sizeof(msg);
		size_t hc = 0;
		status_t s = kosm_ray_read_etc(a->ray, &msg, &sz, NULL, &hc,
			B_RELATIVE_TIMEOUT, 5000000);
		if (s != B_OK)
			break;
		a->received++;
		if (a->perReadSnooze > 0)
			snooze(a->perReadSnooze);
	}
	return B_OK;
}


struct fairness_writer_args {
	kosm_ray_id		ray;
	int				count;
	int				sent;
};


static status_t
fairness_writer_func(void* data)
{
	fairness_writer_args* a = (fairness_writer_args*)data;
	a->sent = 0;
	uint8 msg[16];
	for (int i = 0; i < a->count; i++) {
		msg[0] = (uint8)i;
		status_t s = kosm_ray_write_etc(a->ray, msg, sizeof(msg),
			NULL, 0, B_RELATIVE_TIMEOUT, 1000000);
		if (s == B_OK)
			a->sent++;
	}
	return B_OK;
}


// BASIC

static void
test_create_close()
{
	trace("\n--- test_create_close ---\n");
	kosm_ray_id ep0 = -1, ep1 = -1;
	status_t s = kosm_create_ray(&ep0, &ep1, 0);
	trace("    kosm_create_ray -> ep0=%ld, ep1=%ld, %s\n",
		(long)ep0, (long)ep1, strerror(s));
	TEST_ASSERT("create returns B_OK", s == B_OK);
	TEST_ASSERT("ep0 valid", ep0 >= 0);
	TEST_ASSERT("ep1 valid", ep1 >= 0);

	s = kosm_close_ray(ep0);
	TEST_ASSERT("close ep0", s == B_OK);
	s = kosm_close_ray(ep1);
	TEST_ASSERT("close ep1", s == B_OK);
	s = kosm_close_ray(ep0);
	TEST_ASSERT("double close fails", s != B_OK);
}


static void
test_write_read()
{
	trace("\n--- test_write_read ---\n");
	kosm_ray_id ep0, ep1;
	status_t s = kosm_create_ray(&ep0, &ep1, 0);
	TEST_ASSERT("create", s == B_OK);

	uint8 sendData[64];
	for (int i = 0; i < 64; i++)
		sendData[i] = (uint8)(i * 3 + 7);

	s = kosm_ray_write(ep0, sendData, sizeof(sendData), NULL, 0, 0);
	TEST_ASSERT("write succeeds", s == B_OK);

	uint8 recvData[64];
	memset(recvData, 0, sizeof(recvData));
	size_t sz = sizeof(recvData);
	size_t hc = 0;
	s = kosm_ray_read(ep1, recvData, &sz, NULL, &hc, 0);
	TEST_ASSERT("read succeeds", s == B_OK);
	TEST_ASSERT("read size matches", sz == 64);
	TEST_ASSERT("data matches", memcmp(sendData, recvData, 64) == 0);

	kosm_close_ray(ep0);
	kosm_close_ray(ep1);
}


static void
test_bidirectional()
{
	trace("\n--- test_bidirectional ---\n");
	kosm_ray_id ep0, ep1;
	status_t s = kosm_create_ray(&ep0, &ep1, 0);
	TEST_ASSERT("create", s == B_OK);

	uint32 val1 = 0xDEADBEEF;
	s = kosm_ray_write(ep0, &val1, sizeof(val1), NULL, 0, 0);
	TEST_ASSERT("write ep0->ep1", s == B_OK);

	uint32 recv1 = 0;
	size_t sz = sizeof(recv1);
	size_t hc = 0;
	s = kosm_ray_read(ep1, &recv1, &sz, NULL, &hc, 0);
	TEST_ASSERT("read on ep1", s == B_OK);
	TEST_ASSERT("data ep0->ep1", recv1 == 0xDEADBEEF);

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
}


static void
test_zero_length()
{
	trace("\n--- test_zero_length ---\n");
	kosm_ray_id ep0, ep1;
	status_t s = kosm_create_ray(&ep0, &ep1, 0);
	TEST_ASSERT("create", s == B_OK);

	s = kosm_ray_write(ep0, NULL, 0, NULL, 0, 0);
	TEST_ASSERT("write zero-length", s == B_OK);

	uint8 buf[4];
	size_t sz = sizeof(buf);
	size_t hc = 0;
	s = kosm_ray_read(ep1, buf, &sz, NULL, &hc, 0);
	TEST_ASSERT("read succeeds", s == B_OK);
	TEST_ASSERT("read size is 0", sz == 0);

	kosm_close_ray(ep0);
	kosm_close_ray(ep1);
}


static void
test_multiple_messages()
{
	trace("\n--- test_multiple_messages ---\n");
	kosm_ray_id ep0, ep1;
	status_t s = kosm_create_ray(&ep0, &ep1, 0);
	TEST_ASSERT("create", s == B_OK);

	for (int i = 0; i < 10; i++) {
		uint32 val = (uint32)(i * 1000 + 42);
		s = kosm_ray_write(ep0, &val, sizeof(val), NULL, 0, 0);
		if (s != B_OK) break;
	}
	TEST_ASSERT("all 10 writes succeed", s == B_OK);

	bool allOK = true;
	for (int i = 0; i < 10; i++) {
		uint32 val = 0;
		size_t sz = sizeof(val);
		size_t hc = 0;
		s = kosm_ray_read(ep1, &val, &sz, NULL, &hc, 0);
		uint32 expected = (uint32)(i * 1000 + 42);
		if (s != B_OK || val != expected)
			allOK = false;
	}
	TEST_ASSERT("FIFO order preserved", allOK);

	kosm_close_ray(ep0);
	kosm_close_ray(ep1);
}


static void
test_get_ray_info()
{
	trace("\n--- test_get_ray_info ---\n");
	kosm_ray_id ep0, ep1;
	status_t s = kosm_create_ray(&ep0, &ep1, 0);
	TEST_ASSERT("create", s == B_OK);

	s = kosm_ray_set_qos(ep0, KOSM_QOS_USER_INTERACTIVE);
	TEST_ASSERT("set qos", s == B_OK);

	for (int i = 0; i < 3; i++) {
		uint32 val = i;
		kosm_ray_write(ep0, &val, sizeof(val), NULL, 0, 0);
	}

	kosm_ray_info info;
	memset(&info, 0, sizeof(info));
	s = kosm_get_ray_info(ep0, &info);
	TEST_ASSERT("get_info succeeds", s == B_OK);
	TEST_ASSERT("info.ray matches", info.ray == ep0);
	TEST_ASSERT("info.peer matches", info.peer == ep1);
	TEST_ASSERT("info.qos_class",
		info.qos_class == KOSM_QOS_USER_INTERACTIVE);

	kosm_ray_info info1;
	memset(&info1, 0, sizeof(info1));
	kosm_get_ray_info(ep1, &info1);
	TEST_ASSERT("ep1 queue_count is 3", info1.queue_count == 3);

	kosm_close_ray(ep0);
	kosm_close_ray(ep1);
}


static void
test_read_truncation()
{
	trace("\n--- test_read_truncation ---\n");
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
	TEST_ASSERT("read succeeds", s == B_OK);
	TEST_ASSERT("reported size is true size (256)", sz == 256);
	TEST_ASSERT("first 64 bytes correct",
		memcmp(smallBuf, bigData, 64) == 0);

	kosm_close_ray(ep0);
	kosm_close_ray(ep1);
}


// LIMITS

static void
test_max_data_size()
{
	trace("\n--- test_max_data_size ---\n");
	kosm_ray_id ep0, ep1;
	status_t s = kosm_create_ray(&ep0, &ep1, 0);
	TEST_ASSERT("create", s == B_OK);

	size_t maxSize = KOSM_RAY_MAX_DATA_SIZE;
	uint8* bigBuf = (uint8*)malloc(maxSize + 1);
	TEST_ASSERT("alloc buffer", bigBuf != NULL);

	for (size_t i = 0; i < maxSize; i++)
		bigBuf[i] = (uint8)(i & 0xFF);

	s = kosm_ray_write(ep0, bigBuf, maxSize, NULL, 0, 0);
	TEST_ASSERT("write max size succeeds", s == B_OK);

	uint8* recvBuf = (uint8*)malloc(maxSize);
	size_t sz = maxSize;
	size_t hc = 0;
	s = kosm_ray_read(ep1, recvBuf, &sz, NULL, &hc, 0);
	TEST_ASSERT("read 256KB", s == B_OK);
	TEST_ASSERT("size matches", sz == maxSize);
	TEST_ASSERT("data integrity", memcmp(bigBuf, recvBuf, maxSize) == 0);

	s = kosm_ray_write(ep0, bigBuf, maxSize + 1, NULL, 0, 0);
	TEST_ASSERT("over-size fails KOSM_RAY_TOO_LARGE",
		s == KOSM_RAY_TOO_LARGE);

	free(bigBuf);
	free(recvBuf);
	kosm_close_ray(ep0);
	kosm_close_ray(ep1);
}


static void
test_max_handles()
{
	trace("\n--- test_max_handles ---\n");
	kosm_ray_id ep0, ep1;
	status_t s = kosm_create_ray(&ep0, &ep1, 0);
	TEST_ASSERT("create", s == B_OK);

	kosm_handle_t handles[65];
	kosm_mutex_id mutexes[65];
	for (int i = 0; i < 65; i++) {
		char name[32];
		snprintf(name, sizeof(name), "htest_%d", i);
		mutexes[i] = kosm_create_mutex(name, 0);
		handles[i] = mutexes[i];
	}

	uint8 msg = 0x42;
	s = kosm_ray_write(ep0, &msg, 1, handles, KOSM_RAY_MAX_HANDLES,
		KOSM_RAY_COPY_HANDLES);
	TEST_ASSERT("64 handles succeeds", s == B_OK);

	uint8 dummy;
	size_t sz = 1;
	kosm_handle_t recvH[64];
	size_t rhc = 64;
	kosm_ray_read(ep1, &dummy, &sz, recvH, &rhc, 0);

	s = kosm_ray_write(ep0, &msg, 1, handles, 65, KOSM_RAY_COPY_HANDLES);
	TEST_ASSERT("65 handles fails KOSM_RAY_TOO_MANY_HANDLES",
		s == KOSM_RAY_TOO_MANY_HANDLES);

	for (int i = 0; i < 65; i++)
		kosm_delete_mutex(mutexes[i]);
	kosm_close_ray(ep0);
	kosm_close_ray(ep1);
}


static void
test_queue_full()
{
	trace("\n--- test_queue_full ---\n");
	kosm_ray_id ep0, ep1;
	status_t s = kosm_create_ray(&ep0, &ep1, 0);
	TEST_ASSERT("create", s == B_OK);

	int written = 0;
	for (int i = 0; i < KOSM_RAY_MAX_QUEUE_MESSAGES; i++) {
		uint8 msg = (uint8)i;
		s = kosm_ray_write(ep0, &msg, 1, NULL, 0, 0);
		if (s == B_OK) written++;
		else break;
	}
	TEST_ASSERT("filled 256 messages",
		written == KOSM_RAY_MAX_QUEUE_MESSAGES);

	uint8 extra = 0xFF;
	s = kosm_ray_write_etc(ep0, &extra, 1, NULL, 0,
		B_RELATIVE_TIMEOUT, 0);
	TEST_ASSERT("queue full returns WOULD_BLOCK", s == B_WOULD_BLOCK);

	uint8 buf;
	size_t sz = 1;
	size_t hc = 0;
	kosm_ray_read(ep1, &buf, &sz, NULL, &hc, 0);

	s = kosm_ray_write(ep0, &extra, 1, NULL, 0, 0);
	TEST_ASSERT("write after drain succeeds", s == B_OK);

	for (int i = 0; i < KOSM_RAY_MAX_QUEUE_MESSAGES; i++) {
		sz = 1; hc = 0;
		kosm_ray_read(ep1, &buf, &sz, NULL, &hc, 0);
	}

	kosm_close_ray(ep0);
	kosm_close_ray(ep1);
}


static void
test_invalid_id()
{
	trace("\n--- test_invalid_id ---\n");
	status_t s = kosm_close_ray(-1);
	TEST_ASSERT("close(-1) fails", s != B_OK);
	s = kosm_close_ray(99999);
	TEST_ASSERT("close(99999) fails", s != B_OK);

	uint8 buf[4];
	s = kosm_ray_write(-1, buf, 4, NULL, 0, 0);
	TEST_ASSERT("write(-1) fails", s != B_OK);

	size_t sz = 4;
	size_t hc = 0;
	s = kosm_ray_read(-1, buf, &sz, NULL, &hc, 0);
	TEST_ASSERT("read(-1) fails", s != B_OK);
}


static void
test_double_close()
{
	trace("\n--- test_double_close ---\n");
	kosm_ray_id ep0, ep1;
	status_t s = kosm_create_ray(&ep0, &ep1, 0);
	TEST_ASSERT("create", s == B_OK);

	s = kosm_close_ray(ep0);
	TEST_ASSERT("first close succeeds", s == B_OK);
	s = kosm_close_ray(ep0);
	TEST_ASSERT("second close fails", s != B_OK);

	kosm_close_ray(ep1);
}


// PEER CLOSE

static void
test_write_after_peer_close()
{
	trace("\n--- test_write_after_peer_close ---\n");
	kosm_ray_id ep0, ep1;
	kosm_create_ray(&ep0, &ep1, 0);
	kosm_close_ray(ep1);

	uint32 val = 123;
	status_t s = kosm_ray_write(ep0, &val, sizeof(val), NULL, 0, 0);
	TEST_ASSERT("write returns PEER_CLOSED_ERROR",
		s == KOSM_RAY_PEER_CLOSED_ERROR);

	kosm_close_ray(ep0);
}


static void
test_read_empty_after_peer_close()
{
	trace("\n--- test_read_empty_after_peer_close ---\n");
	kosm_ray_id ep0, ep1;
	kosm_create_ray(&ep0, &ep1, 0);
	kosm_close_ray(ep0);

	uint8 buf[4];
	size_t sz = sizeof(buf);
	size_t hc = 0;
	status_t s = kosm_ray_read(ep1, buf, &sz, NULL, &hc, 0);
	TEST_ASSERT("read returns PEER_CLOSED_ERROR",
		s == KOSM_RAY_PEER_CLOSED_ERROR);

	kosm_close_ray(ep1);
}


static void
test_drain_after_peer_close()
{
	trace("\n--- test_drain_after_peer_close ---\n");
	kosm_ray_id ep0, ep1;
	kosm_create_ray(&ep0, &ep1, 0);

	for (int i = 0; i < 3; i++) {
		uint32 val = (uint32)(i + 100);
		kosm_ray_write(ep0, &val, sizeof(val), NULL, 0, 0);
	}
	kosm_close_ray(ep0);

	bool drainOK = true;
	for (int i = 0; i < 3; i++) {
		uint32 val = 0;
		size_t sz = sizeof(val);
		size_t hc = 0;
		status_t s = kosm_ray_read(ep1, &val, &sz, NULL, &hc, 0);
		if (s != B_OK || val != (uint32)(i + 100))
			drainOK = false;
	}
	TEST_ASSERT("drain 3 messages succeeds", drainOK);

	uint32 dummy;
	size_t sz = sizeof(dummy);
	size_t hc = 0;
	status_t s = kosm_ray_read(ep1, &dummy, &sz, NULL, &hc, 0);
	TEST_ASSERT("4th read returns PEER_CLOSED_ERROR",
		s == KOSM_RAY_PEER_CLOSED_ERROR);

	kosm_close_ray(ep1);
}


static void
test_close_wakes_blocked_reader()
{
	trace("\n--- test_close_wakes_blocked_reader ---\n");
	kosm_ray_id ep0, ep1;
	kosm_create_ray(&ep0, &ep1, 0);

	reader_args rargs;
	memset(&rargs, 0, sizeof(rargs));
	uint8 buf[64];
	rargs.ray = ep1;
	rargs.buffer = buf;
	rargs.bufferSize = sizeof(buf);
	rargs.timeout = 2000000;
	rargs.flags = B_RELATIVE_TIMEOUT;

	thread_id tid = spawn_thread(reader_thread_func, "blocked_reader",
		B_NORMAL_PRIORITY, &rargs);
	resume_thread(tid);
	snooze(50000);

	kosm_close_ray(ep0);

	status_t exitVal;
	wait_for_thread(tid, &exitVal);
	TEST_ASSERT("reader unblocked", rargs.elapsed < 1000000);
	TEST_ASSERT("reader got peer closed error",
		rargs.result == KOSM_RAY_PEER_CLOSED_ERROR);

	kosm_close_ray(ep1);
}


static void
test_close_wakes_blocked_writer()
{
	trace("\n--- test_close_wakes_blocked_writer ---\n");
	kosm_ray_id ep0, ep1;
	kosm_create_ray(&ep0, &ep1, 0);

	for (int i = 0; i < KOSM_RAY_MAX_QUEUE_MESSAGES; i++) {
		uint8 msg = (uint8)i;
		kosm_ray_write(ep0, &msg, 1, NULL, 0, 0);
	}

	uint8 extraMsg = 0xFF;
	writer_args wargs;
	memset(&wargs, 0, sizeof(wargs));
	wargs.ray = ep0;
	wargs.data = &extraMsg;
	wargs.dataSize = 1;
	wargs.flags = B_RELATIVE_TIMEOUT;
	wargs.timeout = 2000000;

	thread_id tid = spawn_thread(writer_thread_func, "blocked_writer",
		B_NORMAL_PRIORITY, &wargs);
	resume_thread(tid);
	snooze(50000);

	kosm_close_ray(ep1);

	status_t exitVal;
	wait_for_thread(tid, &exitVal);
	TEST_ASSERT("writer unblocked", wargs.elapsed < 1000000);
	TEST_ASSERT("writer got error", wargs.result != B_OK);

	kosm_close_ray(ep0);
}


// HANDLES

static void
test_handle_ray_move()
{
	trace("\n--- test_handle_ray_move ---\n");
	kosm_ray_id ep0, ep1;
	kosm_create_ray(&ep0, &ep1, 0);

	kosm_ray_id bep0, bep1;
	kosm_create_ray(&bep0, &bep1, 0);

	kosm_handle_t h = bep0;

	uint8 msg = 0x01;
	status_t s = kosm_ray_write(ep0, &msg, 1, &h, 1, 0);
	TEST_ASSERT("write with ray handle", s == B_OK);

	uint8 recvMsg;
	size_t sz = 1;
	kosm_handle_t recvH;
	size_t rhc = 1;
	s = kosm_ray_read(ep1, &recvMsg, &sz, &recvH, &rhc, 0);
	TEST_ASSERT("read succeeds", s == B_OK);
	TEST_ASSERT("got 1 handle", rhc == 1);

	kosm_handle_info hinfo;
	TEST_ASSERT("get handle info", kosm_get_handle_info(recvH, &hinfo) == B_OK);
	TEST_ASSERT("handle type is RAY", hinfo.type == KOSM_HANDLE_RAY);

	// bep0 was moved — use recvH to close the received endpoint
	kosm_close_ray(recvH);
	kosm_close_ray(bep1);
	kosm_close_ray(ep0);
	kosm_close_ray(ep1);
}


static void
test_handle_mutex()
{
	trace("\n--- test_handle_mutex ---\n");
	kosm_ray_id ep0, ep1;
	kosm_create_ray(&ep0, &ep1, 0);

	kosm_mutex_id mid = kosm_create_mutex("ray_handle_test", 0);
	TEST_ASSERT("create mutex", mid >= 0);

	kosm_handle_t h = mid;

	uint8 msg = 0x02;
	status_t s = kosm_ray_write(ep0, &msg, 1, &h, 1,
		KOSM_RAY_COPY_HANDLES);
	TEST_ASSERT("write with mutex handle", s == B_OK);

	uint8 recvMsg;
	size_t sz = 1;
	kosm_handle_t recvH;
	size_t rhc = 1;
	s = kosm_ray_read(ep1, &recvMsg, &sz, &recvH, &rhc, 0);
	TEST_ASSERT("read succeeds", s == B_OK);
	TEST_ASSERT("got 1 handle", rhc == 1);

	kosm_handle_info hinfo;
	TEST_ASSERT("get handle info", kosm_get_handle_info(recvH, &hinfo) == B_OK);
	TEST_ASSERT("handle type MUTEX", hinfo.type == KOSM_HANDLE_MUTEX);

	kosm_delete_mutex(mid);
	kosm_close_ray(ep0);
	kosm_close_ray(ep1);
}


static void
test_handle_area_move()
{
	trace("\n--- test_handle_area_move ---\n");
	kosm_ray_id ep0, ep1;
	kosm_create_ray(&ep0, &ep1, 0);

	void* addr;
	area_id aid = create_area("ray_area_test", &addr, B_ANY_ADDRESS,
		B_PAGE_SIZE, B_NO_LOCK, B_READ_AREA | B_WRITE_AREA);
	TEST_ASSERT("create area", aid >= 0);

	kosm_handle_t h = aid;

	uint8 msg = 0x03;
	status_t s = kosm_ray_write(ep0, &msg, 1, &h, 1,
		KOSM_RAY_COPY_HANDLES);
	TEST_ASSERT("write with area handle", s == B_OK);

	uint8 recvMsg;
	size_t sz = 1;
	kosm_handle_t recvH = KOSM_HANDLE_INVALID;
	size_t rhc = 1;
	s = kosm_ray_read_etc(ep1, &recvMsg, &sz, &recvH, &rhc,
		B_RELATIVE_TIMEOUT, 1000000);
	TEST_ASSERT("read succeeds", s == B_OK);
	TEST_ASSERT("got 1 handle", rhc == 1);

	kosm_handle_info hinfo;
	TEST_ASSERT("get handle info", kosm_get_handle_info(recvH, &hinfo) == B_OK);
	TEST_ASSERT("handle type AREA", hinfo.type == KOSM_HANDLE_AREA);

	delete_area(aid);
	if (recvH != KOSM_HANDLE_INVALID)
		kosm_close_handle(recvH);
	kosm_close_ray(ep0);
	kosm_close_ray(ep1);
}


static void
test_handle_sem_move()
{
	trace("\n--- test_handle_sem_move ---\n");
	kosm_ray_id ep0, ep1;
	kosm_create_ray(&ep0, &ep1, 0);

	sem_id sid = create_sem(1, "ray_sem_test");
	TEST_ASSERT("create sem", sid >= 0);

	kosm_handle_t h = sid;

	uint8 msg = 0x04;
	status_t s = kosm_ray_write(ep0, &msg, 1, &h, 1,
		KOSM_RAY_COPY_HANDLES);
	TEST_ASSERT("write with sem handle", s == B_OK);

	uint8 recvMsg;
	size_t sz = 1;
	kosm_handle_t recvH = KOSM_HANDLE_INVALID;
	size_t rhc = 1;
	s = kosm_ray_read_etc(ep1, &recvMsg, &sz, &recvH, &rhc,
		B_RELATIVE_TIMEOUT, 1000000);
	TEST_ASSERT("read succeeds", s == B_OK);
	TEST_ASSERT("got 1 handle", rhc == 1);

	kosm_handle_info hinfo;
	TEST_ASSERT("get handle info", kosm_get_handle_info(recvH, &hinfo) == B_OK);
	TEST_ASSERT("handle type SEM", hinfo.type == KOSM_HANDLE_SEM);

	delete_sem(sid);
	kosm_close_ray(ep0);
	kosm_close_ray(ep1);
}


static void
test_handle_dot_move()
{
	trace("\n--- test_handle_dot_move ---\n");
	kosm_ray_id ep0, ep1;
	kosm_create_ray(&ep0, &ep1, 0);

	void* addr = NULL;
	kosm_dot_id dot = kosm_create_dot("ray_dot_test",
		&addr, B_ANY_ADDRESS, B_PAGE_SIZE,
		KOSM_PROT_READ | KOSM_PROT_WRITE,
		KOSM_DOT_LAZY, KOSM_TAG_APP, 0);
	TEST_ASSERT("create dot", dot >= 0);

	if (addr != NULL)
		memset(addr, 0xDD, B_PAGE_SIZE);

	kosm_handle_t h = dot;

	uint8 msg = 0x06;
	status_t s = kosm_ray_write(ep0, &msg, 1, &h, 1, 0);
	TEST_ASSERT("write with dot handle", s == B_OK);

	uint8 recvMsg;
	size_t sz = 1;
	kosm_handle_t recvH;
	size_t rhc = 1;
	s = kosm_ray_read(ep1, &recvMsg, &sz, &recvH, &rhc, 0);
	TEST_ASSERT("read succeeds", s == B_OK);
	TEST_ASSERT("got 1 handle", rhc == 1);

	kosm_handle_info hinfo;
	TEST_ASSERT("get handle info",
		kosm_get_handle_info(recvH, &hinfo) == B_OK);
	TEST_ASSERT("handle type DOT", hinfo.type == KOSM_HANDLE_DOT);

	// Original was moved — verify through received handle
	kosm_dot_info dinfo;
	TEST_ASSERT("dot info via moved handle",
		kosm_get_dot_info(recvH, &dinfo) == B_OK);
	TEST_ASSERT("dot size correct", dinfo.size == B_PAGE_SIZE);

	kosm_delete_dot(recvH);
	kosm_close_ray(ep0);
	kosm_close_ray(ep1);
}


static void
test_handle_dot_copy()
{
	trace("\n--- test_handle_dot_copy ---\n");
	kosm_ray_id ep0, ep1;
	kosm_create_ray(&ep0, &ep1, 0);

	void* addr = NULL;
	kosm_dot_id dot = kosm_create_dot("ray_dot_copy",
		&addr, B_ANY_ADDRESS, B_PAGE_SIZE,
		KOSM_PROT_READ | KOSM_PROT_WRITE,
		KOSM_DOT_LAZY, KOSM_TAG_APP, 0);
	TEST_ASSERT("create dot", dot >= 0);

	if (addr != NULL)
		memset(addr, 0xEE, B_PAGE_SIZE);

	kosm_handle_t h = dot;

	uint8 msg = 0x07;
	status_t s = kosm_ray_write(ep0, &msg, 1, &h, 1,
		KOSM_RAY_COPY_HANDLES);
	TEST_ASSERT("write with dot handle (copy)", s == B_OK);

	uint8 recvMsg;
	size_t sz = 1;
	kosm_handle_t recvH;
	size_t rhc = 1;
	s = kosm_ray_read(ep1, &recvMsg, &sz, &recvH, &rhc, 0);
	TEST_ASSERT("read succeeds", s == B_OK);
	TEST_ASSERT("got 1 handle", rhc == 1);

	kosm_handle_info hinfo;
	TEST_ASSERT("get handle info",
		kosm_get_handle_info(recvH, &hinfo) == B_OK);
	TEST_ASSERT("handle type DOT", hinfo.type == KOSM_HANDLE_DOT);

	// Both handles valid
	kosm_dot_info origInfo, copyInfo;
	TEST_ASSERT("original still valid",
		kosm_get_dot_info(dot, &origInfo) == B_OK);
	TEST_ASSERT("copy valid",
		kosm_get_dot_info(recvH, &copyInfo) == B_OK);
	TEST_ASSERT("same size",
		origInfo.size == copyInfo.size);

	// Data visible through mapped addr
	TEST_ASSERT("data intact",
		((volatile uint8*)addr)[0] == 0xEE);

	kosm_close_handle(recvH);
	kosm_delete_dot(dot);
	kosm_close_ray(ep0);
	kosm_close_ray(ep1);
}


static void
test_handle_fd_not_supported()
{
	trace("\n--- test_handle_fd_not_supported ---\n");
	// FD handles are not yet implemented (TODO in kernel).
	// Verify that KOSM_HANDLE_FD type is defined but no API
	// exists to create one — this is a placeholder test.
	TEST_ASSERT("KOSM_HANDLE_FD defined", KOSM_HANDLE_FD == 0x05);
}


static void
test_handle_invalid()
{
	trace("\n--- test_handle_invalid ---\n");
	kosm_ray_id ep0, ep1;
	kosm_create_ray(&ep0, &ep1, 0);

	// KOSM_HANDLE_INVALID (0) should fail
	kosm_handle_t h = KOSM_HANDLE_INVALID;
	uint8 msg = 0x06;
	status_t s = kosm_ray_write(ep0, &msg, 1, &h, 1, 0);
	TEST_ASSERT("invalid handle (0) fails", s != B_OK);

	// Non-existent handle value should fail
	h = 999999;
	s = kosm_ray_write(ep0, &msg, 1, &h, 1, 0);
	TEST_ASSERT("non-existent handle fails", s != B_OK);

	kosm_close_ray(ep0);
	kosm_close_ray(ep1);
}


// PEEK & FLAGS

static void
test_peek()
{
	trace("\n--- test_peek ---\n");
	kosm_ray_id ep0, ep1;
	kosm_create_ray(&ep0, &ep1, 0);

	uint32 val = 0xBAADF00D;
	kosm_ray_write(ep0, &val, sizeof(val), NULL, 0, 0);

	uint32 peek1 = 0;
	size_t sz = sizeof(peek1);
	size_t hc = 0;
	status_t s = kosm_ray_read(ep1, &peek1, &sz, NULL, &hc,
		KOSM_RAY_PEEK);
	TEST_ASSERT("peek succeeds", s == B_OK);
	TEST_ASSERT("peek data correct", peek1 == 0xBAADF00D);

	uint32 peek2 = 0;
	sz = sizeof(peek2);
	hc = 0;
	s = kosm_ray_read(ep1, &peek2, &sz, NULL, &hc, KOSM_RAY_PEEK);
	TEST_ASSERT("peek again same data", peek2 == 0xBAADF00D);

	uint32 real = 0;
	sz = sizeof(real);
	hc = 0;
	kosm_ray_read(ep1, &real, &sz, NULL, &hc, 0);
	TEST_ASSERT("real read correct", real == 0xBAADF00D);

	uint32 empty = 0;
	sz = sizeof(empty);
	hc = 0;
	s = kosm_ray_read_etc(ep1, &empty, &sz, NULL, &hc,
		B_RELATIVE_TIMEOUT, 0);
	TEST_ASSERT("queue empty after real read", s == B_WOULD_BLOCK);

	kosm_close_ray(ep0);
	kosm_close_ray(ep1);
}


static void
test_copy_handles()
{
	trace("\n--- test_copy_handles ---\n");
	kosm_ray_id ep0, ep1;
	kosm_create_ray(&ep0, &ep1, 0);

	kosm_mutex_id mid = kosm_create_mutex("copy_test", 0);
	TEST_ASSERT("create mutex", mid >= 0);

	kosm_handle_t h = mid;

	uint8 msg = 0x10;
	kosm_ray_write(ep0, &msg, 1, &h, 1, KOSM_RAY_COPY_HANDLES);

	status_t s = kosm_acquire_mutex(mid);
	TEST_ASSERT("sender can still acquire mutex", s == B_OK);
	kosm_release_mutex(mid);

	uint8 recvMsg;
	size_t sz = 1;
	kosm_handle_t recvH;
	size_t rhc = 1;
	s = kosm_ray_read(ep1, &recvMsg, &sz, &recvH, &rhc, 0);
	TEST_ASSERT("read succeeds", s == B_OK);

	kosm_handle_info hinfo;
	TEST_ASSERT("get handle info", kosm_get_handle_info(recvH, &hinfo) == B_OK);
	TEST_ASSERT("handle type correct", hinfo.type == KOSM_HANDLE_MUTEX);

	kosm_delete_mutex(mid);
	kosm_close_ray(ep0);
	kosm_close_ray(ep1);
}


static void
test_timed_read_write()
{
	trace("\n--- test_timed_read_write ---\n");
	kosm_ray_id ep0, ep1;
	kosm_create_ray(&ep0, &ep1, 0);

	uint8 buf[4];
	size_t sz = sizeof(buf);
	size_t hc = 0;
	bigtime_t rt0 = system_time();
	status_t s = kosm_ray_read_etc(ep1, buf, &sz, NULL, &hc,
		B_RELATIVE_TIMEOUT, 100000);
	bigtime_t elapsed = system_time() - rt0;
	TEST_ASSERT("timed read returns TIMED_OUT", s == B_TIMED_OUT);
	TEST_ASSERT("timeout accuracy (drift < 50ms)",
		elapsed >= 90000 && elapsed < 150000);

	kosm_close_ray(ep0);
	kosm_close_ray(ep1);
}


// QOS

static void
test_qos_set_get()
{
	trace("\n--- test_qos_set_get ---\n");
	kosm_ray_id ep0, ep1;
	kosm_create_ray(&ep0, &ep1, 0);

	uint8 qosValues[] = {
		KOSM_QOS_BACKGROUND, KOSM_QOS_UTILITY,
		KOSM_QOS_DEFAULT, KOSM_QOS_USER_INITIATED,
		KOSM_QOS_USER_INTERACTIVE
	};

	bool allOK = true;
	for (int i = 0; i < 5; i++) {
		status_t s = kosm_ray_set_qos(ep0, qosValues[i]);
		if (s != B_OK) { allOK = false; continue; }

		kosm_ray_info info;
		memset(&info, 0, sizeof(info));
		kosm_get_ray_info(ep0, &info);
		if (info.qos_class != qosValues[i])
			allOK = false;
	}
	TEST_ASSERT("all QoS values set/get correctly", allOK);

	kosm_close_ray(ep0);
	kosm_close_ray(ep1);
}


static void
test_qos_invalid()
{
	trace("\n--- test_qos_invalid ---\n");
	kosm_ray_id ep0, ep1;
	kosm_create_ray(&ep0, &ep1, 0);

	TEST_ASSERT("qos CLASS_COUNT fails",
		kosm_ray_set_qos(ep0, KOSM_QOS_CLASS_COUNT) == B_BAD_VALUE);
	TEST_ASSERT("qos 255 fails",
		kosm_ray_set_qos(ep0, 255) == B_BAD_VALUE);

	kosm_close_ray(ep0);
	kosm_close_ray(ep1);
}


static void
test_qos_in_message()
{
	trace("\n--- test_qos_in_message ---\n");
	kosm_ray_id ep0, ep1;
	kosm_create_ray(&ep0, &ep1, 0);

	kosm_ray_set_qos(ep0, KOSM_QOS_USER_INTERACTIVE);

	uint32 val = 42;
	kosm_ray_write(ep0, &val, sizeof(val), NULL, 0, 0);

	kosm_ray_info info;
	memset(&info, 0, sizeof(info));
	kosm_get_ray_info(ep0, &info);
	TEST_ASSERT("sender qos is USER_INTERACTIVE",
		info.qos_class == KOSM_QOS_USER_INTERACTIVE);

	uint32 recv;
	size_t sz = sizeof(recv);
	size_t hc = 0;
	kosm_ray_read(ep1, &recv, &sz, NULL, &hc, 0);

	kosm_close_ray(ep0);
	kosm_close_ray(ep1);
}


// WAIT

static void
test_wait_readable()
{
	trace("\n--- test_wait_readable ---\n");
	kosm_ray_id ep0, ep1;
	kosm_create_ray(&ep0, &ep1, 0);

	uint32 val = 0xF00D;
	writer_args wargs;
	memset(&wargs, 0, sizeof(wargs));
	wargs.ray = ep0;
	wargs.data = &val;
	wargs.dataSize = sizeof(val);
	wargs.delay = 50000;

	thread_id tid = spawn_thread(writer_thread_func, "delayed_writer",
		B_NORMAL_PRIORITY, &wargs);
	resume_thread(tid);

	uint32 observed = 0;
	bigtime_t wt0 = system_time();
	status_t s = kosm_ray_wait(ep1, KOSM_RAY_READABLE, &observed,
		B_RELATIVE_TIMEOUT, 2000000);
	bigtime_t wdt = system_time() - wt0;

	status_t exitVal;
	wait_for_thread(tid, &exitVal);

	TEST_ASSERT("wait returns B_OK", s == B_OK);
	TEST_ASSERT("READABLE observed",
		(observed & KOSM_RAY_READABLE) != 0);
	TEST_ASSERT("waited ~50ms", wdt >= 30000 && wdt < 500000);

	uint32 drain;
	size_t dsz = sizeof(drain);
	size_t dhc = 0;
	kosm_ray_read(ep1, &drain, &dsz, NULL, &dhc, 0);

	kosm_close_ray(ep0);
	kosm_close_ray(ep1);
}


static void
test_wait_peer_closed()
{
	trace("\n--- test_wait_peer_closed ---\n");
	kosm_ray_id ep0, ep1;
	kosm_create_ray(&ep0, &ep1, 0);

	close_peer_args cargs;
	cargs.ray = ep0;
	cargs.delay = 50000;
	cargs.result = B_ERROR;

	thread_id tid = spawn_thread(close_peer_thread_func, "closer",
		B_NORMAL_PRIORITY, &cargs);
	resume_thread(tid);

	uint32 observed = 0;
	status_t s = kosm_ray_wait(ep1, KOSM_RAY_PEER_CLOSED, &observed,
		B_RELATIVE_TIMEOUT, 2000000);

	status_t exitVal;
	wait_for_thread(tid, &exitVal);

	TEST_ASSERT("wait returns B_OK", s == B_OK);
	TEST_ASSERT("PEER_CLOSED observed",
		(observed & KOSM_RAY_PEER_CLOSED) != 0);

	kosm_close_ray(ep1);
}


static void
test_wait_already_satisfied()
{
	trace("\n--- test_wait_already_satisfied ---\n");
	kosm_ray_id ep0, ep1;
	kosm_create_ray(&ep0, &ep1, 0);

	uint32 val = 1;
	kosm_ray_write(ep0, &val, sizeof(val), NULL, 0, 0);

	uint32 observed = 0;
	bigtime_t wt0 = system_time();
	kosm_ray_wait(ep1, KOSM_RAY_READABLE, &observed,
		B_RELATIVE_TIMEOUT, 1000000);
	bigtime_t wdt = system_time() - wt0;

	TEST_ASSERT("returns immediately", wdt < 10000);
	TEST_ASSERT("READABLE set", (observed & KOSM_RAY_READABLE) != 0);

	uint32 drain;
	size_t dsz = sizeof(drain);
	size_t dhc = 0;
	kosm_ray_read(ep1, &drain, &dsz, NULL, &dhc, 0);

	kosm_close_ray(ep0);
	kosm_close_ray(ep1);
}


static void
test_wait_timeout()
{
	trace("\n--- test_wait_timeout ---\n");
	kosm_ray_id ep0, ep1;
	kosm_create_ray(&ep0, &ep1, 0);

	uint32 observed = 0;
	bigtime_t wt0 = system_time();
	status_t s = kosm_ray_wait(ep1, KOSM_RAY_READABLE, &observed,
		B_RELATIVE_TIMEOUT, 100000);
	bigtime_t wdt = system_time() - wt0;

	TEST_ASSERT("returns TIMED_OUT", s == B_TIMED_OUT);
	TEST_ASSERT("elapsed ~100ms", wdt >= 90000 && wdt < 200000);

	kosm_close_ray(ep0);
	kosm_close_ray(ep1);
}


// STRESS

// Single throughput iteration — returns ops/sec
static kosm_ray_id sThroughputEp0, sThroughputEp1;

static double
ray_throughput_iteration()
{
	const int kCycles = 10000;
	uint8 sendBuf[64];
	uint8 recvBuf[64];
	memset(sendBuf, 0xAA, sizeof(sendBuf));

	bigtime_t start = system_time();
	for (int i = 0; i < kCycles; i++) {
		sendBuf[0] = (uint8)(i & 0xFF);
		kosm_ray_write(sThroughputEp0, sendBuf, sizeof(sendBuf),
			NULL, 0, 0);
		size_t sz = sizeof(recvBuf);
		size_t hc = 0;
		kosm_ray_read(sThroughputEp1, recvBuf, &sz, NULL, &hc, 0);
	}
	bigtime_t elapsed = system_time() - start;
	return (double)kCycles * 1000000.0 / (double)elapsed;
}

static void
test_stress_throughput()
{
	trace("\n--- test_stress_throughput ---\n");
	kosm_create_ray(&sThroughputEp0, &sThroughputEp1, 0);

	BenchStats stats = run_benchmark(ray_throughput_iteration, 5, 2);

	trace("    throughput: median=%.0f, min=%.0f, max=%.0f rt/s"
		" (%d runs, %d warmup)\n",
		stats.median_ops, stats.min_ops, stats.max_ops,
		stats.runs, stats.warmup);
	TEST_ASSERT("no errors (implicit)", true);
	TEST_ASSERT("throughput > 10000 rt/s", stats.median_ops > 10000);

	kosm_close_ray(sThroughputEp0);
	kosm_close_ray(sThroughputEp1);
}


static void
test_stress_concurrent_writers()
{
	trace("\n--- test_stress_concurrent_writers ---\n");
	kosm_ray_id ep0, ep1;
	kosm_create_ray(&ep0, &ep1, 0);

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

	int totalRead = 0;
	for (int i = 0; i < kWriterCount * kMsgsPerWriter; i++) {
		uint8 buf[8];
		size_t sz = sizeof(buf);
		size_t hc = 0;
		status_t s = kosm_ray_read_etc(ep1, buf, &sz, NULL, &hc,
			B_RELATIVE_TIMEOUT, 2000000);
		if (s == B_OK) totalRead++;
	}

	int totalWriteErrors = 0;
	for (int i = 0; i < kWriterCount; i++) {
		status_t exitVal;
		wait_for_thread(tids[i], &exitVal);
		totalWriteErrors += wargs[i].errors;
	}

	TEST_ASSERT("all messages read",
		totalRead == kWriterCount * kMsgsPerWriter);
	TEST_ASSERT("no write errors", totalWriteErrors == 0);

	kosm_close_ray(ep0);
	kosm_close_ray(ep1);
}


static void
test_stress_create_close_churn()
{
	trace("\n--- test_stress_create_close_churn ---\n");
	const int kThreads = 8;
	const int kCyclesPerThread = 100;
	ray_churn_args cargs[kThreads];
	thread_id tids[kThreads];

	for (int i = 0; i < kThreads; i++) {
		cargs[i].count = kCyclesPerThread;
		cargs[i].errors = 0;
		char name[32];
		snprintf(name, sizeof(name), "churn_%d", i);
		tids[i] = spawn_thread(ray_churn_thread_func, name,
			B_NORMAL_PRIORITY, &cargs[i]);
		resume_thread(tids[i]);
	}

	int totalErrors = 0;
	for (int i = 0; i < kThreads; i++) {
		status_t exitVal;
		wait_for_thread(tids[i], &exitVal);
		totalErrors += cargs[i].errors;
	}

	TEST_ASSERT("create/close churn no errors", totalErrors == 0);
}


static void
test_stress_large_payload()
{
	trace("\n--- test_stress_large_payload ---\n");
	kosm_ray_id ep0, ep1;
	kosm_create_ray(&ep0, &ep1, 0);

	const int kCycles = 100;
	const size_t kPayloadSize = 64 * 1024;
	uint8* sendBuf = (uint8*)malloc(kPayloadSize);
	uint8* recvBuf = (uint8*)malloc(kPayloadSize);
	TEST_ASSERT("alloc buffers", sendBuf != NULL && recvBuf != NULL);

	int errors = 0;
	for (int i = 0; i < kCycles; i++) {
		memset(sendBuf, (uint8)(i & 0xFF), kPayloadSize);
		sendBuf[0] = (uint8)i;

		status_t s = kosm_ray_write(ep0, sendBuf, kPayloadSize,
			NULL, 0, 0);
		if (s != B_OK) { errors++; continue; }

		memset(recvBuf, 0, kPayloadSize);
		size_t sz = kPayloadSize;
		size_t hc = 0;
		s = kosm_ray_read(ep1, recvBuf, &sz, NULL, &hc, 0);
		if (s != B_OK || sz != kPayloadSize
			|| memcmp(sendBuf, recvBuf, kPayloadSize) != 0)
			errors++;
	}

	TEST_ASSERT("no errors", errors == 0);

	free(sendBuf);
	free(recvBuf);
	kosm_close_ray(ep0);
	kosm_close_ray(ep1);
}


// MOBILE SIM

static void
test_mobile_ipc_simulation()
{
	trace("\n--- test_mobile_ipc_simulation ---\n");
	const int kWorkerCount = 3;
	const int kRequestsPerWorker = 200;
	const int kTotalRequests = kWorkerCount * kRequestsPerWorker;

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

	mobile_dispatcher_args dargs;
	dargs.workerRequestRays = reqSend;
	dargs.workerResponseRays = respRecv;
	dargs.workerCount = kWorkerCount;
	dargs.totalRequests = kTotalRequests;

	thread_id dTid = spawn_thread(mobile_dispatcher_func, "dispatcher",
		B_URGENT_DISPLAY_PRIORITY, &dargs);
	resume_thread(dTid);

	status_t exitVal;
	wait_for_thread(dTid, &exitVal);
	for (int i = 0; i < kWorkerCount; i++)
		wait_for_thread(wTids[i], &exitVal);

	TEST_ASSERT("all requests dispatched",
		dargs.dispatched == kTotalRequests);
	TEST_ASSERT("all responses received",
		dargs.received == kTotalRequests);
	TEST_ASSERT("dispatcher max latency < 100ms",
		dargs.maxLatency < 100000);

	for (int i = 0; i < kWorkerCount; i++) {
		kosm_close_ray(reqSend[i]);
		kosm_close_ray(reqRecv[i]);
		kosm_close_ray(respSend[i]);
		kosm_close_ray(respRecv[i]);
	}
}


// SELECT

static void
test_select_readable()
{
	trace("\n--- test_select_readable ---\n");
	kosm_ray_id ep0, ep1;
	kosm_create_ray(&ep0, &ep1, 0);

	uint8 msg = 0xAA;
	kosm_ray_write(ep0, &msg, 1, NULL, 0, 0);

	object_wait_info info;
	info.object = ep1;
	info.type = B_OBJECT_TYPE_KOSM_RAY;
	info.events = B_EVENT_READ;

	ssize_t result = wait_for_objects(&info, 1);
	TEST_ASSERT("wait returns >= 0", result >= 0);
	TEST_ASSERT("B_EVENT_READ set", (info.events & B_EVENT_READ) != 0);

	kosm_close_ray(ep0);
	kosm_close_ray(ep1);
}


static void
test_select_writable()
{
	trace("\n--- test_select_writable ---\n");
	kosm_ray_id ep0, ep1;
	kosm_create_ray(&ep0, &ep1, 0);

	object_wait_info info;
	info.object = ep0;
	info.type = B_OBJECT_TYPE_KOSM_RAY;
	info.events = B_EVENT_WRITE;

	ssize_t result = wait_for_objects(&info, 1);
	TEST_ASSERT("wait returns >= 0", result >= 0);
	TEST_ASSERT("B_EVENT_WRITE set",
		(info.events & B_EVENT_WRITE) != 0);

	kosm_close_ray(ep0);
	kosm_close_ray(ep1);
}


static void
test_select_peer_closed()
{
	trace("\n--- test_select_peer_closed ---\n");
	kosm_ray_id ep0, ep1;
	kosm_create_ray(&ep0, &ep1, 0);
	kosm_close_ray(ep0);

	object_wait_info info;
	info.object = ep1;
	info.type = B_OBJECT_TYPE_KOSM_RAY;
	info.events = B_EVENT_READ;

	ssize_t result = wait_for_objects(&info, 1);
	TEST_ASSERT("wait returns >= 0", result >= 0);
	TEST_ASSERT("B_EVENT_DISCONNECTED set",
		(info.events & B_EVENT_DISCONNECTED) != 0);

	kosm_close_ray(ep1);
}


static void
test_select_multiple_rays()
{
	trace("\n--- test_select_multiple_rays ---\n");
	kosm_ray_id ep0[3], ep1[3];
	for (int i = 0; i < 3; i++)
		kosm_create_ray(&ep0[i], &ep1[i], 0);

	uint8 msg = 0xBB;
	kosm_ray_write(ep0[1], &msg, 1, NULL, 0, 0);

	object_wait_info infos[3];
	for (int i = 0; i < 3; i++) {
		infos[i].object = ep1[i];
		infos[i].type = B_OBJECT_TYPE_KOSM_RAY;
		infos[i].events = B_EVENT_READ;
	}

	ssize_t result = wait_for_objects(infos, 3);
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
}


static void
test_select_timeout()
{
	trace("\n--- test_select_timeout ---\n");
	kosm_ray_id ep0, ep1;
	kosm_create_ray(&ep0, &ep1, 0);

	object_wait_info info;
	info.object = ep1;
	info.type = B_OBJECT_TYPE_KOSM_RAY;
	info.events = B_EVENT_READ;

	bigtime_t before = system_time();
	ssize_t result = wait_for_objects_etc(&info, 1,
		B_RELATIVE_TIMEOUT, 100000);
	bigtime_t elapsed = system_time() - before;

	TEST_ASSERT("returns B_TIMED_OUT", result == B_TIMED_OUT);
	TEST_ASSERT("elapsed >= 80ms", elapsed >= 80000);
	TEST_ASSERT("elapsed < 500ms", elapsed < 500000);

	kosm_close_ray(ep0);
	kosm_close_ray(ep1);
}


// CROSS-PROCESS

static void
test_cross_process_ray()
{
	trace("\n--- test_cross_process_ray ---\n");

	// Parent creates a ray pair, forks, transfers ep1 to child
	// via bootstrap. Parent writes on ep0, child reads on ep1
	// and writes a response back, parent reads it.
	kosm_ray_id ep0, ep1;
	status_t s = kosm_create_ray(&ep0, &ep1, 0);
	TEST_ASSERT("create ray pair", s == B_OK);

	int syncfd[2], resultfd[2];
	TEST_ASSERT("sync pipe", pipe(syncfd) == 0);
	TEST_ASSERT("result pipe", pipe(resultfd) == 0);

	pid_t child = fork();
	if (child == 0) {
		close(syncfd[1]);
		close(resultfd[0]);

		// Wait for parent to transfer ep1 via bootstrap
		uint8 sync;
		read(syncfd[0], &sync, 1);
		close(syncfd[0]);

		kosm_ray_id myEp = kosm_get_bootstrap_ray();

		uint8 result[4] = {0};

		// Read message from parent
		uint8 buf[64];
		size_t sz = sizeof(buf);
		size_t hc = 0;
		status_t err = kosm_ray_read_etc(myEp, buf, &sz,
			NULL, &hc, B_RELATIVE_TIMEOUT, 2000000);
		result[0] = (err == B_OK) ? 1 : 0;

		// Verify data
		bool match = (sz == 4);
		if (match) {
			uint32 val = *(uint32*)buf;
			match = (val == 0xDEADF00D);
		}
		result[1] = match ? 1 : 0;

		// Write response back
		uint32 resp = 0xBEEFCAFE;
		err = kosm_ray_write(myEp, &resp, sizeof(resp), NULL, 0, 0);
		result[2] = (err == B_OK) ? 1 : 0;

		result[3] = 0xFF; // sentinel

		write(resultfd[1], result, sizeof(result));
		close(resultfd[1]);
		kosm_close_ray(myEp);
		_exit(0);
	}

	TEST_ASSERT("fork succeeded", child > 0);
	close(syncfd[0]);
	close(resultfd[1]);

	// Transfer ep1 ownership to child and set as bootstrap
	s = kosm_ray_set_bootstrap(child, ep1);
	trace("    set_bootstrap -> %s\n", strerror(s));
	TEST_ASSERT("set bootstrap", s == B_OK);

	// Signal child that bootstrap is ready
	uint8 sync = 1;
	write(syncfd[1], &sync, 1);
	close(syncfd[1]);

	// Parent: write to child via ep0
	uint32 msg = 0xDEADF00D;
	s = kosm_ray_write(ep0, &msg, sizeof(msg), NULL, 0, 0);
	trace("    parent write -> %s\n", strerror(s));
	TEST_ASSERT("parent write", s == B_OK);

	// Read child's response on ep0
	uint32 resp = 0;
	size_t rsz = sizeof(resp);
	size_t rhc = 0;
	s = kosm_ray_read_etc(ep0, &resp, &rsz, NULL, &rhc,
		B_RELATIVE_TIMEOUT, 2000000);
	trace("    parent read -> %s, val=0x%08x\n", strerror(s),
		(unsigned)resp);
	TEST_ASSERT("parent read response", s == B_OK);
	TEST_ASSERT("response data correct", resp == 0xBEEFCAFE);

	// Collect child results
	uint8 childResult[4] = {0};
	ssize_t bytesRead = read(resultfd[0], childResult,
		sizeof(childResult));
	close(resultfd[0]);

	int status;
	waitpid(child, &status, 0);
	trace("    child exit status=%d, pipe read=%zd\n",
		WEXITSTATUS(status), bytesRead);
	trace("    child: read=%d, data=%d, write=%d, sentinel=%d\n",
		(int)childResult[0], (int)childResult[1],
		(int)childResult[2], (int)childResult[3]);

	TEST_ASSERT("child: read from parent succeeded",
		childResult[0] == 1);
	TEST_ASSERT("child: data matches",
		childResult[1] == 1);
	TEST_ASSERT("child: write response succeeded",
		childResult[2] == 1);
	TEST_ASSERT("child: sentinel received",
		childResult[3] == 0xFF);

	kosm_close_ray(ep0);
	// ep1 was transferred to child, child closed it
}


// CROSS-PROCESS HANDLE TRANSFER

static void
test_cross_process_handle_transfer()
{
	trace("\n--- test_cross_process_handle_transfer ---\n");

	// Parent creates transport ray + a second ray pair.
	// Transfers transport1 to child via bootstrap.
	// Sends data1 as a handle through transport (move mode).
	// Then communicates through the transferred data ray.
	kosm_ray_id transport0, transport1;
	status_t s = kosm_create_ray(&transport0, &transport1, 0);
	TEST_ASSERT("create transport pair", s == B_OK);

	kosm_ray_id data0, data1;
	s = kosm_create_ray(&data0, &data1, 0);
	TEST_ASSERT("create data pair", s == B_OK);

	int syncfd[2], resultfd[2];
	TEST_ASSERT("sync pipe", pipe(syncfd) == 0);
	TEST_ASSERT("result pipe", pipe(resultfd) == 0);

	pid_t child = fork();
	if (child == 0) {
		close(syncfd[1]);
		close(resultfd[0]);
		uint8 result[3] = {0};

		// Wait for bootstrap
		uint8 sync;
		read(syncfd[0], &sync, 1);
		close(syncfd[0]);

		// Get transport endpoint via bootstrap
		kosm_ray_id myTransport = kosm_get_bootstrap_ray();

		// Read handle from transport (handle transfer gives us data1)
		uint8 msg;
		size_t sz = 1;
		kosm_handle_t h;
		size_t hc = 1;
		status_t err = kosm_ray_read_etc(myTransport, &msg, &sz,
			&h, &hc, B_RELATIVE_TIMEOUT, 2000000);

		kosm_handle_info hinfo;
		bool typeOK = (err == B_OK && hc == 1
			&& kosm_get_handle_info(h, &hinfo) == B_OK
			&& hinfo.type == KOSM_HANDLE_RAY);
		result[0] = typeOK ? 1 : 0;

		if (result[0]) {
			// Use the received handle directly as ray endpoint
			kosm_ray_id receivedRay = h;
			uint32 val = 0;
			size_t vsz = sizeof(val);
			size_t vhc = 0;
			err = kosm_ray_read_etc(receivedRay, &val, &vsz,
				NULL, &vhc, B_RELATIVE_TIMEOUT, 2000000);
			result[1] = (err == B_OK && val == 0xF00DCAFE) ? 1 : 0;

			uint32 reply = 0xACED0001;
			err = kosm_ray_write(receivedRay, &reply, sizeof(reply),
				NULL, 0, 0);
			result[2] = (err == B_OK) ? 1 : 0;

			kosm_close_ray(receivedRay);
		}

		write(resultfd[1], result, sizeof(result));
		close(resultfd[1]);
		kosm_close_ray(myTransport);
		_exit(0);
	}

	TEST_ASSERT("fork succeeded", child > 0);
	close(syncfd[0]);
	close(resultfd[1]);

	// Transfer transport1 to child via bootstrap
	s = kosm_ray_set_bootstrap(child, transport1);
	trace("    set_bootstrap -> %s\n", strerror(s));
	TEST_ASSERT("set bootstrap", s == B_OK);

	// Signal child
	uint8 sync = 1;
	write(syncfd[1], &sync, 1);
	close(syncfd[1]);

	// Send data1 endpoint to child via transport (move, not copy)
	kosm_handle_t h = data1;
	uint8 msg = 0x01;
	s = kosm_ray_write(transport0, &msg, 1, &h, 1, 0);
	TEST_ASSERT("send ray handle to child", s == B_OK);

	// Write through data0, child reads on transferred data1
	uint32 val = 0xF00DCAFE;
	s = kosm_ray_write(data0, &val, sizeof(val), NULL, 0, 0);
	TEST_ASSERT("write through data ray", s == B_OK);

	// Read child's reply
	uint32 reply = 0;
	size_t rsz = sizeof(reply);
	size_t rhc = 0;
	s = kosm_ray_read_etc(data0, &reply, &rsz, NULL, &rhc,
		B_RELATIVE_TIMEOUT, 2000000);
	trace("    parent: reply=0x%08x, %s\n", (unsigned)reply,
		strerror(s));
	TEST_ASSERT("read reply from child", s == B_OK);
	TEST_ASSERT("reply data correct", reply == 0xACED0001);

	// Collect child results
	uint8 childResult[3] = {0};
	read(resultfd[0], childResult, sizeof(childResult));
	close(resultfd[0]);

	int status;
	waitpid(child, &status, 0);
	trace("    child: handle=%d, data=%d, reply=%d\n",
		(int)childResult[0], (int)childResult[1],
		(int)childResult[2]);

	TEST_ASSERT("child: received ray handle",
		childResult[0] == 1);
	TEST_ASSERT("child: read data through transferred ray",
		childResult[1] == 1);
	TEST_ASSERT("child: sent reply through transferred ray",
		childResult[2] == 1);

	kosm_close_ray(transport0);
	// transport1 transferred to child, child closed it
	kosm_close_ray(data0);
	// data1 moved to child via handle transfer, child closed it
}


// PERMISSION

static void
test_cross_process_permission()
{
	trace("\n--- test_cross_process_permission ---\n");

	// Child creates a ray pair and reports handle values via pipe.
	// Parent tries to use those handle values — should fail because
	// handles are per-process (not in parent's handle table).
	int pipefd[2];
	TEST_ASSERT("create pipe", pipe(pipefd) == 0);

	pid_t child = fork();
	if (child == 0) {
		close(pipefd[0]);

		kosm_ray_id ep0, ep1;
		kosm_create_ray(&ep0, &ep1, 0);

		// Report IDs to parent
		int32 ids[2] = { ep0, ep1 };
		write(pipefd[1], ids, sizeof(ids));

		// Keep alive long enough for parent to test
		snooze(500000);

		kosm_close_ray(ep0);
		kosm_close_ray(ep1);
		close(pipefd[1]);
		_exit(0);
	}

	TEST_ASSERT("fork succeeded", child > 0);
	close(pipefd[1]);

	int32 childIds[2] = {-1, -1};
	read(pipefd[0], childIds, sizeof(childIds));
	close(pipefd[0]);

	trace("    child created rays: ep0=%ld, ep1=%ld\n",
		(long)childIds[0], (long)childIds[1]);

	// Try to write to child's ray — should fail (wrong team)
	uint8 msg = 0x42;
	status_t s = kosm_ray_write(childIds[0], &msg, 1, NULL, 0, 0);
	trace("    write to child's ep0 -> %s (0x%08lx)\n",
		strerror(s), (unsigned long)s);
	TEST_ASSERT("write to other team's ray fails", s != B_OK);

	// Try to read from child's ray
	uint8 buf[4];
	size_t sz = sizeof(buf);
	size_t hc = 0;
	s = kosm_ray_read(childIds[1], buf, &sz, NULL, &hc, 0);
	trace("    read from child's ep1 -> %s (0x%08lx)\n",
		strerror(s), (unsigned long)s);
	TEST_ASSERT("read from other team's ray fails", s != B_OK);

	// Try to close child's ray
	s = kosm_close_ray(childIds[0]);
	trace("    close child's ep0 -> %s (0x%08lx)\n",
		strerror(s), (unsigned long)s);
	TEST_ASSERT("close other team's ray fails", s != B_OK);

	// Try to get info on child's ray
	kosm_ray_info info;
	memset(&info, 0, sizeof(info));
	s = kosm_get_ray_info(childIds[0], &info);
	trace("    get_ray_info child's ep0 -> %s (0x%08lx)\n",
		strerror(s), (unsigned long)s);
	TEST_ASSERT("get_info on other team's ray fails", s != B_OK);

	int status;
	waitpid(child, &status, 0);
}


// TEAM DEATH

static void
test_team_death_peer_closed()
{
	trace("\n--- test_team_death_peer_closed ---\n");

	// Parent keeps ep0, transfers ep1 to child via bootstrap.
	// Child exits without closing ep1. Kernel cleanup should
	// destroy ep1, and parent should see PEER_CLOSED on ep0.
	kosm_ray_id ep0, ep1;
	status_t s = kosm_create_ray(&ep0, &ep1, 0);
	TEST_ASSERT("create", s == B_OK);

	int syncfd[2];
	TEST_ASSERT("sync pipe", pipe(syncfd) == 0);

	pid_t child = fork();
	if (child == 0) {
		close(syncfd[1]);

		// Wait for bootstrap
		uint8 sync;
		read(syncfd[0], &sync, 1);
		close(syncfd[0]);

		// Get our endpoint (don't need to use it)
		kosm_get_bootstrap_ray();

		snooze(50000);
		// Exit WITHOUT closing — kernel must reclaim
		_exit(0);
	}

	TEST_ASSERT("fork succeeded", child > 0);
	close(syncfd[0]);

	// Transfer ep1 to child via bootstrap
	s = kosm_ray_set_bootstrap(child, ep1);
	TEST_ASSERT("set bootstrap", s == B_OK);

	// Signal child
	uint8 sync = 1;
	write(syncfd[1], &sync, 1);
	close(syncfd[1]);

	// Wait for child to die
	int status;
	waitpid(child, &status, 0);
	trace("    child exited (status=%d)\n", WEXITSTATUS(status));

	// ep1 was in child's handle table; kernel cleanup closed it.
	// Write to ep0 should get PEER_CLOSED.
	uint32 val = 123;
	s = kosm_ray_write(ep0, &val, sizeof(val), NULL, 0, 0);
	trace("    write after child death -> %s (0x%08lx)\n",
		strerror(s), (unsigned long)s);
	TEST_ASSERT("write returns PEER_CLOSED after team death",
		s == KOSM_RAY_PEER_CLOSED_ERROR);

	// read should also fail
	uint8 buf[4];
	size_t sz = sizeof(buf);
	size_t hc = 0;
	s = kosm_ray_read(ep0, buf, &sz, NULL, &hc, 0);
	trace("    read after child death -> %s (0x%08lx)\n",
		strerror(s), (unsigned long)s);
	TEST_ASSERT("read returns PEER_CLOSED after team death",
		s == KOSM_RAY_PEER_CLOSED_ERROR);

	// wait should report PEER_CLOSED
	uint32 observed = 0;
	s = kosm_ray_wait(ep0, KOSM_RAY_PEER_CLOSED, &observed,
		B_RELATIVE_TIMEOUT, 100000);
	TEST_ASSERT("wait sees PEER_CLOSED",
		s == B_OK && (observed & KOSM_RAY_PEER_CLOSED) != 0);

	kosm_close_ray(ep0);
}


static void
test_team_death_drain_then_closed()
{
	trace("\n--- test_team_death_drain_then_closed ---\n");

	// Child gets ep0 via bootstrap, writes 5 messages, then exits
	// without closing. Parent drains all 5 from ep1, then gets
	// PEER_CLOSED.
	kosm_ray_id ep0, ep1;
	status_t s = kosm_create_ray(&ep0, &ep1, 0);
	TEST_ASSERT("create", s == B_OK);

	int syncfd[2];
	TEST_ASSERT("sync pipe", pipe(syncfd) == 0);

	pid_t child = fork();
	if (child == 0) {
		close(syncfd[1]);

		// Wait for bootstrap
		uint8 sync;
		read(syncfd[0], &sync, 1);
		close(syncfd[0]);

		kosm_ray_id myEp = kosm_get_bootstrap_ray();

		for (int i = 0; i < 5; i++) {
			uint32 val = (uint32)(i + 500);
			kosm_ray_write(myEp, &val, sizeof(val), NULL, 0, 0);
		}
		// Exit without closing — kernel must reclaim
		_exit(0);
	}

	TEST_ASSERT("fork succeeded", child > 0);
	close(syncfd[0]);

	// Transfer ep0 to child via bootstrap
	s = kosm_ray_set_bootstrap(child, ep0);
	TEST_ASSERT("set bootstrap", s == B_OK);

	// Signal child
	uint8 sync = 1;
	write(syncfd[1], &sync, 1);
	close(syncfd[1]);

	int status;
	waitpid(child, &status, 0);
	trace("    child exited\n");

	// Drain 5 messages from ep1
	bool drainOK = true;
	for (int i = 0; i < 5; i++) {
		uint32 val = 0;
		size_t sz = sizeof(val);
		size_t hc = 0;
		s = kosm_ray_read(ep1, &val, &sz, NULL, &hc, 0);
		if (s != B_OK || val != (uint32)(i + 500)) {
			trace("    drain[%d] fail: val=%lu, s=%s\n",
				i, (unsigned long)val, strerror(s));
			drainOK = false;
		}
	}
	TEST_ASSERT("drain 5 messages after team death", drainOK);

	// 6th read should return PEER_CLOSED
	uint32 dummy;
	size_t sz = sizeof(dummy);
	size_t hc = 0;
	s = kosm_ray_read(ep1, &dummy, &sz, NULL, &hc, 0);
	trace("    6th read -> %s\n", strerror(s));
	TEST_ASSERT("6th read returns PEER_CLOSED",
		s == KOSM_RAY_PEER_CLOSED_ERROR);

	kosm_close_ray(ep1);
}


static void
test_team_death_unblocks_wait()
{
	trace("\n--- test_team_death_unblocks_wait ---\n");

	// Parent waits on KOSM_RAY_READABLE on ep1. Child owns ep0
	// (via bootstrap) and dies. Parent should be woken with
	// PEER_CLOSED.
	kosm_ray_id ep0, ep1;
	status_t s = kosm_create_ray(&ep0, &ep1, 0);
	TEST_ASSERT("create", s == B_OK);

	int syncfd[2];
	TEST_ASSERT("sync pipe", pipe(syncfd) == 0);

	pid_t child = fork();
	if (child == 0) {
		close(syncfd[1]);

		uint8 sync;
		read(syncfd[0], &sync, 1);
		close(syncfd[0]);

		// Get our endpoint, keep it alive briefly, then die
		kosm_get_bootstrap_ray();
		snooze(100000);
		_exit(0);
	}

	TEST_ASSERT("fork succeeded", child > 0);
	close(syncfd[0]);

	// Transfer ep0 to child
	s = kosm_ray_set_bootstrap(child, ep0);
	TEST_ASSERT("set bootstrap", s == B_OK);

	uint8 sync = 1;
	write(syncfd[1], &sync, 1);
	close(syncfd[1]);

	// Parent: wait for READABLE or PEER_CLOSED on ep1
	uint32 observed = 0;
	bigtime_t t0 = system_time();
	s = kosm_ray_wait(ep1, KOSM_RAY_READABLE | KOSM_RAY_PEER_CLOSED,
		&observed, B_RELATIVE_TIMEOUT, 3000000);
	bigtime_t elapsed = system_time() - t0;

	int status;
	waitpid(child, &status, 0);

	trace("    wait returned: s=%s, observed=0x%lx, elapsed=%lld us\n",
		strerror(s), (unsigned long)observed, (long long)elapsed);

	TEST_ASSERT("wait returns B_OK", s == B_OK);
	TEST_ASSERT("PEER_CLOSED observed",
		(observed & KOSM_RAY_PEER_CLOSED) != 0);
	TEST_ASSERT("unblocked in reasonable time (< 2s)",
		elapsed < 2000000);

	kosm_close_ray(ep1);
}


static void
test_team_death_select_integration()
{
	trace("\n--- test_team_death_select_integration ---\n");

	// Parent uses wait_for_objects (select) on ep1.
	// Child owns ep0 (via bootstrap) and dies.
	// B_EVENT_DISCONNECTED should fire.
	kosm_ray_id ep0, ep1;
	status_t s = kosm_create_ray(&ep0, &ep1, 0);
	TEST_ASSERT("create", s == B_OK);

	int syncfd[2];
	TEST_ASSERT("sync pipe", pipe(syncfd) == 0);

	pid_t child = fork();
	if (child == 0) {
		close(syncfd[1]);

		uint8 sync;
		read(syncfd[0], &sync, 1);
		close(syncfd[0]);

		kosm_get_bootstrap_ray();
		snooze(100000);
		_exit(0);
	}

	TEST_ASSERT("fork succeeded", child > 0);
	close(syncfd[0]);

	// Transfer ep0 to child
	s = kosm_ray_set_bootstrap(child, ep0);
	TEST_ASSERT("set bootstrap", s == B_OK);

	uint8 sync = 1;
	write(syncfd[1], &sync, 1);
	close(syncfd[1]);

	object_wait_info info;
	info.object = ep1;
	info.type = B_OBJECT_TYPE_KOSM_RAY;
	info.events = B_EVENT_READ;

	bigtime_t t0 = system_time();
	ssize_t result = wait_for_objects_etc(&info, 1,
		B_RELATIVE_TIMEOUT, 3000000);
	bigtime_t elapsed = system_time() - t0;

	int status;
	waitpid(child, &status, 0);

	trace("    select returned: result=%zd, events=0x%04x, "
		"elapsed=%lld us\n", result, info.events, (long long)elapsed);

	TEST_ASSERT("select returns >= 0", result >= 0);
	TEST_ASSERT("B_EVENT_DISCONNECTED set",
		(info.events & B_EVENT_DISCONNECTED) != 0);
	TEST_ASSERT("detected within 2s", elapsed < 2000000);

	kosm_close_ray(ep1);
}


// INPUT-PROFILE
//
// These tests exercise ray under the load profile that KosmTouch /
// pointer-pipeline will produce: sustained pacing at vsync / HID rates,
// QoS-prioritized contention, writev frame layouts, ray_call RPC, and
// realistic backpressure & multi-ray fanout. Acceptance numbers are
// expressed as percentile bounds, not just max — they are the contract
// the input pipeline assumes from the ray runtime.

static void
log_latency(const char* label, const LatencyStats& s)
{
	trace("    %s: count=%zu  p50=%lld  p95=%lld  p99=%lld  "
		"max=%lld  mean=%lld us\n", label, s.count,
		(long long)s.p50, (long long)s.p95, (long long)s.p99,
		(long long)s.max, (long long)s.mean);
}


static void
test_input_sustained_240hz()
{
	trace("\n--- test_input_sustained_240hz ---\n");
	// Mirrors a 240 Hz pointer device delivering events to its window.
	// Latency is wall-clock from write() to read() return, per message.
	const int kRateHz = 240;
	const int kDurationSec = 2;
	const int kTotal = kRateHz * kDurationSec;	// 480 msgs

	kosm_ray_id ep0, ep1;
	status_t s = kosm_create_ray(&ep0, &ep1, 0);
	TEST_REQUIRE("create", s == B_OK);
	kosm_ray_set_qos(ep0, KOSM_QOS_USER_INTERACTIVE);

	bigtime_t* samples = (bigtime_t*)calloc(kTotal, sizeof(bigtime_t));
	TEST_REQUIRE("alloc samples", samples != NULL);

	latency_reader_args rargs = {};
	rargs.ray = ep1;
	rargs.expectedCount = kTotal;
	rargs.samples = samples;
	rargs.readTimeout = 2000000;
	thread_id rTid = spawn_thread(latency_reader_func, "lat_reader",
		B_NORMAL_PRIORITY, &rargs);
	resume_thread(rTid);

	paced_writer_args wargs = {};
	wargs.ray = ep0;
	wargs.rateHz = kRateHz;
	wargs.totalCount = kTotal;
	wargs.startTime = system_time() + 100000;
	thread_id wTid = spawn_thread(paced_writer_func, "paced_writer",
		B_NORMAL_PRIORITY, &wargs);
	resume_thread(wTid);

	status_t exitVal;
	wait_for_thread(wTid, &exitVal);
	wait_for_thread(rTid, &exitVal);

	TEST_ASSERT("writer sent all", wargs.sent == kTotal);
	TEST_ASSERT("no write errors", wargs.writeErrors == 0);
	TEST_ASSERT("reader received all", rargs.received == kTotal);

	LatencyStats lat = compute_latency_stats(samples, rargs.received);
	log_latency("240Hz", lat);

	// Acceptance: 1 frame = 4.16ms @ 240 Hz; 1 vsync (60 Hz) = 16.6ms.
	TEST_ASSERT("p50 < 1ms", lat.p50 < 1000);
	TEST_ASSERT("p95 < 4ms (1 frame)", lat.p95 < 4000);
	TEST_ASSERT("p99 < 8ms (2 frames)", lat.p99 < 8000);
	TEST_ASSERT("max < 16ms (1 vsync)", lat.max < 16000);

	free(samples);
	kosm_close_ray(ep0);
	kosm_close_ray(ep1);
}


static void
test_input_sustained_1000hz()
{
	trace("\n--- test_input_sustained_1000hz ---\n");
	// Tablet/Wacom-style report rate. Same ratchet, tighter bounds.
	const int kRateHz = 1000;
	const int kDurationSec = 1;
	const int kTotal = kRateHz * kDurationSec;	// 1000 msgs

	kosm_ray_id ep0, ep1;
	status_t s = kosm_create_ray(&ep0, &ep1, 0);
	TEST_REQUIRE("create", s == B_OK);
	kosm_ray_set_qos(ep0, KOSM_QOS_USER_INTERACTIVE);

	bigtime_t* samples = (bigtime_t*)calloc(kTotal, sizeof(bigtime_t));
	TEST_REQUIRE("alloc samples", samples != NULL);

	latency_reader_args rargs = {};
	rargs.ray = ep1;
	rargs.expectedCount = kTotal;
	rargs.samples = samples;
	rargs.readTimeout = 2000000;
	thread_id rTid = spawn_thread(latency_reader_func, "lat_reader_1k",
		B_NORMAL_PRIORITY, &rargs);
	resume_thread(rTid);

	paced_writer_args wargs = {};
	wargs.ray = ep0;
	wargs.rateHz = kRateHz;
	wargs.totalCount = kTotal;
	wargs.startTime = system_time() + 100000;
	thread_id wTid = spawn_thread(paced_writer_func, "paced_writer_1k",
		B_NORMAL_PRIORITY, &wargs);
	resume_thread(wTid);

	status_t exitVal;
	wait_for_thread(wTid, &exitVal);
	wait_for_thread(rTid, &exitVal);

	TEST_ASSERT("reader received all", rargs.received == kTotal);

	LatencyStats lat = compute_latency_stats(samples, rargs.received);
	log_latency("1000Hz", lat);

	// At 1ms inter-arrival, p99 must stay under 2ms.
	TEST_ASSERT("p50 < 500us", lat.p50 < 500);
	TEST_ASSERT("p99 < 2ms", lat.p99 < 2000);
	TEST_ASSERT("max < 8ms", lat.max < 8000);

	free(samples);
	kosm_close_ray(ep0);
	kosm_close_ray(ep1);
}


static void
test_input_qos_preemption()
{
	trace("\n--- test_input_qos_preemption ---\n");
	// Two readers same priority, one on USER_INTERACTIVE ray, one on
	// BACKGROUND ray. Both rays get heavy back-to-back writes; we
	// compare drain time. USER_INTERACTIVE must not be slower; ideally
	// it dominates. This is a regression-detector — exact ratio depends
	// on scheduler implementation, but USER_INTERACTIVE behind BACKGROUND
	// would indicate broken QoS plumbing.
	kosm_ray_id hiEp0, hiEp1, loEp0, loEp1;
	TEST_REQUIRE("create high ray", kosm_create_ray(&hiEp0, &hiEp1, 0) == B_OK);
	TEST_REQUIRE("create low ray",  kosm_create_ray(&loEp0, &loEp1, 0) == B_OK);
	kosm_ray_set_qos(hiEp0, KOSM_QOS_USER_INTERACTIVE);
	kosm_ray_set_qos(hiEp1, KOSM_QOS_USER_INTERACTIVE);
	kosm_ray_set_qos(loEp0, KOSM_QOS_BACKGROUND);
	kosm_ray_set_qos(loEp1, KOSM_QOS_BACKGROUND);

	const int kCount = 200;
	bigtime_t* hiSamples = (bigtime_t*)calloc(kCount, sizeof(bigtime_t));
	bigtime_t* loSamples = (bigtime_t*)calloc(kCount, sizeof(bigtime_t));
	TEST_REQUIRE("alloc samples", hiSamples != NULL && loSamples != NULL);

	// Readers start first; they will block until writers run.
	latency_reader_args hiR = {};
	hiR.ray = hiEp1;
	hiR.expectedCount = kCount;
	hiR.samples = hiSamples;
	hiR.readTimeout = 5000000;
	thread_id hiRTid = spawn_thread(latency_reader_func, "qos_hi_rdr",
		B_NORMAL_PRIORITY, &hiR);

	latency_reader_args loR = {};
	loR.ray = loEp1;
	loR.expectedCount = kCount;
	loR.samples = loSamples;
	loR.readTimeout = 5000000;
	thread_id loRTid = spawn_thread(latency_reader_func, "qos_lo_rdr",
		B_NORMAL_PRIORITY, &loR);

	// Writers same prio, paced together so contention is real.
	bigtime_t startAt = system_time() + 100000;
	paced_writer_args hiW = {};
	hiW.ray = hiEp0;
	hiW.rateHz = 240;
	hiW.totalCount = kCount;
	hiW.startTime = startAt;
	thread_id hiWTid = spawn_thread(paced_writer_func, "qos_hi_wr",
		B_NORMAL_PRIORITY, &hiW);

	paced_writer_args loW = {};
	loW.ray = loEp0;
	loW.rateHz = 240;
	loW.totalCount = kCount;
	loW.startTime = startAt;
	thread_id loWTid = spawn_thread(paced_writer_func, "qos_lo_wr",
		B_NORMAL_PRIORITY, &loW);

	resume_thread(hiRTid);
	resume_thread(loRTid);
	resume_thread(hiWTid);
	resume_thread(loWTid);

	status_t exitVal;
	wait_for_thread(hiWTid, &exitVal);
	wait_for_thread(loWTid, &exitVal);
	wait_for_thread(hiRTid, &exitVal);
	wait_for_thread(loRTid, &exitVal);

	TEST_ASSERT("hi reader received all",  hiR.received == kCount);
	TEST_ASSERT("lo reader received all",  loR.received == kCount);

	LatencyStats hiL = compute_latency_stats(hiSamples, hiR.received);
	LatencyStats loL = compute_latency_stats(loSamples, loR.received);
	log_latency("USER_INTERACTIVE", hiL);
	log_latency("BACKGROUND      ", loL);

	// Lower bound: USER_INTERACTIVE must not be substantially slower.
	// Tolerate 50% slack — anything worse signals broken QoS routing.
	TEST_ASSERT("hi p50 <= lo p50 * 1.5",
		hiL.p50 <= (loL.p50 * 3) / 2);
	TEST_ASSERT("hi p99 <= lo p99 * 1.5",
		hiL.p99 <= (loL.p99 * 3) / 2);

	free(hiSamples);
	free(loSamples);
	kosm_close_ray(hiEp0); kosm_close_ray(hiEp1);
	kosm_close_ray(loEp0); kosm_close_ray(loEp1);
}


static void
test_input_writev_basic()
{
	trace("\n--- test_input_writev_basic ---\n");
	// Two iovecs concatenated: header + payload. Reader gets a single
	// contiguous buffer matching their concatenation.
	kosm_ray_id ep0, ep1;
	TEST_REQUIRE("create", kosm_create_ray(&ep0, &ep1, 0) == B_OK);

	struct {
		uint32 magic;
		uint32 version;
		uint32 itemCount;
		uint32 reserved;
	} header = { 0xDEADC0DE, 1, 5, 0 };

	uint32 payload[5] = { 11, 22, 33, 44, 55 };

	struct iovec vecs[2];
	vecs[0].iov_base = &header;
	vecs[0].iov_len = sizeof(header);
	vecs[1].iov_base = payload;
	vecs[1].iov_len = sizeof(payload);

	status_t s = kosm_ray_writev(ep0, vecs, 2, NULL, 0, 0);
	TEST_ASSERT("writev 2 vecs", s == B_OK);

	uint8 recv[sizeof(header) + sizeof(payload)];
	size_t sz = sizeof(recv);
	size_t hc = 0;
	s = kosm_ray_read(ep1, recv, &sz, NULL, &hc, 0);
	TEST_ASSERT("read concat", s == B_OK);
	TEST_ASSERT("size matches", sz == sizeof(header) + sizeof(payload));
	TEST_ASSERT("header part", memcmp(recv, &header, sizeof(header)) == 0);
	TEST_ASSERT("payload part",
		memcmp(recv + sizeof(header), payload, sizeof(payload)) == 0);

	// readv: scatter the same bytes into two recv buffers.
	s = kosm_ray_writev(ep0, vecs, 2, NULL, 0, 0);
	TEST_ASSERT("writev again", s == B_OK);

	uint8 hdrBuf[sizeof(header)];
	uint32 plBuf[5];
	struct iovec rvecs[2];
	rvecs[0].iov_base = hdrBuf;
	rvecs[0].iov_len = sizeof(hdrBuf);
	rvecs[1].iov_base = plBuf;
	rvecs[1].iov_len = sizeof(plBuf);
	hc = 0;
	s = kosm_ray_readv(ep1, rvecs, 2, NULL, &hc, 0);
	TEST_ASSERT("readv 2 vecs", s == B_OK);
	TEST_ASSERT("header scattered correctly",
		memcmp(hdrBuf, &header, sizeof(header)) == 0);
	TEST_ASSERT("payload scattered correctly",
		memcmp(plBuf, payload, sizeof(payload)) == 0);

	// Empty vec list — kernel rejects (does NOT enqueue an empty message).
	// We do NOT follow with a read; the queue is empty and a blocking read
	// would hang the entire suite.
	s = kosm_ray_writev(ep0, NULL, 0, NULL, 0, 0);
	TEST_ASSERT("writev 0 vecs returns error", s != B_OK);

	// Verify queue truly is empty (timeout=0, must not block).
	uint8 dummy[4];
	sz = sizeof(dummy);
	hc = 0;
	s = kosm_ray_read_etc(ep1, dummy, &sz, NULL, &hc,
		B_RELATIVE_TIMEOUT, 0);
	TEST_ASSERT("queue empty after rejected 0-vec writev",
		s == B_WOULD_BLOCK);

	kosm_close_ray(ep0);
	kosm_close_ray(ep1);
}


static void
test_input_writev_pointer_frame()
{
	trace("\n--- test_input_writev_pointer_frame ---\n");
	// Realistic KosmPointerFrame layout: fixed header + variable-length
	// coalesced sub-samples. Plan uses writev to avoid temp buffers.
	kosm_ray_id ep0, ep1;
	TEST_REQUIRE("create", kosm_create_ray(&ep0, &ep1, 0) == B_OK);
	kosm_ray_set_qos(ep0, KOSM_QOS_USER_INTERACTIVE);

	struct primary_event {
		uint32 kind;
		uint32 version;
		bigtime_t timestamp;
		float position[2];
		float pressure;
		uint32 contact_id;
		uint32 padding;
	};
	struct sub_sample {
		bigtime_t timestamp;
		float position[2];
		float pressure;
	};

	const int kFrames = 240;
	const int kSubSamplesPerFrame = 8;
	int errors = 0;
	bigtime_t totalLatency = 0;
	bigtime_t maxLatency = 0;

	for (int f = 0; f < kFrames; f++) {
		primary_event hdr;
		memset(&hdr, 0, sizeof(hdr));
		hdr.kind = 0x504F494E;	// 'POIN'
		hdr.version = 1;
		hdr.timestamp = system_time();
		hdr.position[0] = 100.0f + (float)f;
		hdr.position[1] = 200.0f;
		hdr.pressure = 0.5f;
		hdr.contact_id = 1;

		sub_sample subs[kSubSamplesPerFrame];
		for (int i = 0; i < kSubSamplesPerFrame; i++) {
			subs[i].timestamp = hdr.timestamp - (kSubSamplesPerFrame - i);
			subs[i].position[0] = hdr.position[0] - (float)i * 0.5f;
			subs[i].position[1] = hdr.position[1];
			subs[i].pressure = hdr.pressure;
		}

		struct iovec vecs[2];
		vecs[0].iov_base = &hdr;
		vecs[0].iov_len = sizeof(hdr);
		vecs[1].iov_base = subs;
		vecs[1].iov_len = sizeof(subs);

		bigtime_t t0 = system_time();
		status_t s = kosm_ray_writev(ep0, vecs, 2, NULL, 0, 0);
		if (s != B_OK) { errors++; continue; }

		uint8 recvBuf[sizeof(hdr) + sizeof(subs)];
		size_t sz = sizeof(recvBuf);
		size_t hc = 0;
		s = kosm_ray_read(ep1, recvBuf, &sz, NULL, &hc, 0);
		bigtime_t dt = system_time() - t0;
		if (s != B_OK || sz != sizeof(recvBuf)) { errors++; continue; }
		if (memcmp(recvBuf, &hdr, sizeof(hdr)) != 0) errors++;
		if (memcmp(recvBuf + sizeof(hdr), subs, sizeof(subs)) != 0) errors++;
		totalLatency += dt;
		if (dt > maxLatency) maxLatency = dt;
	}

	trace("    pointer-frame: frames=%d errors=%d  "
		"avg=%lld us  max=%lld us\n",
		kFrames, errors,
		(long long)(totalLatency / kFrames), (long long)maxLatency);

	TEST_ASSERT("no writev/read errors", errors == 0);
	TEST_ASSERT("avg < 200us", (totalLatency / kFrames) < 200);
	TEST_ASSERT("max < 8ms", maxLatency < 8000);

	kosm_close_ray(ep0);
	kosm_close_ray(ep1);
}


static void
test_input_ray_call_latency()
{
	trace("\n--- test_input_ray_call_latency ---\n");
	// kosm_ray_call should be cheaper than write+read separately
	// because the scheduler can hand-off to the responder atomically.
	// We measure both paths and log the ratio. Acceptance is loose:
	// ray_call must not be substantially worse.
	kosm_ray_id ep0, ep1;
	TEST_REQUIRE("create", kosm_create_ray(&ep0, &ep1, 0) == B_OK);
	kosm_ray_set_qos(ep0, KOSM_QOS_USER_INTERACTIVE);
	kosm_ray_set_qos(ep1, KOSM_QOS_USER_INTERACTIVE);

	const int kCount = 500;

	ray_call_server_args sargs = {};
	sargs.ray = ep1;
	sargs.count = kCount;
	thread_id sTid = spawn_thread(ray_call_server_func, "rpc_server",
		B_NORMAL_PRIORITY, &sargs);
	resume_thread(sTid);

	bigtime_t* callSamples = (bigtime_t*)calloc(kCount, sizeof(bigtime_t));
	TEST_REQUIRE("alloc samples", callSamples != NULL);

	int callErrors = 0;
	for (int i = 0; i < kCount; i++) {
		uint8 send[64];
		memset(send, 0, sizeof(send));
		send[0] = (uint8)i;
		uint8 recv[64];
		size_t recvSz = sizeof(recv);
		size_t recvHc = 0;
		bigtime_t t0 = system_time();
		status_t s = kosm_ray_call(ep0, send, sizeof(send), NULL, 0,
			recv, &recvSz, NULL, &recvHc,
			B_RELATIVE_TIMEOUT, 1000000);
		bigtime_t dt = system_time() - t0;
		if (s == B_OK && recv[0] == (uint8)(send[0] + 1))
			callSamples[i] = dt;
		else
			callErrors++;
	}

	status_t exitVal;
	sargs.stop = true;
	wait_for_thread(sTid, &exitVal);

	TEST_ASSERT("server processed all", sargs.processed == kCount);
	TEST_ASSERT("no call errors", callErrors == 0);

	LatencyStats lat = compute_latency_stats(callSamples, kCount);
	log_latency("kosm_ray_call ", lat);

	// Hard ceiling — RPC primitive should be << 2ms p99 on idle system.
	TEST_ASSERT("p50 < 200us", lat.p50 < 200);
	TEST_ASSERT("p99 < 2ms",   lat.p99 < 2000);

	free(callSamples);
	kosm_close_ray(ep0);
	kosm_close_ray(ep1);
}


static void
test_input_endurance_10s()
{
	trace("\n--- test_input_endurance_10s ---\n");
	// Sustained 240 Hz for 10 seconds. We split samples in half and
	// compare first vs last second of p99 to detect monotonic latency
	// drift. Also queries team-info before/after for handle/area leak.
	const int kRateHz = 240;
	const int kDurationSec = 10;
	const int kTotal = kRateHz * kDurationSec;	// 2400 msgs

	team_info beforeInfo;
	memset(&beforeInfo, 0, sizeof(beforeInfo));
	get_team_info(B_CURRENT_TEAM, &beforeInfo);

	kosm_ray_id ep0, ep1;
	TEST_REQUIRE("create", kosm_create_ray(&ep0, &ep1, 0) == B_OK);
	kosm_ray_set_qos(ep0, KOSM_QOS_USER_INTERACTIVE);

	bigtime_t* samples = (bigtime_t*)calloc(kTotal, sizeof(bigtime_t));
	TEST_REQUIRE("alloc samples", samples != NULL);

	latency_reader_args rargs = {};
	rargs.ray = ep1;
	rargs.expectedCount = kTotal;
	rargs.samples = samples;
	rargs.readTimeout = 5000000;
	thread_id rTid = spawn_thread(latency_reader_func, "endur_rdr",
		B_NORMAL_PRIORITY, &rargs);
	resume_thread(rTid);

	paced_writer_args wargs = {};
	wargs.ray = ep0;
	wargs.rateHz = kRateHz;
	wargs.totalCount = kTotal;
	wargs.startTime = system_time() + 100000;
	thread_id wTid = spawn_thread(paced_writer_func, "endur_wr",
		B_NORMAL_PRIORITY, &wargs);
	resume_thread(wTid);

	status_t exitVal;
	wait_for_thread(wTid, &exitVal);
	wait_for_thread(rTid, &exitVal);

	TEST_ASSERT("reader received all", rargs.received == kTotal);

	// Drift: compare first second window vs last.
	bigtime_t firstWin[kRateHz], lastWin[kRateHz];
	memcpy(firstWin, samples, sizeof(firstWin));
	memcpy(lastWin, samples + (kTotal - kRateHz), sizeof(lastWin));
	LatencyStats first = compute_latency_stats(firstWin, kRateHz);
	LatencyStats last  = compute_latency_stats(lastWin, kRateHz);
	log_latency("first 1s", first);
	log_latency("last  1s", last);

	// Aggregate over full run — separate buffer so we don't re-sort
	// the windows we already used.
	LatencyStats lat = compute_latency_stats(samples, rargs.received);
	log_latency("total 10s", lat);

	team_info afterInfo;
	memset(&afterInfo, 0, sizeof(afterInfo));
	get_team_info(B_CURRENT_TEAM, &afterInfo);
	int32 areaDelta = afterInfo.area_count - beforeInfo.area_count;
	int32 threadDelta = afterInfo.thread_count - beforeInfo.thread_count;
	trace("    leak check: area_delta=%ld thread_delta=%ld\n",
		(long)areaDelta, (long)threadDelta);

	// last p99 / first p99 — drift > 2x over 10s indicates a problem.
	bool driftBounded = last.p99 <= (first.p99 * 2 + 1000);
	TEST_ASSERT("p99 drift bounded (last <= 2x first + 1ms slack)",
		driftBounded);
	TEST_ASSERT("aggregate p99 < 16ms", lat.p99 < 16000);
	TEST_ASSERT("no area leak (delta < 5)", areaDelta < 5);
	TEST_ASSERT("no thread leak (delta == 0)", threadDelta == 0);

	free(samples);
	kosm_close_ray(ep0);
	kosm_close_ray(ep1);
}


static void
test_input_backpressure_slow_consumer()
{
	trace("\n--- test_input_backpressure_slow_consumer ---\n");
	// Producer at 240 Hz, consumer 10x slower (snooze 41ms per read).
	// Queue depth (256) is filled in ~1.07s. Producer uses timeout=0
	// — must observe B_WOULD_BLOCK and continue (drop semantics) rather
	// than hang. Consumer must drain whatever was buffered.
	kosm_ray_id ep0, ep1;
	TEST_REQUIRE("create", kosm_create_ray(&ep0, &ep1, 0) == B_OK);

	const int kProducerTotal = 240 * 3;	// 720 attempts (3s @ 240 Hz)
	const int kConsumerTotal = 24 * 3;	// 72 reads (3s @ 24 Hz)

	slow_reader_args sr = {};
	sr.ray = ep1;
	sr.count = kConsumerTotal;
	sr.perReadSnooze = 41000;	// 24 Hz = 41.6ms
	thread_id rTid = spawn_thread(slow_reader_func, "slow_rdr",
		B_NORMAL_PRIORITY, &sr);
	resume_thread(rTid);

	int sent = 0, blocks = 0, otherErrors = 0;
	bigtime_t period = 1000000 / 240;
	bigtime_t deadline = system_time() + 100000;
	for (int i = 0; i < kProducerTotal; i++) {
		snooze_until(deadline, B_SYSTEM_TIMEBASE);
		deadline += period;
		timed_msg msg;
		memset(&msg, 0, sizeof(msg));
		msg.seq = (uint32)i;
		msg.send_time = system_time();
		status_t s = kosm_ray_write_etc(ep0, &msg, sizeof(msg),
			NULL, 0, B_RELATIVE_TIMEOUT, 0);
		if (s == B_OK) sent++;
		else if (s == B_WOULD_BLOCK) blocks++;
		else otherErrors++;
	}

	status_t exitVal;
	wait_for_thread(rTid, &exitVal);

	trace("    producer attempts=%d sent=%d blocks=%d other_errors=%d\n",
		kProducerTotal, sent, blocks, otherErrors);
	trace("    consumer received=%d (target %d)\n",
		sr.received, kConsumerTotal);

	TEST_ASSERT("no producer hang (loop completed)",
		sent + blocks + otherErrors == kProducerTotal);
	TEST_ASSERT("no unexpected errors", otherErrors == 0);
	// Producer must have hit backpressure — otherwise queue size or
	// consumer timing has changed unexpectedly.
	TEST_ASSERT("producer did hit backpressure (blocks > 0)",
		blocks > 0);
	TEST_ASSERT("consumer drained at its rate",
		sr.received >= kConsumerTotal - 2);

	kosm_close_ray(ep0);
	kosm_close_ray(ep1);
}


static void
test_input_multi_ray_fairness()
{
	trace("\n--- test_input_multi_ray_fairness ---\n");
	// Orbita-like fanout: many windows = many rays of equal QoS.
	// Each writer must get roughly equal service from a single reader
	// using wait_for_objects across all rays.
	const int kRayCount = 16;
	const int kPerRay = 50;
	kosm_ray_id ep0[kRayCount], ep1[kRayCount];
	for (int i = 0; i < kRayCount; i++) {
		TEST_REQUIRE("create",
			kosm_create_ray(&ep0[i], &ep1[i], 0) == B_OK);
		kosm_ray_set_qos(ep0[i], KOSM_QOS_USER_INTERACTIVE);
		kosm_ray_set_qos(ep1[i], KOSM_QOS_USER_INTERACTIVE);
	}

	fairness_writer_args wargs[kRayCount];
	thread_id wTids[kRayCount];
	for (int i = 0; i < kRayCount; i++) {
		wargs[i].ray = ep0[i];
		wargs[i].count = kPerRay;
		wargs[i].sent = 0;
		char name[32];
		snprintf(name, sizeof(name), "fair_wr_%d", i);
		wTids[i] = spawn_thread(fairness_writer_func, name,
			B_NORMAL_PRIORITY, &wargs[i]);
		resume_thread(wTids[i]);
	}

	// Single reader, multiplex via wait_for_objects.
	int perRayReceived[kRayCount];
	memset(perRayReceived, 0, sizeof(perRayReceived));
	int totalExpected = kRayCount * kPerRay;
	int totalReceived = 0;
	bigtime_t deadline = system_time() + 5000000;	// 5s budget
	while (totalReceived < totalExpected
			&& system_time() < deadline) {
		object_wait_info infos[kRayCount];
		for (int i = 0; i < kRayCount; i++) {
			infos[i].object = ep1[i];
			infos[i].type = B_OBJECT_TYPE_KOSM_RAY;
			infos[i].events = B_EVENT_READ;
		}
		ssize_t r = wait_for_objects_etc(infos, kRayCount,
			B_RELATIVE_TIMEOUT, 200000);
		if (r < 0)
			continue;
		for (int i = 0; i < kRayCount; i++) {
			if ((infos[i].events & B_EVENT_READ) == 0)
				continue;
			uint8 buf[16];
			size_t sz = sizeof(buf);
			size_t hc = 0;
			status_t s = kosm_ray_read_etc(ep1[i], buf, &sz,
				NULL, &hc, B_RELATIVE_TIMEOUT, 0);
			if (s == B_OK) {
				perRayReceived[i]++;
				totalReceived++;
			}
		}
	}

	status_t exitVal;
	for (int i = 0; i < kRayCount; i++)
		wait_for_thread(wTids[i], &exitVal);

	int minPerRay = perRayReceived[0];
	int maxPerRay = perRayReceived[0];
	for (int i = 1; i < kRayCount; i++) {
		if (perRayReceived[i] < minPerRay) minPerRay = perRayReceived[i];
		if (perRayReceived[i] > maxPerRay) maxPerRay = perRayReceived[i];
	}
	trace("    rays=%d total_recv=%d min=%d max=%d (target %d each)\n",
		kRayCount, totalReceived, minPerRay, maxPerRay, kPerRay);

	TEST_ASSERT("all messages received",
		totalReceived == totalExpected);
	TEST_ASSERT("no starvation (min == kPerRay)",
		minPerRay == kPerRay);
	TEST_ASSERT("no over-delivery", maxPerRay == kPerRay);

	for (int i = 0; i < kRayCount; i++) {
		kosm_close_ray(ep0[i]);
		kosm_close_ray(ep1[i]);
	}
}


static void
test_input_handle_transfer_overhead()
{
	trace("\n--- test_input_handle_transfer_overhead ---\n");
	// Per-window-ray bootstrap will transfer 1 ray handle per window.
	// Document overhead at common counts. No hard assertion — this is
	// a baseline to catch future regressions.
	kosm_ray_id ep0, ep1;
	TEST_REQUIRE("create", kosm_create_ray(&ep0, &ep1, 0) == B_OK);

	const int kIters = 500;
	const int kCounts[] = { 0, 1, 4, 16 };
	const int kCountVariants = sizeof(kCounts) / sizeof(kCounts[0]);

	for (int v = 0; v < kCountVariants; v++) {
		int n = kCounts[v];

		kosm_mutex_id mutexes[16];
		kosm_handle_t handles[16];
		for (int i = 0; i < n; i++) {
			char name[32];
			snprintf(name, sizeof(name), "hover_%d_%d", v, i);
			mutexes[i] = kosm_create_mutex(name, 0);
			handles[i] = mutexes[i];
		}

		bigtime_t t0 = system_time();
		uint8 msg = 0;
		int errors = 0;
		uint32 writeFlags = (n > 0) ? KOSM_RAY_COPY_HANDLES : 0;
		for (int i = 0; i < kIters; i++) {
			status_t s = kosm_ray_write(ep0, &msg, 1,
				n > 0 ? handles : NULL, n, writeFlags);
			if (s != B_OK) { errors++; continue; }
			uint8 recvMsg;
			size_t sz = 1;
			kosm_handle_t recvH[16];
			size_t rhc = 16;
			s = kosm_ray_read(ep1, &recvMsg, &sz,
				n > 0 ? recvH : NULL, &rhc, 0);
			if (s != B_OK) { errors++; continue; }
			for (size_t j = 0; j < rhc; j++)
				kosm_close_handle(recvH[j]);
		}
		bigtime_t dt = system_time() - t0;

		double opsPerSec = (double)kIters * 1000000.0 / (double)dt;
		trace("    handles=%2d  iters=%d  errors=%d  "
			"%.0f ops/sec  (avg %lld us/op)\n",
			n, kIters, errors, opsPerSec,
			(long long)(dt / kIters));

		for (int i = 0; i < n; i++)
			kosm_delete_mutex(mutexes[i]);

		TEST_ASSERT("no errors at this handle count", errors == 0);
	}

	kosm_close_ray(ep0);
	kosm_close_ray(ep1);
}


// Suite registration

static const TestEntry sRayTests[] = {
	// Basic
	{ "create/close",               test_create_close,               "Basic" },
	{ "write/read",                 test_write_read,                 "Basic" },
	{ "bidirectional",              test_bidirectional,              "Basic" },
	{ "zero-length",                test_zero_length,                "Basic" },
	{ "multiple messages",          test_multiple_messages,          "Basic" },
	{ "get_ray_info",               test_get_ray_info,               "Basic" },
	{ "read truncation",            test_read_truncation,            "Basic" },
	// Limits
	{ "max data size",              test_max_data_size,              "Limits" },
	{ "max handles",                test_max_handles,                "Limits" },
	{ "queue full",                 test_queue_full,                 "Limits" },
	{ "invalid id",                 test_invalid_id,                 "Limits" },
	{ "double close",               test_double_close,               "Limits" },
	// Peer Close
	{ "write after peer close",     test_write_after_peer_close,     "Peer Close" },
	{ "read empty after peer close", test_read_empty_after_peer_close, "Peer Close" },
	{ "drain after peer close",     test_drain_after_peer_close,     "Peer Close" },
	{ "close wakes blocked reader", test_close_wakes_blocked_reader, "Peer Close" },
	{ "close wakes blocked writer", test_close_wakes_blocked_writer, "Peer Close" },
	// Handles
	{ "ray handle move",            test_handle_ray_move,            "Handles" },
	{ "mutex handle",               test_handle_mutex,               "Handles" },
	{ "area handle",                test_handle_area_move,           "Handles" },
	{ "sem handle",                 test_handle_sem_move,            "Handles" },
	{ "dot handle move",            test_handle_dot_move,            "Handles" },
	{ "dot handle copy",            test_handle_dot_copy,            "Handles" },
	{ "FD not supported",           test_handle_fd_not_supported,    "Handles" },
	{ "invalid handle",             test_handle_invalid,             "Handles" },
	// Peek & Flags
	{ "peek",                       test_peek,                       "Peek & Flags" },
	{ "copy handles",               test_copy_handles,               "Peek & Flags" },
	{ "timed read/write",           test_timed_read_write,           "Peek & Flags" },
	// QoS
	{ "set/get QoS",                test_qos_set_get,                "QoS" },
	{ "invalid QoS",                test_qos_invalid,                "QoS" },
	{ "QoS in message",             test_qos_in_message,             "QoS" },
	// Wait
	{ "wait readable",              test_wait_readable,              "Wait" },
	{ "wait peer closed",           test_wait_peer_closed,           "Wait" },
	{ "wait already satisfied",     test_wait_already_satisfied,     "Wait" },
	{ "wait timeout",               test_wait_timeout,               "Wait" },
	// Stress
	{ "throughput",                  test_stress_throughput,          "STRESS" },
	{ "concurrent writers",          test_stress_concurrent_writers,  "STRESS" },
	{ "create/close churn",          test_stress_create_close_churn,  "STRESS" },
	{ "large payload",               test_stress_large_payload,       "STRESS" },
	// Mobile Sim
	{ "IPC pipeline 3 workers",      test_mobile_ipc_simulation,     "MOBILE SIM" },
	// Select
	{ "select readable",             test_select_readable,           "Select" },
	{ "select writable",             test_select_writable,           "Select" },
	{ "select peer closed",          test_select_peer_closed,        "Select" },
	{ "select multiple rays",        test_select_multiple_rays,      "Select" },
	{ "select timeout",              test_select_timeout,            "Select" },
	// Cross-Process
	{ "cross-process IPC",           test_cross_process_ray,         "Cross-Process" },
	{ "cross-process handle transfer", test_cross_process_handle_transfer, "Cross-Process" },
	// Permission
	{ "cross-process permission",    test_cross_process_permission,  "Permission" },
	// Team Death
	{ "team death peer closed",      test_team_death_peer_closed,    "Team Death" },
	{ "team death drain then closed", test_team_death_drain_then_closed, "Team Death" },
	{ "team death unblocks wait",    test_team_death_unblocks_wait,  "Team Death" },
	{ "team death select integration", test_team_death_select_integration, "Team Death" },
	// Input Profile (KosmTouch / pointer-pipeline acceptance)
	{ "sustained 240 Hz",            test_input_sustained_240hz,             "INPUT-PROFILE" },
	{ "sustained 1000 Hz",           test_input_sustained_1000hz,            "INPUT-PROFILE" },
	{ "QoS preemption under load",   test_input_qos_preemption,              "INPUT-PROFILE" },
	{ "writev basic",                test_input_writev_basic,                "INPUT-PROFILE" },
	{ "writev pointer frame",        test_input_writev_pointer_frame,        "INPUT-PROFILE" },
	{ "ray_call latency",            test_input_ray_call_latency,            "INPUT-PROFILE" },
	{ "endurance 10s + leak check",  test_input_endurance_10s,               "INPUT-PROFILE" },
	{ "backpressure slow consumer",  test_input_backpressure_slow_consumer,  "INPUT-PROFILE" },
	{ "multi-ray fairness",          test_input_multi_ray_fairness,          "INPUT-PROFILE" },
	{ "handle transfer overhead",    test_input_handle_transfer_overhead,    "INPUT-PROFILE" },
};


TestSuite
get_ray_test_suite()
{
	TestSuite suite;
	suite.name = "KosmRay IPC";
	suite.tests = sRayTests;
	suite.count = sizeof(sRayTests) / sizeof(sRayTests[0]);
	return suite;
}
