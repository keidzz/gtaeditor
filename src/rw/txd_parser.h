#ifndef TXD_PARSER_H
#define TXD_PARSER_H

#include "rw_types.h"

#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/templates/hash_map.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/string.hpp>

using namespace godot;

// =============================================================================
// TxdParser — Parses RenderWare TXD (Texture Dictionary) files.
//
// TXD files contain one or more textures, each as a TextureNative section.
// Each texture has a name, optional alpha name, dimensions, raster format,
// and pixel data in one of several compressed/uncompressed formats.
//
// Supported formats:
//   - DXT1 (Format_565 / Format_1555)
//   - DXT3 (Format_4444)
//   - Raw BGRA (Format_8888 / Format_888)
//   - Palette8 (256-color indexed)
//
// Reference: http://www.gtamodding.com/wiki/Texture_Native_Struct
// =============================================================================

struct TxdTexture {
	String name;
	String alpha_name;
	Ref<ImageTexture> texture;
	bool has_alpha = false;
};

class TxdParser {
public:
	// Parse a TXD file from raw bytes.
	// Returns a map of texture_name (lowercase) → TxdTexture.
	static HashMap<String, TxdTexture> parse(const PackedByteArray &p_data);
};

#endif // TXD_PARSER_H
