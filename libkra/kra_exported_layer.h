// ############################################################################ #
// Copyright © 2022-2026 Piet Bronders & Jeroen De Geeter <piet.bronders@gmail.com>
// Licensed under the MIT License.
// See LICENSE in the project root for license information.
// ############################################################################ #

#ifndef KRA_EXPORTED_LAYER_H
#define KRA_EXPORTED_LAYER_H

namespace kra
{
    // This class represents an exported version of a Layer */
    /* In the case of a PAINT_LAYER, this class stores the decompressed data of the entire layer */
    /* In the case of a GROUP_LAYER, this class stores a vector of UUIDs of its child layers */
    class ExportedLayer
    {
    public:
        std::string name;

        unsigned int x;
        unsigned int y;

        uint8_t opacity;

        bool visible;

        LayerType type;

        // PAINT_LAYER
        ColorSpace color_space = RGBA;

        int32_t top;
        int32_t left;
        int32_t bottom;
        int32_t right;

        unsigned int pixel_size;
        std::vector<uint8_t> data;

        /* `data` only covers the [left, right) x [top, bottom) region the layer actually painted. */
        /* Every pixel outside of that region has the colour below, in the same channel order as `data`. */
        std::vector<uint8_t> default_pixel;

        // GROUP_LAYER
        std::vector<std::string> child_uuids;
    };
};

#endif // KRA_EXPORTED_LAYER_H