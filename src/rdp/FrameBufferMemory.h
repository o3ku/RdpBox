#pragma once

#include "common/NativeTypes.h"

#include <cstdint>
#include <vector>

namespace FrameBufferMemory
{
inline void release(FrameBuffer &frame)
{
    frame.width = 0;
    frame.height = 0;
    frame.stride = 0;
    std::vector<std::uint8_t>().swap(frame.pixels);
}
}
