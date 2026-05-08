#include "dat_parser.h"

#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

// =============================================================================
// DatParser implementation
// =============================================================================

DatResult DatParser::parse(const String &p_absolute_path) {
	DatResult result;

	Ref<FileAccess> file = FileAccess::open(p_absolute_path, FileAccess::READ);
	if (file.is_null()) {
		UtilityFunctions::printerr("[DatParser] Failed to open: ", p_absolute_path);
		return result;
	}

	while (!file->eof_reached()) {
		String line = file->get_line().strip_edges();

		// Skip empty lines and comments.
		if (line.is_empty() || line.begins_with("#")) {
			continue;
		}

		// Split into keyword + path. Format: "KEYWORD PATH"
		// Use split with max 2 parts to handle paths with spaces (unlikely but safe).
		PackedStringArray tokens = line.split(" ", false, 2);
		if (tokens.size() < 2) {
			continue;
		}

		String keyword = tokens[0].to_upper();
		String path = tokens[1].strip_edges();

		if (keyword == "IDE") {
			result.ide_paths.push_back(path);
		} else if (keyword == "IPL" || keyword == "MAPZONE") {
			result.ipl_paths.push_back(path);
		} else if (keyword == "IMG") {
			result.img_paths.push_back(path);
		} else if (keyword == "TEXDICTION") {
			result.txd_paths.push_back(path);
		}
		// SPLASH, COLFILE, MODELFILE — ignored for map loading.
	}

	UtilityFunctions::print("[DatParser] Parsed ", p_absolute_path,
			" — IDE:", result.ide_paths.size(),
			" IPL:", result.ipl_paths.size(),
			" IMG:", result.img_paths.size());

	return result;
}
