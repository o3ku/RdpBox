#include "FrameBufferAnalysis.h"

#include <algorithm>
#include <cstddef>

namespace
{
bool isWhitePixel(const std::uint8_t *pixel)
{
    return pixel[0] >= 245 && pixel[1] >= 245 && pixel[2] >= 245;
}
}

bool isLikelyPlaceholderWhiteFrame(const FrameBuffer &frame)
{
    if (frame.empty() || frame.stride < frame.width * 4)
        return false;

    const int stepX = std::max(1, frame.width / 16);
    const int stepY = std::max(1, frame.height / 16);
    int sampleCount = 0;
    int whiteCount = 0;

    for (int y = 0; y < frame.height; y += stepY) {
        const auto *row = frame.pixels.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(frame.stride);
        for (int x = 0; x < frame.width; x += stepX) {
            const auto *pixel = row + static_cast<std::size_t>(x) * 4u;
            ++sampleCount;
            if (isWhitePixel(pixel))
                ++whiteCount;
        }
    }

    return sampleCount > 0 && (whiteCount * 100 / sampleCount) >= 98;
}
