#include "ide_parser.h"

#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

// =============================================================================
// IdeParser implementation
// =============================================================================

// Internal section types for the IDE state machine.
enum class IdeSection {
	NONE,
	OBJS,
	TOBJ,
	ANIM,
	CARS,
	PEDS,
	WEAP,
	TXDP,
	_2DFX,
	UNKNOWN,
};

// Helper: split an IDE line by commas and spaces, trimming whitespace.
static PackedStringArray split_ide_line(const String &p_line) {
	// IDE lines use ", " as separator (comma followed by space).
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

// Helper: parse the section header string to an enum value.
static IdeSection parse_section_name(const String &p_name) {
	String lower = p_name.to_lower().strip_edges();
	if (lower == "objs") return IdeSection::OBJS;
	if (lower == "tobj") return IdeSection::TOBJ;
	if (lower == "anim") return IdeSection::ANIM;
	if (lower == "cars") return IdeSection::CARS;
	if (lower == "peds") return IdeSection::PEDS;
	if (lower == "weap") return IdeSection::WEAP;
	if (lower == "txdp") return IdeSection::TXDP;
	if (lower == "2dfx") return IdeSection::_2DFX;
	return IdeSection::UNKNOWN;
}

IdeResult IdeParser::parse(const String &p_absolute_path) {
	IdeResult result;

	Ref<FileAccess> file = FileAccess::open(p_absolute_path, FileAccess::READ);
	if (file.is_null()) {
		UtilityFunctions::printerr("[IdeParser] Failed to open: ", p_absolute_path);
		return result;
	}

	IdeSection current_section = IdeSection::NONE;

	while (!file->eof_reached()) {
		String line = file->get_line().strip_edges();

		// Skip empty lines and comments.
		if (line.is_empty() || line.begins_with("#")) {
			continue;
		}

		// Check for section end.
		if (line.to_lower() == "end") {
			current_section = IdeSection::NONE;
			continue;
		}

		// If we're not in a section, this line starts a new section.
		if (current_section == IdeSection::NONE) {
			current_section = parse_section_name(line);
			continue;
		}

		// Parse the line based on the current section.
		PackedStringArray tokens = split_ide_line(line);
		if (tokens.is_empty()) {
			continue;
		}

		switch (current_section) {
			case IdeSection::OBJS: {
				// OBJS: ID, ModelName, TextureName, ObjectCount?, DrawDist, [DrawDist2..], Flags
				if (tokens.size() < 5) break;

				ItemDefinition def;
				def.id = tokens[0].to_int();
				def.model_name = tokens[1];
				def.txd_name = tokens[2];

				// If 5 tokens: ObjectCount=1 implied, DrawDist=tokens[3], Flags=tokens[4]
				// If 6+ tokens: ObjectCount=tokens[3], DrawDist=tokens[4], Flags=last
				if (tokens.size() == 5) {
					def.draw_distance = tokens[3].to_float();
					def.flags = static_cast<uint32_t>(tokens[4].to_int());
				} else {
					def.draw_distance = tokens[4].to_float();
					def.flags = static_cast<uint32_t>(tokens[tokens.size() - 1].to_int());
				}

				result.definitions[def.id] = def;
			} break;

			case IdeSection::TOBJ: {
				// TOBJ: ID, ModelName, TextureName, ObjectCount?, DrawDist, [DrawDist2..], Flags, TimeOn, TimeOff
				if (tokens.size() < 7) break;

				ItemDefinition def;
				def.id = tokens[0].to_int();
				def.model_name = tokens[1];
				def.txd_name = tokens[2];

				if (tokens.size() == 7) {
					def.draw_distance = tokens[3].to_float();
					def.flags = static_cast<uint32_t>(tokens[4].to_int());
					def.time_on = tokens[5].to_int();
					def.time_off = tokens[6].to_int();
				} else {
					def.draw_distance = tokens[4].to_float();
					def.flags = static_cast<uint32_t>(tokens[tokens.size() - 3].to_int());
					def.time_on = tokens[tokens.size() - 2].to_int();
					def.time_off = tokens[tokens.size() - 1].to_int();
				}

				result.definitions[def.id] = def;
			} break;

			case IdeSection::ANIM: {
				// ANIM (SA only): ID, ModelName, TextureName, AnimName, DrawDist, Flags
				if (tokens.size() < 6) break;

				ItemDefinition def;
				def.id = tokens[0].to_int();
				def.model_name = tokens[1];
				def.txd_name = tokens[2];
				// tokens[3] = AnimName (ignored for map loading)
				def.draw_distance = tokens[4].to_float();
				def.flags = static_cast<uint32_t>(tokens[5].to_int());

				result.definitions[def.id] = def;
			} break;

			case IdeSection::TXDP: {
				// TXDP: TextureName, TextureParentName
				if (tokens.size() < 2) break;

				TextureParent tp;
				tp.child_name = tokens[0].to_lower();
				tp.parent_name = tokens[1].to_lower();
				result.texture_parents.push_back(tp);
			} break;

			default:
				// CARS, PEDS, WEAP, 2DFX, UNKNOWN — skip for map loading.
				break;
		}
	}

	return result;
}
