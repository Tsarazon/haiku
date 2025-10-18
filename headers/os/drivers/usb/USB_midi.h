#ifndef USB_MIDI_H
#define USB_MIDI_H

#include <stdlib.h>
#include <usb/USB_audio.h>

// (Partial) USB Class Definitions for MIDI Devices, version 1.0
// Reference: http://www.usb.org/developers/devclass_docs/midi10.pdf

#define USB_MIDI_CLASS_VERSION		0x0100	// Class specification version 1.0

// USB MIDI Event Packet

// ... as clean structure:
typedef struct {	// USB MIDI Event Packet
	uint8	cin:4;	// Code Index Number
	uint8	cn:4;	// Cable Number
	uint8	midi[3];
} _PACKED usb_midi_event_packet;

// Phase 5.2: Extended MIDI packet with timestamp and priority (v2)
typedef struct {
	uint8		cin:4;		// Code Index Number
	uint8		cn:4;		// Cable Number  
	uint8		midi[3];	// MIDI data bytes
	
	// V2 extensions for low-latency performance
	bigtime_t	timestamp;	// Precise event timestamp (microseconds)
	uint8		priority;	// Message priority (0-255, higher = more urgent)
	uint8		reserved[3];// Padding for alignment
} _PACKED usb_midi_event_packet_v2;

// Adaptive buffer sizing for different usage scenarios
#define MIDI_BUFFER_SIZE_MIN		64		// Casual use (14KB memory)
#define MIDI_BUFFER_SIZE_DEFAULT	256		// Typical usage (6KB memory)
#define MIDI_BUFFER_SIZE_MAX		1024	// Professional (24KB memory)


// MIDIStreaming (ms) interface descriptors (p20)

enum { // MIDI Streaming descriptors subtypes
	USB_MS_HEADER_DESCRIPTOR = 0x01,
	USB_MS_MIDI_IN_JACK_DESCRIPTOR,
	USB_MS_MIDI_OUT_JACK_DESCRIPTOR,
	USB_MS_ELEMENT_DESCRIPTOR
};

typedef struct usb_midi_interface_header_descriptor {
	uint8	length;
	uint8	descriptor_type;
	uint8	descriptor_subtype;	// USB_MS_HEADER_DESCRIPTOR
	uint16	ms_version;
	uint16	total_length;
} _PACKED usb_midi_interface_header_descriptor;


enum {
	USB_MIDI_EMBEDDED_JACK = 0x01,
	USB_MIDI_EXTERNAL_JACK
};

typedef struct usb_midi_in_jack_descriptor {
	uint8	length;
	uint8	descriptor_type;
	uint8	descriptor_subtype;	// USB_MS_MIDI_IN_JACK_DESCRIPTOR
	uint8	type;	// USB_MIDI_{EMBEDDED | EXTERNAL}_JACK
	uint8	id;
	uint8	string_descriptor;
} _PACKED usb_ms_midi_in_jack_descriptor;


typedef struct usb_midi_source {
	uint8	source_id;
	uint8	source_pin;
} _PACKED usb_midi_source;

typedef struct usb_midi_out_jack_descriptor {
	uint8	length;
	uint8	descriptor_type;
	uint8	descriptor_subtype;	// USB_MS_MIDI_OUT_JACK_DESCRIPTOR
	uint8	type;	// USB_MIDI_{EMBEDDED | EXTERNAL}_JACK
	uint8	id;
	uint8	inputs_count;
	usb_midi_source input_source[0];	// inputs_count times
	// uint8	string_descriptor;
} _PACKED usb_midi_out_jack_descriptor;


