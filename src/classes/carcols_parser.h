#ifndef CARCOLS_PARSER_H
#define CARCOLS_PARSER_H

#include <godot_cpp/templates/hash_map.hpp>
#include <godot_cpp/templates/vector.hpp>
#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/variant/string.hpp>

namespace godot {

// =============================================================================
// CarColsData — parsed GTA SA data/carcols.dat.
//
// The file has three sections: `col` (the color palette, index = line order),
// `car` (per-vehicle 2-color combos: primary, secondary), and `car4`
// (per-vehicle 4-color combos: primary, secondary, tertiary, quaternary).
// Sections end with `end`. `#` starts a comment (the palette entries' trailing
// comments carry the human-readable color names, e.g. "# 7 striking blue").
// =============================================================================

struct CarColorCombo {
	Color colors[4];
	int color_count = 0;
};

struct CarColsData {
	Vector<Color> palette;
	// Keyed by lowercase model name ("savanna", "admiral", ...).
	HashMap<String, Vector<CarColorCombo>> vehicle_combos;

	// Parses p_path (an absolute or res:// path). Returns an empty
	// CarColsData (with a logged error) if the file can't be opened.
	static CarColsData parse(const String &p_path);
};

} // namespace godot

#endif // CARCOLS_PARSER_H