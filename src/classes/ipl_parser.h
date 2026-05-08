#ifndef IPL_PARSER_H
#define IPL_PARSER_H

#include "item_placement.h"

#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/templates/vector.hpp>

using namespace godot;

// =============================================================================
// IplParser — Parses GTA IPL (Item Placement) files in both text and binary
// formats. All coordinates are converted to Godot's system during parsing.
//
// Text IPL (from .dat references):
//   Section "inst": ID, ModelName, Interior, PosX, PosY, PosZ,
//                    RotX, RotY, RotZ, RotW, LODIndex
//   11 tokens for GTA San Andreas.
//
// Binary IPL (streaming, from IMG archive):
//   Header: "bnry" magic (4 bytes) + num_instances (int32)
//   Skip 68 bytes (section offsets/counts)
//   Per instance (40 bytes):
//     float[3] position, float[4] rotation, int32 id, int32 interior, int32 lod
//
// Reference: http://www.gtamodding.com/wiki/Item_Placement
// =============================================================================

class IplParser {
public:
	// Parse a text IPL file. Returns all INST placements.
	static Vector<ItemPlacement> parse_text(const String &p_absolute_path);

	// Parse a binary IPL from raw bytes (read from IMG archive).
	static Vector<ItemPlacement> parse_binary(const PackedByteArray &p_data);
};

#endif // IPL_PARSER_H
