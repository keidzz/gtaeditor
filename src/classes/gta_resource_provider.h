#ifndef GTA_RESOURCE_PROVIDER_H
#define GTA_RESOURCE_PROVIDER_H

#include "gta_path_resolver.h"
#include "img_archive.h"
#include "item_definition.h"
#include "model_collection.h"
#include "texture_collection.h"

#include <godot_cpp/templates/hash_map.hpp>
#include <godot_cpp/variant/string.hpp>

using namespace godot;

// =============================================================================
// GtaResourceProvider — process-wide, lazily-initialized owner of the shared
// GTA data: the IMG archive index, ModelCollection, TextureCollection, and
// parsed IDE definitions.
//
// This is the piece that lets GTAModelInstance/GTAVehicleInstance be fully
// self-contained: whichever one of them (or MapBuilder) asks first pays the
// one-time cost of resolving the game path, parsing gta.dat/default.dat and
// every referenced IDE file, and indexing the IMG archive's DFF/TXD/COL
// entries. Everyone after that reuses the same loaded data — no duplicate
// parsing, and no requirement that a MapBuilder node exist in the scene.
//
// Not a Node; not registered with ClassDB. Plain internal C++ singleton,
// analogous in spirit to ModelCollection/TextureCollection's own lazy caches.
// =============================================================================

class GtaResourceProvider {
public:
	static GtaResourceProvider *get_singleton();

	// Resolves p_gta_path, loads models/gta3.img, parses default.dat/gta.dat
	// and every IDE file they reference, and indexes DFF/TXD/COL entries.
	// Safe to call repeatedly/from multiple nodes — a no-op once loaded.
	// Returns false only if the game path/IMG archive couldn't be resolved.
	bool ensure_loaded(const String &p_gta_path);

	bool is_loaded() const { return loaded; }

	ModelCollection *get_models() { return &models; }
	TextureCollection *get_textures() { return &textures; }

	// MapBuilder still needs these directly for its own placement (IPL/COL/
	// LOD) loading, which stays MapBuilder's responsibility since only
	// MapBuilder builds a `placements` array.
	ImgArchive *get_img_archive() { return &img_archive; }
	ImgArchive *get_interior_img_archive() { return &interior_img_archive; }
	GtaPathResolver *get_path_resolver() { return &path_resolver; }
	const HashMap<int32_t, ItemDefinition> &get_all_definitions() const { return definitions; }

	// Looks up a parsed IDE definition by numeric id (OBJS/TOBJ/ANIM, and
	// CARS if IdeParser is parsing that section — see ide_parser.h).
	bool find_definition(int32_t p_id, ItemDefinition &r_definition);

	// Zero-copy variant for hot paths (e.g. MapBuilder::spawn_placement(),
	// called on every streamed-in object). Returns nullptr if not found.
	// Safe to hold onto: `definitions` is only ever populated during
	// ensure_loaded() and never modified afterward, so this pointer stays
	// valid for the lifetime of the singleton (i.e. the whole process).
	const ItemDefinition *find_definition_ptr(int32_t p_id);

	// Looks up a parsed IDE definition by model (.dff) name, case-insensitive.
	bool find_definition_by_model_name(const String &p_model_name, ItemDefinition &r_definition);

private:
	GtaPathResolver path_resolver;
	ImgArchive img_archive;
	ImgArchive interior_img_archive;
	ModelCollection models;
	TextureCollection textures;
	HashMap<int32_t, ItemDefinition> definitions;

	bool loaded = false;
	String loaded_gta_path;

	void load_dat_file(const String &p_dat_path);
	void load_ide_file(const String &p_ide_path);
	void index_img_assets();
};

#endif // GTA_RESOURCE_PROVIDER_H
