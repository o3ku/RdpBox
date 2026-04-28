#include <cassert>

#include "common/NativeTypes.h"
#include "rdp/FrameBufferAnalysis.h"

namespace
{
FrameBuffer solidFrame(int width, int height, std::uint8_t b, std::uint8_t g, std::uint8_t r)
{
    FrameBuffer frame;
    frame.width = width;
    frame.height = height;
    frame.stride = width * 4;
    frame.pixels.resize(static_cast<std::size_t>(frame.stride) * static_cast<std::size_t>(frame.height));

    for (std::size_t i = 0; i < frame.pixels.size(); i += 4) {
        frame.pixels[i] = b;
        frame.pixels[i + 1] = g;
        frame.pixels[i + 2] = r;
        frame.pixels[i + 3] = 255;
    }

    return frame;
}
}

int main()
{
    {
        const FrameBuffer frame = solidFrame(1280, 720, 255, 255, 255);
        assert(isLikelyPlaceholderWhiteFrame(frame));
    }

    {
        FrameBuffer frame = solidFrame(1280, 720, 255, 255, 255);
        for (int y = 0; y < 200; ++y) {
            for (int x = 0; x < 300; ++x) {
                const std::size_t offset = static_cast<std::size_t>(y) * static_cast<std::size_t>(frame.stride)
                    + static_cast<std::size_t>(x) * 4u;
                frame.pixels[offset] = 32;
                frame.pixels[offset + 1] = 64;
                frame.pixels[offset + 2] = 96;
            }
        }
        assert(!isLikelyPlaceholderWhiteFrame(frame));
    }

    {
        const FrameBuffer frame = solidFrame(1280, 720, 17, 17, 17);
        assert(!isLikelyPlaceholderWhiteFrame(frame));
    }

    return 0;
}
