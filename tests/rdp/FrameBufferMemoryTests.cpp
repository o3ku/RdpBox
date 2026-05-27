#include <cassert>

#include "common/NativeTypes.h"
#include "rdp/FrameBufferMemory.h"

int main()
{
    {
        FrameBuffer frame;
        FrameBufferMemory::resize(frame, 640, 480, 2560, 640u * 480u * 4u);
        assert(frame.width == 640);
        assert(frame.height == 480);
        assert(frame.stride == 2560);
        assert(frame.pixels.size() == 640u * 480u * 4u);
        assert(frame.pixels.capacity() >= frame.pixels.size());
    }

    {
        FrameBuffer frame;
        frame.pixels.reserve(3u * 1024u * 1024u);
        frame.pixels.resize(3u * 1024u * 1024u);

        FrameBufferMemory::resize(frame, 320, 200, 1280, 320u * 200u * 4u);
        assert(frame.width == 320);
        assert(frame.height == 200);
        assert(frame.stride == 1280);
        assert(frame.pixels.size() == 320u * 200u * 4u);
        assert(frame.pixels.capacity() <= 320u * 200u * 4u);
    }

    {
        FrameBuffer frame;
        frame.pixels.reserve(1024u * 1024u);
        frame.pixels.resize(128u * 128u * 4u);
        const size_t priorCapacity = frame.pixels.capacity();

        FrameBufferMemory::resize(frame, 64, 64, 256, 64u * 64u * 4u);
        assert(frame.width == 64);
        assert(frame.height == 64);
        assert(frame.stride == 256);
        assert(frame.pixels.size() == 64u * 64u * 4u);
        assert(frame.pixels.capacity() == priorCapacity);
    }

    {
        FrameBuffer frame;
        FrameBufferMemory::resize(frame, 100, 50, 400, 20000u);
        assert(!frame.pixels.empty());

        FrameBufferMemory::release(frame);
        assert(frame.width == 0);
        assert(frame.height == 0);
        assert(frame.stride == 0);
        assert(frame.pixels.empty());
        assert(frame.pixels.capacity() == 0);
    }

    return 0;
}
