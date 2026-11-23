#pragma once

#include "common/NativeTypes.h"

#include <cstddef>
#include <cstdint>

namespace RdpPointerAlpha
{
// Reconstructs per-pixel alpha for a server pointer frame from its RDP
// AND mask.
//
// freerdp_image_copy_from_pointer_data decodes a pointer into BGRA32, but
// for a 32bpp pointer whose sender leaves the alpha byte unused (BGRX, the
// common Windows case) every decoded pixel has alpha 0: a white opaque
// pixel and a transparent pixel are then both (0, 0, 0, 0) in the frame,
// and the AND mask is the only record of which is which. Guessing from
// the colour alone cannot work -- a black opaque pixel is identical to a
// transparent one -- so the mask has to be consulted here, while it is
// still available in nativefreerdp_pointer_new.
//
// For each pixel whose decoded alpha is already non-zero (a true-alpha
// cursor, or an inverted pixel FreeRDP already resolved to alpha 0xFF)
// the pixel is left alone. In fact, if any pixel at all carries alpha
// the whole frame is left alone: the alpha channel is authoritative,
// and for a 32bpp ARGB pointer the AND mask is vestigial -- standard
// Windows 32bpp ARGB cursors (the aero set, I-beam included) store
// transparency in the alpha channel and leave the AND mask all zeros
// (0x00 = opaque), so consulting it would force the transparent
// background opaque and turn a black shape into a solid black
// rectangle. Only a frame with no alpha at all (BGRX: every pixel
// alpha 0) is reconstructed from the mask: for a pixel with alpha 0
// the AND bit decides, 1 means transparent (alpha 0x00), 0 means
// opaque (alpha 0xFF). That is FreeRDP's own rule for icon data
// (alpha = mask bit ? 0x00 : 0xFF).
//
// The AND-mask layout matches FreeRDP's pointer decoder: one bit per
// pixel, row stride ((width + 7) / 8) padded up to an even byte count,
// rows bottom-up for colour pointers (xorBpp != 1) and top-down for
// 1bpp pointers. Returns false (and leaves the frame untouched) when
// the frame is invalid or the mask is missing or too short for the
// frame; the caller then falls back to the classifier's opaque
// last resort, which is the best that can be done with no mask.
bool applyAndMask(FrameBuffer &frame, const std::uint8_t *andMask, std::size_t andMaskLength,
                  std::uint32_t xorBpp);
}
