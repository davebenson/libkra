// ############################################################################ #
// Copyright © 2022-2026 Piet Bronders & Jeroen De Geeter <piet.bronders@gmail.com>
// Licensed under the MIT License.
// See LICENSE in the project root for license information.
// ############################################################################ #

#include "kra_layer.h"

#include <lunasvg.h>

namespace kra
{
    // ---------------------------------------------------------------------------------------------------------------------
    // Extract important common attributes as stored in this layer's XML element
    // ---------------------------------------------------------------------------------------------------------------------
    void Layer::import_attributes(const std::string &p_name, unzFile &p_file, const tinyxml2::XMLElement *p_xml_element)
    {
        /* Get important layer attributes from the XML-file */
        filename = p_xml_element->Attribute("filename");
        name = p_xml_element->Attribute("name");
        uuid = p_xml_element->Attribute("uuid");

        x = p_xml_element->UnsignedAttribute("x", 0);
        y = p_xml_element->UnsignedAttribute("y", 0);
        opacity = p_xml_element->UnsignedAttribute("opacity", 0);

        visible = p_xml_element->BoolAttribute("visible", true);

        switch (type)
        {
        case PAINT_LAYER:
            _import_paint_attributes(p_name, p_file, p_xml_element);
            break;
        case GROUP_LAYER:
            _import_group_attributes(p_name, p_file, p_xml_element);
            break;
        case VECTOR_LAYER:
            _import_vector_attributes(p_name, p_file, p_xml_element);
            break;
        }
    }

    // ---------------------------------------------------------------------------------------------------------------------
    // Get an exported version of this layer that can be used by other (external) programs & wrappers
    // ---------------------------------------------------------------------------------------------------------------------
    std::unique_ptr<ExportedLayer> Layer::get_exported_layer() const
    {
        std::unique_ptr<ExportedLayer> exported_layer = std::make_unique<ExportedLayer>();

        /* Copy all important properties immediately */
        exported_layer->name = name;
        exported_layer->x = x;
        exported_layer->y = y;
        exported_layer->opacity = opacity;
        exported_layer->visible = visible;
        exported_layer->type = type;

        switch (type)
        {
        case PAINT_LAYER:
        {
            exported_layer->color_space = color_space;

            exported_layer->top = layer_data->get_top();
            exported_layer->left = layer_data->get_left();
            exported_layer->bottom = layer_data->get_bottom();
            exported_layer->right = layer_data->get_right();

            exported_layer->pixel_size = layer_data->pixel_size;

            exported_layer->data = layer_data->get_composed_data(color_space);
            break;
        }
        case GROUP_LAYER:
            for (auto const &child : children)
            {
                exported_layer->child_uuids.push_back(child->uuid);
            }
            break;
        case VECTOR_LAYER:
            // Vector layer data is available in svg_content
            // Applications can access the raw SVG data for rendering
            break;
        }

        return exported_layer;
    }

    // ---------------------------------------------------------------------------------------------------------------------
    // Print layer attributes to the output console
    // ---------------------------------------------------------------------------------------------------------------------
    void Layer::print_layer_attributes() const
    {
        fprintf(stdout, "----- Layer with name '%s' contains following values:\n", name.c_str());
        fprintf(stdout, "   >> filename = %s\n", filename.c_str());
        fprintf(stdout, "   >> name = %s\n", name.c_str());
        fprintf(stdout, "   >> uuid = %s\n", uuid.c_str());
        fprintf(stdout, "   >> x = %i\n", x);
        fprintf(stdout, "   >> y = %i\n", y);
        fprintf(stdout, "   >> opacity = %i\n", opacity);
        fprintf(stdout, "   >> visible = %s\n", visible ? "true" : "false");
        fprintf(stdout, "   >> type = %i\n", type);

        switch (type)
        {
        case PAINT_LAYER:
            _print_paint_layer_attributes();
            break;
        case GROUP_LAYER:
            _print_group_layer_attributes();
            break;
        case VECTOR_LAYER:
            _print_vector_layer_attributes();
            break;
        }
    }

