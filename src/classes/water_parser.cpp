#include "water_parser.h"

#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

// Helper to convert GTA (X,Y,Z) to Godot (X,Z,-Y)
static inline Vector3 to_godot(float x, float y, float z) {
	return Vector3(x, z, -y);
}

Vector<WaterPlane> WaterParser::parse(const String &p_path) {
	Vector<WaterPlane> planes;

	Ref<FileAccess> file = FileAccess::open(p_path, FileAccess::READ);
	if (file.is_null()) {
		UtilityFunctions::printerr("[WaterParser] Could not open: ", p_path);
		return planes;
	}

	String line = file->get_line().strip_edges();
	if (line != "processed") {
		UtilityFunctions::printerr("[WaterParser] Only 'processed' water.dat is supported");
		return planes;
	}

	while (!file->eof_reached()) {
		line = file->get_line().strip_edges();
		if (line.is_empty() || line.begins_with(";")) {
			continue;
		}

		// Split by spaces, removing empty entries
		PackedStringArray toks;
		PackedStringArray raw_toks = line.split(" ", false);
		for (int i = 0; i < raw_toks.size(); i++) {
			if (!raw_toks[i].is_empty()) {
				toks.push_back(raw_toks[i]);
			}
		}

		if (toks.size() == 29) {
			WaterPlane wp;
			wp.is_triangle = false;
			
			// Extract GTA coordinates and convert to Godot
			// Group 1
			wp.p1 = to_godot(toks[7 * 1 + 0].to_float(), toks[7 * 1 + 1].to_float(), toks[7 * 1 + 2].to_float());
			// Group 0
			wp.p2 = to_godot(toks[7 * 0 + 0].to_float(), toks[7 * 0 + 1].to_float(), toks[7 * 0 + 2].to_float());
			// Group 2
			wp.p3 = to_godot(toks[7 * 2 + 0].to_float(), toks[7 * 2 + 1].to_float(), toks[7 * 2 + 2].to_float());
			// Group 3
			wp.p4 = to_godot(toks[7 * 3 + 0].to_float(), toks[7 * 3 + 1].to_float(), toks[7 * 3 + 2].to_float());
			
			wp.mode = toks[28].to_int();
			planes.push_back(wp);
		} else if (toks.size() == 22) {
			WaterPlane wp;
			wp.is_triangle = true;
			
			// Group 2
			wp.p1 = to_godot(toks[7 * 2 + 0].to_float(), toks[7 * 2 + 1].to_float(), toks[7 * 2 + 2].to_float());
			// Group 0
			wp.p2 = to_godot(toks[7 * 0 + 0].to_float(), toks[7 * 0 + 1].to_float(), toks[7 * 0 + 2].to_float());
			// Group 1
			wp.p3 = to_godot(toks[7 * 1 + 0].to_float(), toks[7 * 1 + 1].to_float(), toks[7 * 1 + 2].to_float());
			
			wp.mode = toks[21].to_int();
			planes.push_back(wp);
		}
	}

	return planes;
}
