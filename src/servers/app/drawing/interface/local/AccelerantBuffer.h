#ifndef ACCELERANT_BUFFER_H
#define ACCELERANT_BUFFER_H

#include <Accelerant.h>
#include "RenderingBuffer.h"

#include <cstdint>

class AccelerantBuffer : public RenderingBuffer {
public:
	AccelerantBuffer();
	AccelerantBuffer(const display_mode& mode,
					 const frame_buffer_config& config);
	AccelerantBuffer(const AccelerantBuffer& other,
					 bool offscreenBuffer = false);
	AccelerantBuffer(AccelerantBuffer&& other) noexcept;
	virtual						~AccelerantBuffer();

	AccelerantBuffer&	operator=(AccelerantBuffer&& other) noexcept;

	virtual	status_t			InitCheck() const;

	virtual	color_space			ColorSpace() const;
	virtual	void*				Bits() const;
	virtual	uint32				BytesPerRow() const;
	virtual	uint32				Width() const;
	virtual	uint32				Height() const;

	void				SetDisplayMode(const display_mode& mode);
	void				SetFrameBufferConfig(
		const frame_buffer_config& config);
	void				SetOffscreenBuffer(bool offscreenBuffer);

private:
	enum BufferFlagBits : uint8_t {
		FLAG_NONE                    = 0,
		FLAG_DISPLAY_MODE_SET        = 1 << 0,
		FLAG_FRAME_BUFFER_CONFIG_SET = 1 << 1,
		FLAG_OFFSCREEN_BUFFER        = 1 << 2
	};

	bool				IsInitialized() const noexcept;
	uint32				GetDimensionOrDefault(uint32 value) const noexcept;

	bool				HasFlag(uint8_t flag) const noexcept;
	void				SetFlag(uint8_t flag, bool value) noexcept;

	display_mode		fDisplayMode;
	frame_buffer_config	fFrameBufferConfig;
	uint8_t				fFlags;
};

#endif // ACCELERANT_BUFFER_H