    // ---------------------------------------------------------------------------------------------------------------------
    // Extract attributes specific to this layer's type (= PAINT_LAYER) and create a LayerData-instance
    // ---------------------------------------------------------------------------------------------------------------------
    void Layer::_import_paint_attributes(const std::string &p_name, unzFile &p_file, const tinyxml2::XMLElement *p_xml_element)
    {
        std::string color_space_name = p_xml_element->Attribute("colorspacename");
        /* The color space defines the number of 'channels' */
        /* Each seperate layer can have its own color space in KRA, but this doesn't seem to used by default */
        color_space = get_color_space(color_space_name);

        /* Try and find the relevant file that defines this layer's tile data */
        /* This also automatically decrypts the tile data */
        /* The "Sample/"-folder is hard-coded as I have yet to encounter a case where this folder is named differently! */
        const std::string &layer_path = p_name + "/layers/" + filename;
        std::vector<unsigned char> layer_content;
        const char *c_path = layer_path.c_str();
        int errorCode = unzLocateFile(p_file, c_path, 1);
        errorCode += extract_current_file_to_vector(p_file, layer_content);
        if (errorCode == UNZ_OK)
        {
            /* Start extracting the tile data. */
            layer_data = std::make_unique<LayerData>();
            layer_data->import_attributes(layer_content);

            /* Also try to load the default pixel if it exists */
            layer_data->has_default_pixel = false;
            const std::string &default_pixel_path = p_name + "/layers/" + filename + ".defaultpixel";
            std::vector<unsigned char> default_pixel_content;
            int default_pixel_error = unzLocateFile(p_file, default_pixel_path.c_str(), 1);
            default_pixel_error += extract_current_file_to_vector(p_file, default_pixel_content);
            if (default_pixel_error == UNZ_OK && default_pixel_content.size() >= 4)
            {
                layer_data->default_pixel[0] = default_pixel_content[0];
                layer_data->default_pixel[1] = default_pixel_content[1];
                layer_data->default_pixel[2] = default_pixel_content[2];
                layer_data->default_pixel[3] = default_pixel_content[3];
                layer_data->has_default_pixel = true;
                if (verbosity_level >= VERBOSE)
                {
                    fprintf(stdout, "   >> Loaded default pixel for layer '%s': R=%d G=%d B=%d A=%d\n",
                            name.c_str(),
                            layer_data->default_pixel[0],
                            layer_data->default_pixel[1],
                            layer_data->default_pixel[2],
                            layer_data->default_pixel[3]);
                }
            }
        }
        else
        {
            fprintf(stdout, "ERROR: Layer entry with path '%s' could not be found in KRA archive.\n", layer_path.c_str());
        }
    }

    // ---------------------------------------------------------------------------------------------------------------------
    // Extract attributes specific to this layer's type (= GROUP_LAYER) and recursively import child layers
    // ---------------------------------------------------------------------------------------------------------------------
    void Layer::_import_group_attributes(const std::string &p_name, unzFile &p_file, const tinyxml2::XMLElement *p_xml_element)
    {
        const tinyxml2::XMLElement *layers_element = p_xml_element->FirstChildElement("layers");
        const tinyxml2::XMLNode *first_child = layers_element->FirstChild();
        const tinyxml2::XMLElement *layer_node = (first_child != 0) ? first_child->ToElement() : 0;

        while (layer_node != 0)
        {
            /* Check the type of the layer and proceed from there... */
            std::string node_type = layer_node->Attribute("nodetype");
            std::unique_ptr<Layer> layer = std::make_unique<Layer>();

            /* Try to determine the layer type from the node type string */
            if (layer_type_from_string(node_type, &layer->type))
            {
                layer->import_attributes(p_name, p_file, layer_node);

                if (verbosity_level >= VERBOSE)
                {
                    layer->print_layer_attributes();
                }

                children.push_back(std::move(layer));
            }

            /* Try to get the next layer entry... if not available break */
            const tinyxml2::XMLNode *nextSibling = layer_node->NextSibling();
            if (nextSibling == 0)
            {
                break;
            }
            else
            {
                layer_node = nextSibling->ToElement();
            }
        }
    }