enum { // USB Element Capabilities bitmap (p23,25)
	USB_MS_ELEMENT_CUSTOM_UNDEFINED		= 0x0001,
	USB_MS_ELEMENT_MIDI_CLOCK			= 0x0002,
	USB_MS_ELEMENT_MIDI_TIME_CODE		= 0x0004,
	USB_MS_ELEMENT_MTC	= USB_MS_ELEMENT_MIDI_TIME_CODE,
	USB_MS_ELEMENT_MIDI_MACHINE_CONTROL	= 0x0008,
	USB_MS_ELEMENT_MMC	= USB_MS_ELEMENT_MIDI_MACHINE_CONTROL,
	// General MIDI System Level 1 compatible
	USB_MS_ELEMENT_GM1					= 0x0010,
	// General MIDI System Level 2 compatible
	USB_MS_ELEMENT_GM2					= 0x0020,
	// GS Format compatible (Roland)
	USB_MS_ELEMENT_GS					= 0x0040,
	// XG compatible (Yamaha)
	USB_MS_ELEMENT_XG					= 0x0080,
	USB_MS_ELEMENT_EFX					= 0x0100,
	// internal MIDI Patcher or Router
	USB_MS_ELEMENT_MIDI_PATCH_BAY		= 0x0200,
	// Downloadable Sounds Standards Level 1 compatible
	USB_MS_ELEMENT_DLS1					= 0x0400,
	// Downloadable Sounds Standards Level 2 compatible
	USB_MS_ELEMENT_DLS2					= 0x0800
};

typedef struct usb_midi_element_descriptor {
	uint8	length;
	uint8	descriptor_type;
	uint8	descriptor_subtype;	// USB_MS_ELEMENT_DESCRIPTOR
	uint8	id;
	uint8	inputs_count;
	usb_midi_source input_source[0];	// inputs_count times
	// uint8	outputs_count;
	// uint8	input_terminal_id;
	// uint8	output_terminal_id;
	// uint8	capabilities_size;
	// uint8	capabilities[0];	// capabilities_size bytes
	// uint8	string_descriptor;	// see USB_MS_ELEMENT_* enum above
} _PACKED usb_midi_element_descriptor;

// Class-Specific MIDIStream Bulk Data Endpoint descriptor (p26)

#define USB_MS_GENERAL_DESCRIPTOR	0x01

typedef struct usb_midi_endpoint_descriptor {
	uint8	length;
	uint8	descriptor_type;	// USB_DESCRIPTOR_CS_ENDPOINT
	uint8	descriptor_subtype;	// USB_MS_GENERAL_DESCRIPTOR
	uint8	jacks_count;
	uint8	jacks_id[0];		// jacks_count times
} _PACKED usb_midi_endpoint_descriptor;


#ifdef __cplusplus

// Phase 5.2: Lock-free ring buffer for low-latency MIDI event handling
// Uses Haiku native atomics (vint32) instead of std::atomic
// Implements generation counter for ABA problem protection
// Target: < 2ms latency, < 0.5ms jitter

class MIDIEventBuffer {
private:
	usb_midi_event_packet_v2*	fBuffer;
	size_t						fCapacity;
	
	// Haiku native atomics - int32 with atomic operations
	int32	fWritePos;		// Current write position
	int32	fReadPos;		// Current read position
	int32	fGeneration;		// Generation counter for ABA problem protection
	
	bigtime_t	fLastFlushTime;	// Last buffer flush timestamp

public:
	// Constructor with adaptive sizing
	explicit MIDIEventBuffer(size_t capacity = MIDI_BUFFER_SIZE_DEFAULT)
		: fCapacity(capacity), fLastFlushTime(0)
	{
		// Validate capacity range
		if (capacity < MIDI_BUFFER_SIZE_MIN)
			fCapacity = MIDI_BUFFER_SIZE_MIN;
		else if (capacity > MIDI_BUFFER_SIZE_MAX)
			fCapacity = MIDI_BUFFER_SIZE_MAX;
		
		// Allocate buffer - plain C array, not std::array
		fBuffer = (usb_midi_event_packet_v2*)malloc(
			fCapacity * sizeof(usb_midi_event_packet_v2));
		
		// Initialize atomic counters using Haiku atomics
		atomic_set(&fWritePos, 0);
		atomic_set(&fReadPos, 0);
		atomic_set(&fGeneration, 0);
	}
	
