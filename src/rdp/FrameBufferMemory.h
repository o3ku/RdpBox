#pragma once

#include "common/NativeTypes.h"

#include <cstdint>
#include <vector>

namespace FrameBufferMemory
{
inline void resize(FrameBuffer &frame,
                   int width,
                   int height,
                   int stride,
                   std::size_t requiredBytes)
{
    frame.width = width;
    frame.height = height;
    frame.stride = stride;

    const std::size_t currentCapacity = frame.pixels.capacity();
    const bool needsGrow = frame.pixels.size() < requiredBytes;
    const bool shouldShrink = currentCapacity > requiredBytes * 2 && (currentCapacity - requiredBytes) > (1024 * 1024);

    if (needsGrow || shouldShrink) {
        std::vector<std::uint8_t> resized(requiredBytes);
        frame.pixels.swap(resized);
        return;
    }

    frame.pixels.resize(requiredBytes);
}

inline void release(FrameBuffer &frame)
{
    frame.width = 0;
    frame.height = 0;
    frame.stride = 0;
    std::vector<std::uint8_t>().swap(frame.pixels);
}
}
