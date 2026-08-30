// ############################################################################ #
// Example program: kra-filtered-export
// Demonstrates composing layers from a KRA file into a single PNG output
// ############################################################################ #

#include "../libkra/kra_utility.h"
#include "../libkra/kra_document.h"
#include "../libkra/kra_layer.h"
#include "../libpng/png.h"

#include <iostream>
#include <vector>
#include <regex>
#include <string>

// ---------------------------------------------------------------------------------------------------------------------
// Filter action types
// ---------------------------------------------------------------------------------------------------------------------
enum FilterAction
{
	ACCEPT,
	DENY
};

struct FilterRule
{
	std::regex pattern;
	FilterAction action;
};

// ---------------------------------------------------------------------------------------------------------------------
// Check if a layer should be included based on filter rules
// ---------------------------------------------------------------------------------------------------------------------
bool should_include_layer(const std::string &layer_name,
						  const std::vector<FilterRule> &rules,
						  FilterAction default_action)
{
	// Check each rule in order
	for (const auto &rule : rules)
	{
		if (std::regex_search(layer_name, rule.pattern))
		{
			return rule.action == ACCEPT;
		}
	}

	// No match, use default
	return default_action == ACCEPT;
}

// ---------------------------------------------------------------------------------------------------------------------
// Export RGBA data to a PNG file
// ---------------------------------------------------------------------------------------------------------------------
bool write_png(const char *filename, unsigned int width, unsigned int height, const uint8_t *rgba_data)
{
	bool success = true;
	FILE *fp = NULL;
	png_structp png_ptr = NULL;
	png_infop info_ptr = NULL;
	png_bytep row = NULL;

	fp = fopen(filename, "wb");
	if (fp == NULL)
	{
		std::cerr << "Could not open file " << filename << " for writing" << std::endl;
		return false;
	}

	png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
	if (png_ptr == NULL)
	{
		std::cerr << "Could not allocate write struct" << std::endl;
		fclose(fp);
		return false;
	}

	info_ptr = png_create_info_struct(png_ptr);
	if (info_ptr == NULL)
	{
		std::cerr << "Could not allocate info struct" << std::endl;
		png_destroy_write_struct(&png_ptr, NULL);
		fclose(fp);
		return false;
	}

	if (setjmp(png_jmpbuf(png_ptr)))
	{
		std::cerr << "Error during png creation" << std::endl;
		png_destroy_write_struct(&png_ptr, &info_ptr);
		fclose(fp);
		return false;
	}

	png_init_io(png_ptr, fp);

	png_set_IHDR(png_ptr, info_ptr, width, height, 8, PNG_COLOR_TYPE_RGBA,
				 PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_BASE, PNG_FILTER_TYPE_BASE);

	png_write_info(png_ptr, info_ptr);

	row = (png_bytep)malloc(4 * width * sizeof(png_byte));

	for (unsigned int y = 0; y < height; y++)
	{
		memcpy(row, &rgba_data[4 * width * y], 4 * width * sizeof(png_byte));
		png_write_row(png_ptr, row);
	}

	png_write_end(png_ptr, NULL);

	if (fp != NULL)
		fclose(fp);
	if (info_ptr != NULL)
		png_free_data(png_ptr, info_ptr, PNG_FREE_ALL, -1);
	if (png_ptr != NULL)
		png_destroy_write_struct(&png_ptr, (png_infopp)NULL);
	if (row != NULL)
		std::free(row);

	return success;
}

