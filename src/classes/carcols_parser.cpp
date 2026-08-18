#include "carcols_parser.h"

#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

namespace godot {

CarColsData CarColsData::parse(const String &p_path) {
	CarColsData data;

	Ref<FileAccess> file = FileAccess::open(p_path, FileAccess::READ);
	if (file.is_null()) {
		UtilityFunctions::printerr("[CarColsData] Could not open: ", p_path);
		return data;
	}

	enum Section { SECTION_NONE, SECTION_COL, SECTION_CAR, SECTION_CAR4 };
	Section section = SECTION_NONE;

	while (!file->eof_reached()) {
		String line = file->get_line().strip_edges();
		if (line.is_empty() || line.begins_with("#")) {
			continue;
		}

		// Drop trailing comments ("0,0,0		# 0 black	black").
		int hash = line.find("#");
		if (hash >= 0) {
			line = line.substr(0, hash).strip_edges();
		}
		if (line.is_empty()) {
			continue;
		}

		String lower = line.to_lower();
		if (lower == "col") {
			section = SECTION_COL;
			continue;
		}
		if (lower == "car") {
			section = SECTION_CAR;
			continue;
		}
		if (lower == "car4") {
			section = SECTION_CAR4;
			continue;
		}
		if (lower == "end") {
			section = SECTION_NONE;
			continue;
		}

		PackedStringArray parts = line.split(",", false);
		if (parts.is_empty()) {
			continue;
		}

		if (section == SECTION_COL) {
			// "<r>,<g>,<b>" — palette index is implicit by line order.
			if (parts.size() >= 3) {
				Color c(
						float(parts[0].to_int()) / 255.0f,
						float(parts[1].to_int()) / 255.0f,
						float(parts[2].to_int()) / 255.0f, 1.0f);
				data.palette.push_back(c);
			}
		} else if (section == SECTION_CAR || section == SECTION_CAR4) {
			// "<model>, <p>,<s>, <p>,<s>, ..." (car) or
			// "<model>, <p>,<s>,<t>,<q>, ..." (car4).
			String model = parts[0].strip_edges().to_lower();
			int per_combo = section == SECTION_CAR4 ? 4 : 2;
			Vector<CarColorCombo> &combos = data.vehicle_combos[model];

			for (int i = 1; i + per_combo - 1 < parts.size(); i += per_combo) {
				CarColorCombo combo;
				for (int k = 0; k < per_combo; k++) {
					int idx = parts[i + k].strip_edges().to_int();
					if (idx >= 0 && idx < data.palette.size()) {
						combo.colors[k] = data.palette[idx];
					} else {
						combo.colors[k] = Color(0.4f, 0.4f, 0.4f); // Fallback gray.
					}
				}
				combo.color_count = per_combo;
				combos.push_back(combo);
			}
		}
	}

	return data;
}

} // namespace godot