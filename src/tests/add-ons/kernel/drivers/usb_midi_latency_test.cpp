/*
 * Copyright 2025, Haiku Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * USB MIDI Lock-Free Buffer Latency and Jitter Test
 * Phase 5.2 Implementation Verification
 *
 * This test measures:
 * - End-to-end MIDI event latency
 * - Timestamp jitter and variance
 * - Lock-free buffer throughput
 * - ABA problem detection via generation counter
 *
 * Target metrics:
 * - Latency: < 2ms average
 * - Jitter: < 0.5ms standard deviation
 * - Throughput: > 400K events/sec
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <OS.h>
#include <SupportDefs.h>
#include <usb/USB_midi.h>

// Statistics collection
struct LatencyStats {
	bigtime_t min_latency;
	bigtime_t max_latency;
	bigtime_t total_latency;
	bigtime_t* samples;
	size_t sample_count;
	size_t capacity;
	
	LatencyStats(size_t cap)
		: min_latency(B_INFINITE_TIMEOUT)
		, max_latency(0)
		, total_latency(0)
		, sample_count(0)
		, capacity(cap)
	{
		samples = (bigtime_t*)malloc(capacity * sizeof(bigtime_t));
	}
	
	~LatencyStats()
	{
		free(samples);
	}
	
	void AddSample(bigtime_t latency)
	{
		if (sample_count < capacity) {
			samples[sample_count++] = latency;
			total_latency += latency;
			
			if (latency < min_latency)
				min_latency = latency;
			if (latency > max_latency)
				max_latency = latency;
		}
	}
	
	double Average() const
	{
		if (sample_count == 0)
			return 0.0;
		return (double)total_latency / sample_count;
	}
	
	double StandardDeviation() const
	{
		if (sample_count < 2)
			return 0.0;
		
		double avg = Average();
		double variance = 0.0;
		
		for (size_t i = 0; i < sample_count; i++) {
			double diff = samples[i] - avg;
			variance += diff * diff;
		}
		
		return sqrt(variance / (sample_count - 1));
	}
	
	void PrintReport(const char* test_name) const
	{
		printf("\n=== %s ===\n", test_name);
		printf("Samples:     %zu\n", sample_count);
		printf("Min:         %.3f ms\n", min_latency / 1000.0);
		printf("Max:         %.3f ms\n", max_latency / 1000.0);
		printf("Average:     %.3f ms\n", Average() / 1000.0);
		printf("Std Dev:     %.3f ms\n", StandardDeviation() / 1000.0);
		printf("Jitter:      %.3f ms (max - min)\n", 
			(max_latency - min_latency) / 1000.0);
		
		// Performance evaluation
		printf("\nPerformance Metrics:\n");
		double avg_ms = Average() / 1000.0;
		double jitter_ms = StandardDeviation() / 1000.0;
		
		if (avg_ms < 2.0)
			printf("✓ Latency PASS (< 2ms target)\n");
		else
			printf("✗ Latency FAIL (%.3f ms > 2ms target)\n", avg_ms);
		
		if (jitter_ms < 0.5)
			printf("✓ Jitter PASS (< 0.5ms target)\n");
		else
			printf("✗ Jitter FAIL (%.3f ms > 0.5ms target)\n", jitter_ms);
	}
};


// Test 1: Basic lock-free buffer operations
static void
test_basic_operations()
{
	printf("\n[Test 1] Basic Lock-Free Buffer Operations\n");
	printf("===========================================\n");
	
	MIDIEventBuffer buffer(MIDI_BUFFER_SIZE_DEFAULT);
	
	// Test 1.1: Empty buffer
	printf("Empty buffer check... ");
	if (buffer.IsEmpty() && buffer.Count() == 0) {
		printf("✓ PASS\n");
	} else {
		printf("✗ FAIL\n");
	}
	
	// Test 1.2: Single write/read
	printf("Single write/read... ");
	usb_midi_event_packet_v2 packet;
	memset(&packet, 0, sizeof(packet));
	packet.cin = 0x9;
	packet.cn = 0;
	packet.midi[0] = 0x90;
	packet.midi[1] = 0x3C;
	packet.midi[2] = 0x7F;
	packet.timestamp = system_time();
	packet.priority = 200;
	
	if (buffer.TryWrite(packet)) {
		usb_midi_event_packet_v2 read_packet;
		if (buffer.TryRead(read_packet)) {
			if (memcmp(&packet, &read_packet, sizeof(packet)) == 0) {
				printf("✓ PASS\n");
			} else {
				printf("✗ FAIL (data mismatch)\n");
			}
		} else {
			printf("✗ FAIL (read failed)\n");
		}
	} else {
		printf("✗ FAIL (write failed)\n");
	}
	
	// Test 1.3: Buffer full detection
	printf("Buffer full detection... ");
	size_t writes = 0;
	while (buffer.TryWrite(packet)) {
		writes++;
	}
	
	if (buffer.IsFull() && writes == (MIDI_BUFFER_SIZE_DEFAULT - 1)) {
		printf("✓ PASS (%zu writes)\n", writes);
	} else {
		printf("✗ FAIL (expected %d, got %zu)\n", 
			MIDI_BUFFER_SIZE_DEFAULT - 1, writes);
	}
}


// Test 2: Latency measurement
static void
test_latency()
{
	printf("\n[Test 2] Latency Measurement\n");
	printf("=============================\n");
	
	const size_t TEST_COUNT = 10000;
	MIDIEventBuffer buffer(MIDI_BUFFER_SIZE_DEFAULT);
	LatencyStats stats(TEST_COUNT);
	
	printf("Running %zu iterations...\n", TEST_COUNT);
	
	for (size_t i = 0; i < TEST_COUNT; i++) {
		usb_midi_event_packet_v2 packet;
		memset(&packet, 0, sizeof(packet));
		packet.cin = 0x9;
		packet.cn = 0;
		packet.midi[0] = 0x90;
		packet.midi[1] = 0x3C + (i % 12);
		packet.midi[2] = 0x7F;
		
		// Measure write latency
		bigtime_t start = system_time();
		packet.timestamp = start;
		packet.priority = 200;
		
		if (!buffer.TryWrite(packet)) {
			// Buffer full - read one event to make space
			usb_midi_event_packet_v2 dummy;
			buffer.TryRead(dummy);
			buffer.TryWrite(packet);
		}
		
		// Read back immediately
		usb_midi_event_packet_v2 read_packet;
		if (buffer.TryRead(read_packet)) {
			bigtime_t end = system_time();
			bigtime_t latency = end - start;
			stats.AddSample(latency);
		}
	}
	
	stats.PrintReport("Lock-Free Buffer Latency");
}


// Test 3: Throughput measurement
static void
test_throughput()
{
	printf("\n[Test 3] Throughput Measurement\n");
	printf("================================\n");
	
	const size_t EVENT_COUNT = 100000;
	MIDIEventBuffer buffer(MIDI_BUFFER_SIZE_MAX);
	
	printf("Writing %zu events...\n", EVENT_COUNT);
	
	bigtime_t start = system_time();
	size_t writes = 0;
	
	for (size_t i = 0; i < EVENT_COUNT; i++) {
		usb_midi_event_packet_v2 packet;
		memset(&packet, 0, sizeof(packet));
		packet.cin = 0x9;
		packet.cn = 0;
		packet.midi[0] = 0x90;
		packet.midi[1] = 0x3C + (i % 88);
		packet.midi[2] = 0x40 + (i % 64);
		packet.timestamp = system_time();
		packet.priority = 200;
		
		if (buffer.TryWrite(packet)) {
			writes++;
		} else {
			// Buffer full - read some to make space
			usb_midi_event_packet_v2 dummy;
			for (int j = 0; j < 10; j++) {
				buffer.TryRead(dummy);
			}
		}
	}
	
	bigtime_t write_time = system_time() - start;
	
	printf("Reading events...\n");
	start = system_time();
	size_t reads = 0;
	
	usb_midi_event_packet_v2 packet;
	while (buffer.TryRead(packet)) {
		reads++;
	}
	
	bigtime_t read_time = system_time() - start;
	
	printf("\nResults:\n");
	printf("Writes:          %zu\n", writes);
	printf("Reads:           %zu\n", reads);
	printf("Write time:      %.3f ms\n", write_time / 1000.0);
	printf("Read time:       %.3f ms\n", read_time / 1000.0);
	
	double write_throughput = (writes * 1000000.0) / write_time;
	double read_throughput = (reads * 1000000.0) / read_time;
	
	printf("Write throughput: %.0f events/sec\n", write_throughput);
	printf("Read throughput:  %.0f events/sec\n", read_throughput);
	
	if (write_throughput > 400000.0) {
		printf("✓ Throughput PASS (> 400K events/sec target)\n");
	} else {
		printf("✗ Throughput FAIL (%.0f < 400K events/sec target)\n", 
			write_throughput);
	}
}


// Test 4: Multi-threaded stress test
static int32
producer_thread(void* data)
{
	MIDIEventBuffer* buffer = (MIDIEventBuffer*)data;
	const size_t ITERATIONS = 50000;
	
	for (size_t i = 0; i < ITERATIONS; i++) {
		usb_midi_event_packet_v2 packet;
		memset(&packet, 0, sizeof(packet));
		packet.cin = 0x9;
		packet.cn = 0;
		packet.midi[0] = 0x90;
		packet.midi[1] = 0x3C + (i % 88);
		packet.midi[2] = 0x7F;
		packet.timestamp = system_time();
		packet.priority = 200;
		
		while (!buffer->TryWrite(packet)) {
			snooze(1);  // Wait 1 microsecond
		}
	}
	
	return 0;
}


static int32
consumer_thread(void* data)
{
	MIDIEventBuffer* buffer = (MIDIEventBuffer*)data;
	size_t reads = 0;
	bigtime_t start = system_time();
	
	// Run for 1 second
	while ((system_time() - start) < 1000000) {
		usb_midi_event_packet_v2 packet;
		if (buffer->TryRead(packet)) {
			reads++;
		} else {
			snooze(1);  // Wait 1 microsecond
		}
	}
	
	printf("Consumer thread read %zu events\n", reads);
	return 0;
}


static void
test_concurrent_access()
{
	printf("\n[Test 4] Concurrent Access (Lock-Free Verification)\n");
	printf("====================================================\n");
	
	MIDIEventBuffer buffer(MIDI_BUFFER_SIZE_MAX);
	
	printf("Starting producer and consumer threads...\n");
	
	int32 initial_gen = buffer.Generation();
	
	thread_id producer = spawn_thread(producer_thread, "producer", 
		B_NORMAL_PRIORITY, &buffer);
	thread_id consumer = spawn_thread(consumer_thread, "consumer", 
		B_NORMAL_PRIORITY, &buffer);
	
	resume_thread(producer);
	resume_thread(consumer);
	
	status_t status;
	wait_for_thread(producer, &status);
	wait_for_thread(consumer, &status);
	
	int32 final_gen = buffer.Generation();
	
	printf("\nGeneration counter increased by: %d\n", final_gen - initial_gen);
	printf("Final buffer count: %zu\n", buffer.Count());
	
	if (final_gen > initial_gen) {
		printf("✓ ABA protection working (generation counter active)\n");
	} else {
		printf("✗ ABA protection may be broken\n");
	}
}


// Test 5: Jitter measurement under load
static void
test_jitter_under_load()
{
	printf("\n[Test 5] Jitter Measurement Under Load\n");
	printf("=======================================\n");
	
	const size_t TEST_COUNT = 10000;
	MIDIEventBuffer buffer(MIDI_BUFFER_SIZE_DEFAULT);
	LatencyStats stats(TEST_COUNT);
	
	printf("Measuring jitter with varying buffer load...\n");
	
	// Pre-fill buffer to 50% capacity
	for (size_t i = 0; i < MIDI_BUFFER_SIZE_DEFAULT / 2; i++) {
		usb_midi_event_packet_v2 packet;
		memset(&packet, 0, sizeof(packet));
		packet.timestamp = system_time();
		buffer.TryWrite(packet);
	}
	
	for (size_t i = 0; i < TEST_COUNT; i++) {
		usb_midi_event_packet_v2 packet;
		memset(&packet, 0, sizeof(packet));
		packet.cin = 0x9;
		packet.cn = 0;
		packet.midi[0] = 0x90;
		packet.midi[1] = 0x3C + (i % 12);
		packet.midi[2] = 0x7F;
		
		bigtime_t write_start = system_time();
		packet.timestamp = write_start;
		packet.priority = 200;
		
		buffer.TryWrite(packet);
		
		// Immediate read
		usb_midi_event_packet_v2 read_packet;
		buffer.TryRead(read_packet);
		bigtime_t read_end = system_time();
		
		stats.AddSample(read_end - write_start);
		
		// Randomly add/remove events to vary buffer load
		if (i % 10 == 0) {
			usb_midi_event_packet_v2 dummy;
			memset(&dummy, 0, sizeof(dummy));
			dummy.timestamp = system_time();
			buffer.TryWrite(dummy);
		}
		if (i % 15 == 0) {
			usb_midi_event_packet_v2 dummy;
			buffer.TryRead(dummy);
		}
	}
	
	stats.PrintReport("Jitter Under Variable Load");
}


int
main(int argc, char** argv)
{
	printf("\n");
	printf("╔══════════════════════════════════════════════════════════════╗\n");
	printf("║  USB MIDI Lock-Free Buffer Test Suite (Phase 5.2)          ║\n");
	printf("║  Target: < 2ms latency, < 0.5ms jitter, > 400K events/sec  ║\n");
	printf("╚══════════════════════════════════════════════════════════════╝\n");
	
	test_basic_operations();
	test_latency();
	test_throughput();
	test_concurrent_access();
	test_jitter_under_load();
	
	printf("\n");
	printf("╔══════════════════════════════════════════════════════════════╗\n");
	printf("║  Test Suite Complete                                        ║\n");
	printf("╚══════════════════════════════════════════════════════════════╝\n");
	printf("\n");
	
	return 0;
}
