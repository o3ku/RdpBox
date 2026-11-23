#include "common/rdp/RdpPointerAlpha.h"

#include <cstddef>
#include <cstdint>

namespace RdpPointerAlpha
{
bool applyAndMask(FrameBuffer &frame, const std::uint8_t *andMask, std::size_t andMaskLength,
                  std::uint32_t xorBpp)
{
    if (frame.empty() || frame.width <= 0 || frame.height <= 0 || frame.stride < frame.width * 4)
        return false;

    const std::size_t requiredBytes = static_cast<std::size_t>(frame.stride)
        * static_cast<std::size_t>(frame.height);
    if (frame.pixels.size() < requiredBytes)
        return false;

    // The frame already carries its own transparency (any pixel with
    // alpha != 0): the alpha channel is authoritative and the AND mask
    // must not be consulted. For a 32bpp ARGB pointer -- the Windows
    // aero cursors, I-beam included -- the sender fills the alpha byte
    // and leaves the AND mask all zeros (vestigial: 0x00 means opaque),
    // so applying the mask here would force every transparent background
    // pixel opaque and turn a black shape on a transparent background
    // into a solid black rectangle. FreeRDP itself ignores the AND mask
    // for xorBpp == 32 for the same reason. Only a frame with no alpha
    // at all (BGRX) needs the mask.
    for (int y = 0; y < frame.height; ++y)
    {
        const std::uint8_t *row = frame.pixels.data()
            + static_cast<std::size_t>(y) * static_cast<std::size_t>(frame.stride);
        for (int x = 0; x < frame.width; ++x)
        {
            if (row[static_cast<std::size_t>(x) * 4u + 3u] != 0)
                return true;
        }
    }

    if (!andMask || andMaskLength == 0)
        return false;

    // One bit per pixel, rows padded up to an even byte count, exactly as
    // freerdp_image_copy_from_pointer_data_*bpp lays the mask out.
    std::size_t andStep = static_cast<std::size_t>((frame.width + 7) / 8);
    andStep += andStep % 2;
    if (andStep == 0 || andStep * static_cast<std::size_t>(frame.height) > andMaskLength)
        return false;

    // Colour pointers arrive bottom-up on the wire and FreeRDP flips them
    // while decoding; 1bpp pointers do not flip.
    const bool flipRows = xorBpp != 1;

    for (int y = 0; y < frame.height; ++y)
    {
        const int maskRow = flipRows ? frame.height - 1 - y : y;
        const std::uint8_t *maskBytes = andMask + static_cast<std::size_t>(maskRow) * andStep;
        std::uint8_t *row = frame.pixels.data()
            + static_cast<std::size_t>(y) * static_cast<std::size_t>(frame.stride);
        for (int x = 0; x < frame.width; ++x)
        {
            std::uint8_t *pixel = row + static_cast<std::size_t>(x) * 4u;
            if (pixel[3] != 0)
                continue;

            const bool transparent = (maskBytes[static_cast<std::size_t>(x) / 8u]
                                      & static_cast<std::uint8_t>(0x80u >> (x % 8))) != 0;
            pixel[3] = transparent ? static_cast<std::uint8_t>(0x00)
                                   : static_cast<std::uint8_t>(0xFF);
        }
    }

    return true;
}
}
