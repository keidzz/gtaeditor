#ifndef IDE_PARSER_H
#define IDE_PARSER_H

#include "item_definition.h"

#include <godot_cpp/templates/hash_map.hpp>
#include <godot_cpp/templates/vector.hpp>
#include <godot_cpp/variant/string.hpp>

using namespace godot;

// =============================================================================
// IdeParser — Parses GTA IDE (Item Definition) files.
//
// IDE files define object types with sections:
//   OBJS: ID, ModelName, TextureName, ObjectCount?, DrawDist, Flags
//   TOBJ: Same as OBJS + TimeOn, TimeOff
//   ANIM: ID, ModelName, TextureName, AnimName, DrawDist, Flags
//   CARS: ID, ModelName, TextureName, ... (vehicle-type-specific fields).
//         Only ID/ModelName/TextureName are parsed — enough for
//         GTAVehicleInstance/GTAModelInstance's numeric model_id lookups to
//         resolve vehicles; draw_distance/flags are set to a flat fallback
//         since CARS doesn't define per-vehicle equivalents of those.
//   TXDP: TextureName, TextureParentName
//   2DFX: Effect definitions attached to object definitions
//
// Lines between section headers and "end" are parsed per-section.
// Comment character is '#'.
//
// Reference: http://www.gtamodding.com/wiki/Item_Definition
// =============================================================================

struct IdeResult {
	HashMap<int32_t, ItemDefinition> definitions;
	Vector<TextureParent> texture_parents;
};

class IdeParser {
public:
	// Parse an IDE file at the given absolute path.
	static IdeResult parse(const String &p_absolute_path);
};

#endif // IDE_PARSER_H
