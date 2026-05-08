#include "ipl_parser.h"
#include "../rw/binary_reader.h"

#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

// =============================================================================
// IplParser implementation
// =============================================================================

// Helper: split an IPL inst line by ", " (comma-space separator used in SA).
static PackedStringArray split_ipl_line(const String &p_line) {
	PackedStringArray raw = p_line.split(",", false);
	PackedStringArray result;
	for (int i = 0; i < raw.size(); i++) {
		String token = raw[i].strip_edges();
		if (!token.is_empty()) {
			result.push_back(token);
		}
	}
	return result;
}

Vector<ItemPlacement> IplParser::parse_text(const String &p_absolute_path) {
	Vector<ItemPlacement> placements;

	Ref<FileAccess> file = FileAccess::open(p_absolute_path, FileAccess::READ);
	if (file.is_null()) {
		UtilityFunctions::printerr("[IplParser] Failed to open text IPL: ", p_absolute_path);
		return placements;
	}

	bool in_inst_section = false;

	while (!file->eof_reached()) {
		String line = file->get_line().strip_edges();

		// Skip empty lines and comments.
		if (line.is_empty() || line.begins_with("#")) {
			continue;
		}

		String lower = line.to_lower();

		// Check for section boundaries.
		if (lower == "end") {
			in_inst_section = false;
			continue;
		}
		if (lower == "inst") {
			in_inst_section = true;
			continue;
		}

		// If we hit a different section name (no comma = single token), switch off.
		if (!line.contains(",")) {
			in_inst_section = false;
			continue;
		}

		if (!in_inst_section) {
			continue;
		}

		// Parse INST line for GTA SA: ID, ModelName, Interior, PosX, PosY, PosZ,
		//                              RotX, RotY, RotZ, RotW, LODIndex
		PackedStringArray tokens = split_ipl_line(line);
		if (tokens.size() < 11) {
			continue;
		}

		ItemPlacement p;
		p.definition_id = tokens[0].to_int();
		p.item_name = tokens[1].to_lower();
		p.interior = tokens[2].to_int();

		// Coordinate conversion: Godot(x, y, z) = GTA(x, z, -y)
		float gta_x = tokens[3].to_float();
		float gta_y = tokens[4].to_float();
		float gta_z = tokens[5].to_float();
		p.position = Vector3(gta_x, gta_z, -gta_y);

		// Quaternion: Handedness change requires negating angle, then mapping axes.
		// Godot_X = -GTA_X, Godot_Y = -GTA_Z, Godot_Z = -(-GTA_Y) = GTA_Y
		float gta_rx = tokens[6].to_float();
		float gta_ry = tokens[7].to_float();
		float gta_rz = tokens[8].to_float();
		float gta_rw = tokens[9].to_float();
		p.rotation = Quaternion(-gta_rx, -gta_rz, gta_ry, gta_rw);

		p.lod_index = tokens[10].to_int();

		placements.push_back(p);
	}

	return placements;
}

Vector<ItemPlacement> IplParser::parse_binary(const PackedByteArray &p_data) {
	Vector<ItemPlacement> placements;

	BinaryReader reader(p_data);

	// Verify "bnry" magic (0x796E7262 little-endian = "bnry").
	if (reader.get_length() < 76) {
		return placements;
	}

	uint32_t magic = reader.read_uint32();
	if (magic != 0x79726E62) { // "bnry" in little-endian
		return placements; // Silently skip non-binary IPLs
	}

	int32_t num_instances = reader.read_int32();

	// Skip 68 bytes of section offsets/counts to reach instance data.
	// (matches Unity reference: reader.SkipStream(68))
	reader.skip(68);

	placements.resize(num_instances);

	for (int32_t i = 0; i < num_instances; i++) {
		if (!reader.can_read(40)) {
			break;
		}

		ItemPlacement p;

		// Position: read as GTA (x, y, z), convert to Godot (x, z, -y).
		float gta_x = reader.read_float();
		float gta_y = reader.read_float();
		float gta_z = reader.read_float();
		p.position = Vector3(gta_x, gta_z, -gta_y);

		// Rotation: convert to Godot (-rx, -rz, ry, rw).
		float gta_rx = reader.read_float();
		float gta_ry = reader.read_float();
		float gta_rz = reader.read_float();
		float gta_rw = reader.read_float();
		p.rotation = Quaternion(-gta_rx, -gta_rz, gta_ry, gta_rw);

		// Object ID.
		p.definition_id = reader.read_int32();

		// Interior (skip for streaming — read but often 0).
		p.interior = reader.read_int32();

		// LOD index.
		p.lod_index = reader.read_int32();

		p.item_name = "streaming";

		placements.set(i, p);
	}

	return placements;
}