// ---------------------------------------------------------------------------------------------------------------------
// Compose all layers and save to PNG
// ---------------------------------------------------------------------------------------------------------------------
bool compose_and_export(const std::unique_ptr<kra::Document> &document,
						const std::string &output_filename,
						const std::vector<FilterRule> &filter_rules,
						FilterAction default_action)
{
	unsigned int width = document->width;
	unsigned int height = document->height;

	std::cout << "Composing document: " << width << "x" << height << std::endl;

	// Allocate RGBA buffer initialized to transparent
	std::vector<uint8_t> composed_buffer(width * height * 4, 0);

	// Compose each layer onto the buffer (from bottom to top)
	// Note: layers are stored in reverse order (top layer first), so iterate backwards
	for (auto it = document->layers.rbegin(); it != document->layers.rend(); ++it)
	{
		const auto &layer = *it;

		// Check if layer passes filter
		if (!should_include_layer(layer->name, filter_rules, default_action))
		{
			std::cout << "  Skipping layer: " << layer->name << " (filtered out)" << std::endl;
			continue;
		}

		std::cout << "  Composing layer: " << layer->name << " (type " << layer->type << ")" << std::endl;
		layer->compose(width, height, document->x_res, composed_buffer.data());
	}

	// Save the composed result
	std::cout << "Writing composed image to: " << output_filename << std::endl;
	return write_png(output_filename.c_str(), width, height, composed_buffer.data());
}

// ---------------------------------------------------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------------------------------------------------
int main(int argc, const char *argv[])
{
	if (argc < 2)
	{
		std::cerr << "Usage: " << argv[0] << " <input.kra> [output.png] [options]" << std::endl;
		std::cerr << "  Composes layers from a KRA file into a single PNG" << std::endl;
		std::cerr << std::endl;
		std::cerr << "Options:" << std::endl;
		std::cerr << "  --accept=REGEX      Accept layers matching regex pattern" << std::endl;
		std::cerr << "  --deny=REGEX        Deny layers matching regex pattern" << std::endl;
		std::cerr << "  --default-deny      Change default action to DENY (default is ACCEPT)" << std::endl;
		std::cerr << std::endl;
		std::cerr << "Filter rules are evaluated in order. First match wins." << std::endl;
		return 1;
	}

	std::string input_file;
	std::string output_file = "output.png";
	std::vector<FilterRule> filter_rules;
	FilterAction default_action = ACCEPT;

	// Parse command-line arguments
	for (int i = 1; i < argc; ++i)
	{
		std::string arg = argv[i];

		if (arg.substr(0, 9) == "--accept=")
		{
			std::string pattern = arg.substr(9);
			try
			{
				filter_rules.push_back({std::regex(pattern), ACCEPT});
			}
			catch (const std::regex_error &e)
			{
				std::cerr << "Invalid regex pattern: " << pattern << std::endl;
				return 1;
			}
		}
		else if (arg.substr(0, 7) == "--deny=")
		{
			std::string pattern = arg.substr(7);
			try
			{
				filter_rules.push_back({std::regex(pattern), DENY});
			}
			catch (const std::regex_error &e)
			{
				std::cerr << "Invalid regex pattern: " << pattern << std::endl;
				return 1;
			}
		}
		else if (arg == "--default-deny")
		{
			default_action = DENY;
		}
		else if (input_file.empty())
		{
			input_file = arg;
		}
		else if (output_file == "output.png")
		{
			output_file = arg;
		}
		else
		{
			std::cerr << "Unknown argument: " << arg << std::endl;
			return 1;
		}
	}

	if (input_file.empty())
	{
		std::cerr << "Error: No input file specified" << std::endl;
		return 1;
	}

	// Convert to wstring for document load
	std::wstring input_wstr(input_file.begin(), input_file.end());

	// Load the document
	std::unique_ptr<kra::Document> document = std::make_unique<kra::Document>();
	int result = document->load(input_wstr);
	if (result != 0)
	{
		std::cerr << "Failed to load document: " << input_file << std::endl;
		return result;
	}

	// Only handle RGBA documents for now
	if (document->color_space != kra::ColorSpace::RGBA)
	{
		std::cerr << "ERROR: Only RGBA color space is currently supported" << std::endl;
		std::cerr << "Document color space: " << kra::get_color_space_name(document->color_space) << std::endl;
		return 1;
	}

	// Compose and export
	if (!compose_and_export(document, output_file, filter_rules, default_action))
	{
		std::cerr << "Failed to compose and export document" << std::endl;
		return 1;
	}

	std::cout << "Successfully exported to: " << output_file << std::endl;
	return 0;
}
