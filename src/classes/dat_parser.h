#ifndef DAT_PARSER_H
#define DAT_PARSER_H

#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/templates/vector.hpp>

using namespace godot;

// =============================================================================
// DatParser — Parses GTA's .dat files (default.dat, gta.dat).
// Each non-comment line has format: KEYWORD PATH
// Comment character is '#'.
//
// Collects lists of IDE, IPL, and IMG paths referenced by the .dat file.
// =============================================================================

struct DatResult {
	Vector<String> ide_paths;
	Vector<String> ipl_paths;
	Vector<String> img_paths;
	Vector<String> txd_paths;
};

class DatParser {
public:
	// Parse a .dat file at the given absolute path.
	// Returns the collected IDE/IPL/IMG/TXD paths (as raw GTA-style paths).
	static DatResult parse(const String &p_absolute_path);
};

#endif // DAT_PARSER_H