    // ---------------------------------------------------------------------------------------------------------------------
    // Print additional attributes specific to this layer's type (= PAINT_LAYER) to the output console
    // ---------------------------------------------------------------------------------------------------------------------
    void Layer::_print_paint_layer_attributes() const
    {
        fprintf(stdout, "   -- Additional attributes specific to this layer's type (= PAINT_LAYER):\n");
        fprintf(stdout, "   >> color_space = %i\n", color_space);
        // TODO: Also print attributes of the layer_data!
    }

    // ---------------------------------------------------------------------------------------------------------------------
    // Print additional attributes specific to this layer's type (= GROUP_LAYER) to the output console
    // ---------------------------------------------------------------------------------------------------------------------
    void Layer::_print_group_layer_attributes() const
    {
        fprintf(stdout, "   -- Additional attributes specific to this layer's type (= GROUP_LAYER):\n");
        fprintf(stdout, "   >> children:\n");
        for (const auto &layer : children)
        {
            fprintf(stdout, "      - '%s' (%s)\n", layer->name.c_str(), layer->uuid.c_str());
        }
    }

    // ---------------------------------------------------------------------------------------------------------------------
    // Extract attributes specific to this layer's type (= VECTOR_LAYER) and load the SVG content
    // ---------------------------------------------------------------------------------------------------------------------
    void Layer::_import_vector_attributes(const std::string &p_name, unzFile &p_file, const tinyxml2::XMLElement *p_xml_element)
    {
        /* Vector layers are stored in a directory named <filename>.shapelayer/content.svg */
        const std::string &svg_path = p_name + "/layers/" + filename + ".shapelayer/content.svg";
        const char *c_path = svg_path.c_str();
        int errorCode = unzLocateFile(p_file, c_path, 1);
        errorCode += extract_current_file_to_vector(p_file, svg_content);
        if (errorCode != UNZ_OK)
        {
            fprintf(stdout, "ERROR: Vector layer SVG content with path '%s' could not be found in KRA archive.\n", svg_path.c_str());
        }
    }

    // ---------------------------------------------------------------------------------------------------------------------
    // Print additional attributes specific to this layer's type (= VECTOR_LAYER) to the output console
    // ---------------------------------------------------------------------------------------------------------------------
    void Layer::_print_vector_layer_attributes() const
    {
        fprintf(stdout, "   -- Additional attributes specific to this layer's type (= VECTOR_LAYER):\n");
        fprintf(stdout, "   >> svg_content size = %zu bytes\n", svg_content.size());
        if (!svg_content.empty())
        {
            fprintf(stdout, "   >> SVG data loaded successfully\n");
        }
    }