	~MIDIEventBuffer()
	{
		if (fBuffer != NULL) {
			free(fBuffer);
			fBuffer = NULL;
		}
	}
	
	// Lock-free write operation with generation counter for ABA protection
	bool TryWrite(const usb_midi_event_packet_v2& packet)
	{
		if (fBuffer == NULL)
			return false;
		
		// Read current positions atomically
		int32 currentWrite = atomic_get(&fWritePos);
		int32 currentRead = atomic_get(&fReadPos);
		
		// Calculate next write position (circular buffer)
		int32 nextWrite = (currentWrite + 1) % fCapacity;
		
		// Check if buffer is full
		if (nextWrite == currentRead)
			return false;	// Buffer full, cannot write
		
		// Write packet to buffer
		fBuffer[currentWrite] = packet;
		
		// Memory write barrier - ensure write is visible before updating position
		// This is critical for correctness on multi-core systems
		__atomic_thread_fence(__ATOMIC_RELEASE);
		
		// Update write position atomically
		atomic_set(&fWritePos, nextWrite);
		
		// Increment generation counter for ABA protection
		// Prevents issues when positions wrap around
		atomic_add(&fGeneration, 1);
		
		return true;
	}
	
	// Lock-free read operation with proper memory ordering
	bool TryRead(usb_midi_event_packet_v2& packet)
	{
		if (fBuffer == NULL)
			return false;
		
		// Read current positions atomically
		int32 currentRead = atomic_get(&fReadPos);
		int32 currentWrite = atomic_get(&fWritePos);
		
		// Check if buffer is empty
		if (currentRead == currentWrite)
			return false;	// Buffer empty, no data available
		
		// Read packet from buffer
		packet = fBuffer[currentRead];
		
		// Memory read barrier - ensure read completes before updating position
		__atomic_thread_fence(__ATOMIC_ACQUIRE);
		
		// Calculate next read position (circular buffer)
		int32 nextRead = (currentRead + 1) % fCapacity;
		
		// Update read position atomically
		atomic_set(&fReadPos, nextRead);
		
		return true;
	}
	
	// Check if buffer should be flushed (max 1ms latency)
	bool ShouldFlush() const
	{
		bigtime_t now = system_time();
		return (now - fLastFlushTime) > 1000;	// 1ms threshold
	}
	
	// Update last flush time
	void MarkFlushed()
	{
		fLastFlushTime = system_time();
	}
	
	// Get current number of events in buffer (non-atomic, for monitoring)
	size_t Count() const
	{
		int32 write = atomic_get(const_cast<int32*>(&fWritePos));
		int32 read = atomic_get(const_cast<int32*>(&fReadPos));
		
		if (write >= read)
			return write - read;
		else
			return fCapacity - read + write;
	}
	
	// Get buffer capacity
	size_t Capacity() const { return fCapacity; }
	
	// Check if buffer is empty
	bool IsEmpty() const
	{
		return atomic_get(const_cast<int32*>(&fReadPos)) == atomic_get(const_cast<int32*>(&fWritePos));
	}
	
	// Check if buffer is full
	bool IsFull() const
	{
		int32 write = atomic_get(const_cast<int32*>(&fWritePos));
		int32 read = atomic_get(const_cast<int32*>(&fReadPos));
		int32 nextWrite = (write + 1) % fCapacity;
		return nextWrite == read;
	}
	
	// Get generation counter (for debugging ABA issues)
	int32 Generation() const
	{
		return atomic_get(const_cast<int32*>(&fGeneration));
	}
	
	// Prevent copying (no copy constructor/assignment)
private:
	MIDIEventBuffer(const MIDIEventBuffer&);
	MIDIEventBuffer& operator=(const MIDIEventBuffer&);
};

#endif	// __cplusplus


#endif	// USB_MIDI_H
