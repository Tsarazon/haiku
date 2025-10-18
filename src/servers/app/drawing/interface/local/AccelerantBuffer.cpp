/*
 * Copyright 2005 Michael Lotz <mmlr@mlotz.ch>
 * All rights reserved. Distributed under the terms of the MIT license.
 */

//! A RenderingBuffer implementation that accesses graphics memory directly.

#include "AccelerantBuffer.h"

#include <utility>

namespace {
	constexpr uint32 kDefaultDimension = 0;
	constexpr color_space kDefaultColorSpace = B_NO_COLOR_SPACE;
	constexpr status_t kNotInitializedStatus = B_NO_INIT;
	constexpr status_t kInitializedStatus = B_OK;
}


AccelerantBuffer::AccelerantBuffer()
:
fDisplayMode{},
fFrameBufferConfig{},
fFlags(FLAG_NONE)
{
}


AccelerantBuffer::AccelerantBuffer(const display_mode& mode,
								   const frame_buffer_config& config)
:
AccelerantBuffer()
{
	SetDisplayMode(mode);
	SetFrameBufferConfig(config);
}


AccelerantBuffer::AccelerantBuffer(const AccelerantBuffer& other,
								   bool offscreenBuffer)
:
fDisplayMode(other.fDisplayMode),
fFrameBufferConfig(other.fFrameBufferConfig),
fFlags(other.fFlags)
{
	if (offscreenBuffer || other.HasFlag(FLAG_OFFSCREEN_BUFFER))
		SetFlag(FLAG_OFFSCREEN_BUFFER, true);
}


AccelerantBuffer::AccelerantBuffer(AccelerantBuffer&& other) noexcept
:
fDisplayMode(std::move(other.fDisplayMode)),
fFrameBufferConfig(std::move(other.fFrameBufferConfig)),
fFlags(other.fFlags)
{
	other.fFlags = FLAG_NONE;
}


AccelerantBuffer::~AccelerantBuffer()
{
}


AccelerantBuffer&
AccelerantBuffer::operator=(AccelerantBuffer&& other) noexcept
{
	if (this != &other) {
		fDisplayMode = std::move(other.fDisplayMode);
		fFrameBufferConfig = std::move(other.fFrameBufferConfig);
		fFlags = other.fFlags;

		other.fFlags = FLAG_NONE;
	}
	return *this;
}


bool
AccelerantBuffer::HasFlag(uint8_t flag) const noexcept
{
	return (fFlags & flag) == flag;
}


void
AccelerantBuffer::SetFlag(uint8_t flag, bool value) noexcept
{
	if (value) {
		fFlags |= flag;
	} else {
		fFlags &= ~flag;
	}
}


bool
AccelerantBuffer::IsInitialized() const noexcept
{
	return HasFlag(FLAG_DISPLAY_MODE_SET)
	&& HasFlag(FLAG_FRAME_BUFFER_CONFIG_SET);
}


uint32
AccelerantBuffer::GetDimensionOrDefault(uint32 value) const noexcept
{
	return IsInitialized() ? value : kDefaultDimension;
}


status_t
AccelerantBuffer::InitCheck() const
{
	return IsInitialized() ? kInitializedStatus : kNotInitializedStatus;
}


color_space
AccelerantBuffer::ColorSpace() const
{
	if (!IsInitialized())
		return kDefaultColorSpace;

	return static_cast<color_space>(fDisplayMode.space);
}


void*
AccelerantBuffer::Bits() const
{
	if (!IsInitialized())
		return nullptr;

	auto* bits = static_cast<uint8*>(fFrameBufferConfig.frame_buffer);
	if (bits == nullptr)
		return nullptr;

	if (HasFlag(FLAG_OFFSCREEN_BUFFER)) {
		const uint32 offset = static_cast<uint32>(fDisplayMode.virtual_height)
		* fFrameBufferConfig.bytes_per_row;
		bits += offset;
	}

	return bits;
}


uint32
AccelerantBuffer::BytesPerRow() const
{
	return GetDimensionOrDefault(fFrameBufferConfig.bytes_per_row);
}


uint32
AccelerantBuffer::Width() const
{
	return GetDimensionOrDefault(fDisplayMode.virtual_width);
}


uint32
AccelerantBuffer::Height() const
{
	return GetDimensionOrDefault(fDisplayMode.virtual_height);
}


void
AccelerantBuffer::SetDisplayMode(const display_mode& mode)
{
	fDisplayMode = mode;
	SetFlag(FLAG_DISPLAY_MODE_SET, true);
}


void
AccelerantBuffer::SetFrameBufferConfig(const frame_buffer_config& config)
{
	fFrameBufferConfig = config;
	SetFlag(FLAG_FRAME_BUFFER_CONFIG_SET, true);
}


void
AccelerantBuffer::SetOffscreenBuffer(bool offscreenBuffer)
{
	SetFlag(FLAG_OFFSCREEN_BUFFER, offscreenBuffer);
}