    // ---------------------------------------------------------------------------------------------------------------------
    // Compose this layer on top of the existing RGBA buffer
    // ---------------------------------------------------------------------------------------------------------------------
    void Layer::compose(unsigned int document_width, unsigned int document_height, float document_dpi, uint8_t *rgba_inout) const
    {
        if (!visible)
        {
            return; // Skip invisible layers
        }

        switch (type)
        {
        case PAINT_LAYER:
        {
            if (!layer_data)
            {
                break;
            }

            // Get the layer's pixel data
            std::vector<uint8_t> layer_pixels = layer_data->get_composed_data(color_space);

            // If no tile data but default pixel exists, fill entire document with default pixel
            if (layer_pixels.empty() && layer_data->has_default_pixel)
            {
                // Fill entire document with default pixel
                for (unsigned int dy = 0; dy < document_height; dy++)
                {
                    for (unsigned int dx = 0; dx < document_width; dx++)
                    {
                        size_t dst_idx = (dy * document_width + dx) * 4;

                        // Get default pixel color with layer opacity applied
                        uint8_t src_r = layer_data->default_pixel[0];
                        uint8_t src_g = layer_data->default_pixel[1];
                        uint8_t src_b = layer_data->default_pixel[2];
                        uint8_t src_a = (layer_data->default_pixel[3] * opacity) / 255;

                        // Get destination pixel
                        uint8_t dst_r = rgba_inout[dst_idx + 0];
                        uint8_t dst_g = rgba_inout[dst_idx + 1];
                        uint8_t dst_b = rgba_inout[dst_idx + 2];
                        uint8_t dst_a = rgba_inout[dst_idx + 3];

                        // Alpha blending
                        float src_alpha = src_a / 255.0f;
                        float dst_alpha = dst_a / 255.0f;
                        float out_alpha = src_alpha + dst_alpha * (1.0f - src_alpha);

                        if (out_alpha > 0.0f)
                        {
                            rgba_inout[dst_idx + 0] = (uint8_t)((src_r * src_alpha + dst_r * dst_alpha * (1.0f - src_alpha)) / out_alpha);
                            rgba_inout[dst_idx + 1] = (uint8_t)((src_g * src_alpha + dst_g * dst_alpha * (1.0f - src_alpha)) / out_alpha);
                            rgba_inout[dst_idx + 2] = (uint8_t)((src_b * src_alpha + dst_b * dst_alpha * (1.0f - src_alpha)) / out_alpha);
                            rgba_inout[dst_idx + 3] = (uint8_t)(out_alpha * 255.0f);
                        }
                    }
                }
                break;
            }

            if (layer_pixels.empty())
            {
                break;
            }

            int32_t layer_left = layer_data->get_left();
            int32_t layer_top = layer_data->get_top();
            int32_t layer_right = layer_data->get_right();
            int32_t layer_bottom = layer_data->get_bottom();
            unsigned int layer_width = layer_right - layer_left;
            unsigned int layer_height = layer_bottom - layer_top;

            // Blend each pixel from the layer onto the document buffer
            for (unsigned int ly = 0; ly < layer_height; ly++)
            {
                for (unsigned int lx = 0; lx < layer_width; lx++)
                {
                    int doc_x = x + layer_left + lx;
                    int doc_y = y + layer_top + ly;

                    // Skip if outside document bounds
                    if (doc_x < 0 || doc_x >= (int)document_width ||
                        doc_y < 0 || doc_y >= (int)document_height)
                    {
                        continue;
                    }

                    size_t src_idx = (ly * layer_width + lx) * 4;
                    size_t dst_idx = (doc_y * document_width + doc_x) * 4;

                    // Get source pixel with layer opacity applied
                    uint8_t src_r = layer_pixels[src_idx + 0];
                    uint8_t src_g = layer_pixels[src_idx + 1];
                    uint8_t src_b = layer_pixels[src_idx + 2];
                    uint8_t src_a = (layer_pixels[src_idx + 3] * opacity) / 255;

                    // Get destination pixel
                    uint8_t dst_r = rgba_inout[dst_idx + 0];
                    uint8_t dst_g = rgba_inout[dst_idx + 1];
                    uint8_t dst_b = rgba_inout[dst_idx + 2];
                    uint8_t dst_a = rgba_inout[dst_idx + 3];

                    // Alpha blending: out = src over dst
                    float src_alpha = src_a / 255.0f;
                    float dst_alpha = dst_a / 255.0f;
                    float out_alpha = src_alpha + dst_alpha * (1.0f - src_alpha);

                    if (out_alpha > 0.0f)
                    {
                        rgba_inout[dst_idx + 0] = (uint8_t)((src_r * src_alpha + dst_r * dst_alpha * (1.0f - src_alpha)) / out_alpha);
                        rgba_inout[dst_idx + 1] = (uint8_t)((src_g * src_alpha + dst_g * dst_alpha * (1.0f - src_alpha)) / out_alpha);
                        rgba_inout[dst_idx + 2] = (uint8_t)((src_b * src_alpha + dst_b * dst_alpha * (1.0f - src_alpha)) / out_alpha);
                        rgba_inout[dst_idx + 3] = (uint8_t)(out_alpha * 255.0f);
                    }
                }
            }
            break;
        }
        case VECTOR_LAYER:
        {
            if (svg_content.empty())
            {
                break;
            }

            // Check for font usage in SVG and warn about missing fonts
            std::string svg_str((const char*)svg_content.data(), svg_content.size());
            size_t font_family_pos = svg_str.find("font-family:");
            if (font_family_pos != std::string::npos)
            {
                // Extract font name (rough extraction)
                size_t start = font_family_pos + 12; // length of "font-family:"
                while (start < svg_str.size() && (svg_str[start] == ' ' || svg_str[start] == '\t')) start++;
                size_t end = start;
                while (end < svg_str.size() && svg_str[end] != ';' && svg_str[end] != '"' && svg_str[end] != '\'' && svg_str[end] != ',') end++;

                if (end > start)
                {
                    std::string font_name = svg_str.substr(start, end - start);
                    // Trim trailing spaces
                    while (!font_name.empty() && (font_name.back() == ' ' || font_name.back() == '\t'))
                        font_name.pop_back();

                    if (!font_name.empty() && verbosity_level >= VERBOSE)
                    {
                        fprintf(stdout, "   >> Vector layer uses font: %s\n", font_name.c_str());
                    }
                }
            }

            // Parse SVG using lunasvg
            auto document = lunasvg::Document::loadFromData((const char*)svg_content.data(), svg_content.size());
            if (!document)
            {
                fprintf(stderr, "ERROR: Failed to parse SVG for layer '%s'\n", name.c_str());
                break;
            }

            // Calculate scaled dimensions based on DPI
            // SVG uses 96 DPI as default, so scale based on document DPI
            float scale = document_dpi / 96.0f;
            int svg_width = (int)(document->width() * scale);
            int svg_height = (int)(document->height() * scale);

            // Render to bitmap at the scaled size
            auto bitmap = document->renderToBitmap(svg_width, svg_height);
            if (bitmap.isNull())
            {
                fprintf(stderr, "ERROR: Failed to rasterize SVG for layer '%s'\n", name.c_str());
                break;
            }

            // Convert from ARGB32 Premultiplied to RGBA Plain
            bitmap.convertToRGBA();

            uint8_t* svg_data = bitmap.data();

            // Blend the rasterized SVG onto the document buffer
            for (unsigned int sy = 0; sy < svg_height; sy++)
            {
                for (unsigned int sx = 0; sx < svg_width; sx++)
                {
                    int doc_x = x + sx;
                    int doc_y = y + sy;

                    // Skip if outside document bounds
                    if (doc_x < 0 || doc_x >= (int)document_width ||
                        doc_y < 0 || doc_y >= (int)document_height)
                    {
                        continue;
                    }

                    size_t src_idx = (sy * svg_width + sx) * 4;
                    size_t dst_idx = (doc_y * document_width + doc_x) * 4;

                    // Get source pixel with layer opacity applied
                    uint8_t src_r = svg_data[src_idx + 0];
                    uint8_t src_g = svg_data[src_idx + 1];
                    uint8_t src_b = svg_data[src_idx + 2];
                    uint8_t src_a = (svg_data[src_idx + 3] * opacity) / 255;

                    // Get destination pixel
                    uint8_t dst_r = rgba_inout[dst_idx + 0];
                    uint8_t dst_g = rgba_inout[dst_idx + 1];
                    uint8_t dst_b = rgba_inout[dst_idx + 2];
                    uint8_t dst_a = rgba_inout[dst_idx + 3];

                    // Alpha blending
                    float src_alpha = src_a / 255.0f;
                    float dst_alpha = dst_a / 255.0f;
                    float out_alpha = src_alpha + dst_alpha * (1.0f - src_alpha);

                    if (out_alpha > 0.0f)
                    {
                        rgba_inout[dst_idx + 0] = (uint8_t)((src_r * src_alpha + dst_r * dst_alpha * (1.0f - src_alpha)) / out_alpha);
                        rgba_inout[dst_idx + 1] = (uint8_t)((src_g * src_alpha + dst_g * dst_alpha * (1.0f - src_alpha)) / out_alpha);
                        rgba_inout[dst_idx + 2] = (uint8_t)((src_b * src_alpha + dst_b * dst_alpha * (1.0f - src_alpha)) / out_alpha);
                        rgba_inout[dst_idx + 3] = (uint8_t)(out_alpha * 255.0f);
                    }
                }
            }
            break;
        }
        case GROUP_LAYER:
        {
            // Recursively compose all children
            for (const auto &child : children)
            {
                child->compose(document_width, document_height, document_dpi, rgba_inout);
            }
            break;
        }
        }
    }
};