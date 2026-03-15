/*
 * Copyright 2025 KosmOS Project. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * KosmRay IPC test functions for the unified test suite.
 */


#include "TestCommon.h"

#include <KosmOS.h>

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
	s = kosm_ray_write(ep0, &extra, 1, NULL, 0, 0);
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
	kosm_handle_t recvH;
	size_t rhc = 1;
	s = kosm_ray_read(ep1, &recvMsg, &sz, &recvH, &rhc, 0);
	TEST_ASSERT("read succeeds", s == B_OK);
	TEST_ASSERT("got 1 handle", rhc == 1);

	kosm_handle_info hinfo;
	TEST_ASSERT("get handle info", kosm_get_handle_info(recvH, &hinfo) == B_OK);
	TEST_ASSERT("handle type AREA", hinfo.type == KOSM_HANDLE_AREA);

	delete_area(aid);
	// recvH is a cloned area handle — close it to clean up
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
	kosm_handle_t recvH;
	size_t rhc = 1;
	s = kosm_ray_read(ep1, &recvMsg, &sz, &recvH, &rhc, 0);
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
	s = kosm_ray_read(ep1, &empty, &sz, NULL, &hc, 0);
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
		KOSM_QOS_DEFAULT, KOSM_QOS_UTILITY,
		KOSM_QOS_USER_INITIATED, KOSM_QOS_USER_INTERACTIVE
	};

	bool allOK = true;
	for (int i = 0; i < 4; i++) {
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

	TEST_ASSERT("qos 4 fails", kosm_ray_set_qos(ep0, 4) == B_BAD_VALUE);
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

static void
test_stress_throughput()
{
	trace("\n--- test_stress_throughput ---\n");
	kosm_ray_id ep0, ep1;
	kosm_create_ray(&ep0, &ep1, 0);

	const int kCycles = 10000;
	uint8 sendBuf[64];
	uint8 recvBuf[64];
	memset(sendBuf, 0xAA, sizeof(sendBuf));

	int errors = 0;
	bigtime_t start = system_time();
	for (int i = 0; i < kCycles; i++) {
		sendBuf[0] = (uint8)(i & 0xFF);
		status_t s = kosm_ray_write(ep0, sendBuf, sizeof(sendBuf),
			NULL, 0, 0);
		if (s != B_OK) { errors++; continue; }

		size_t sz = sizeof(recvBuf);
		size_t hc = 0;
		s = kosm_ray_read(ep1, recvBuf, &sz, NULL, &hc, 0);
		if (s != B_OK) errors++;
	}
	bigtime_t elapsed = system_time() - start;
	double opsPerSec = (double)kCycles * 1000000.0 / (double)elapsed;

	trace("    throughput: %.0f round-trips/sec\n", opsPerSec);
	TEST_ASSERT("no errors", errors == 0);
	TEST_ASSERT("throughput > 10000 rt/s", opsPerSec > 10000);

	kosm_close_ray(ep0);
	kosm_close_ray(ep1);
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
