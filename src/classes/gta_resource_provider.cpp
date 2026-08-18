#include "gta_resource_provider.h"

#include "dat_parser.h"
#include "ide_parser.h"

#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

// =============================================================================
// GtaResourceProvider implementation
// =============================================================================

GtaResourceProvider *GtaResourceProvider::get_singleton() {
	static GtaResourceProvider instance;
	return &instance;
}

bool GtaResourceProvider::ensure_loaded(const String &p_gta_path) {
	if (loaded) {
		if (p_gta_path != loaded_gta_path) {
			UtilityFunctions::printerr("[GtaResourceProvider] Already loaded from '", loaded_gta_path,
					"' — ignoring different gta_path '", p_gta_path,
					"'. All GTAModelInstance/GTAVehicleInstance/MapBuilder nodes in a session share one loaded game path.");
		}
		return true;
	}

	// Resolve GTA path to absolute (same logic as MapBuilder::load_map()).
	String abs_gta_path;
	if (p_gta_path.begins_with("res://")) {
		abs_gta_path = ProjectSettings::get_singleton()->globalize_path(p_gta_path);
	} else {
		abs_gta_path = p_gta_path;
	}
	if (!abs_gta_path.ends_with("/")) {
		abs_gta_path += "/";
	}

	path_resolver.set_root(abs_gta_path);
	loaded_gta_path = p_gta_path;

	UtilityFunctions::print("[GtaResourceProvider] GTA path: ", abs_gta_path);

	// 1. Load IMG archives (models/gta3.img, models/gta_int.img).
	String img_path = path_resolver.resolve("models/gta3.img");
	if (img_path.is_empty()) {
		UtilityFunctions::printerr("[GtaResourceProvider] Could not find models/gta3.img");
		return false;
	}
	img_archive.load(img_path);

	// gta_int.img holds the interior models (gen_int/sweets/cjhs/etc.).
	String interior_img_path = path_resolver.resolve("models/gta_int.img");
	if (interior_img_path.is_empty()) {
		UtilityFunctions::printerr("[GtaResourceProvider] Could not find models/gta_int.img");
		return false;
	}
	interior_img_archive.load(interior_img_path);

	// 2. Parse default.dat.
	String default_dat_path = path_resolver.resolve("data/default.dat");
	if (!default_dat_path.is_empty()) {
		load_dat_file(default_dat_path);
	}

	// 3. Parse gta.dat.
	String gta_dat_path = path_resolver.resolve("data/gta.dat");
	if (!gta_dat_path.is_empty()) {
		load_dat_file(gta_dat_path);
	}

	// 4. Index all DFF/TXD/COL entries from the IMG archive.
	index_img_assets();

	loaded = true;
	UtilityFunctions::print("[GtaResourceProvider] Ready — ", definitions.size(), " IDE definitions, ",
			models.get_model_count(), " models, ", textures.get_txd_count(), " texture dictionaries.");
	return true;
}

bool GtaResourceProvider::find_definition(int32_t p_id, ItemDefinition &r_definition) {
	if (!definitions.has(p_id)) {
		return false;
	}
	r_definition = definitions[p_id];
	return true;
}

const ItemDefinition *GtaResourceProvider::find_definition_ptr(int32_t p_id) {
	if (!definitions.has(p_id)) {
		return nullptr;
	}
	return &definitions[p_id];
}

bool GtaResourceProvider::find_definition_by_model_name(const String &p_model_name, ItemDefinition &r_definition) {
	String target = p_model_name.to_lower();
	for (const KeyValue<int32_t, ItemDefinition> &kv : definitions) {
		if (kv.value.model_name.to_lower() == target) {
			r_definition = kv.value;
			return true;
		}
	}
	return false;
}

void GtaResourceProvider::load_dat_file(const String &p_dat_path) {
	DatResult dat = DatParser::parse(p_dat_path);
	for (int i = 0; i < dat.ide_paths.size(); i++) {
		load_ide_file(dat.ide_paths[i]);
	}
}

void GtaResourceProvider::load_ide_file(const String &p_ide_path) {
	String resolved = path_resolver.resolve(p_ide_path);
	if (resolved.is_empty()) {
		return;
	}

	IdeResult result = IdeParser::parse(resolved);

	// Merge definitions.
	for (const KeyValue<int32_t, ItemDefinition> &kv : result.definitions) {
		definitions[kv.key] = kv.value;
	}

	// Register texture parents.
	for (int i = 0; i < result.texture_parents.size(); i++) {
		textures.add_parent(result.texture_parents[i].child_name,
				result.texture_parents[i].parent_name);
	}
}

void GtaResourceProvider::index_img_assets() {
	// Register all DFF files from the IMG archives.
	Vector<String> dff_entries = img_archive.get_entries_with_extension(".dff");
	for (int i = 0; i < dff_entries.size(); i++) {
		models.register_dff(dff_entries[i], &img_archive);
	}
	Vector<String> int_dff_entries = interior_img_archive.get_entries_with_extension(".dff");
	for (int i = 0; i < int_dff_entries.size(); i++) {
		models.register_dff(int_dff_entries[i], &interior_img_archive);
	}

	// Register all TXD files from the IMG archives.
	Vector<String> txd_entries = img_archive.get_entries_with_extension(".txd");
	for (int i = 0; i < txd_entries.size(); i++) {
		textures.register_txd(txd_entries[i], &img_archive);
	}
	Vector<String> int_txd_entries = interior_img_archive.get_entries_with_extension(".txd");
	for (int i = 0; i < int_txd_entries.size(); i++) {
		textures.register_txd(int_txd_entries[i], &interior_img_archive);
	}

	// Parse all COL files from the IMG archives.
	Vector<String> col_entries = img_archive.get_entries_with_extension(".col");
	for (int i = 0; i < col_entries.size(); i++) {
		PackedByteArray data = img_archive.read_entry(col_entries[i]);
		if (!data.is_empty()) {
			models.load_col_bytes(data, col_entries[i]);
		}
	}
	Vector<String> int_col_entries = interior_img_archive.get_entries_with_extension(".col");
	for (int i = 0; i < int_col_entries.size(); i++) {
		PackedByteArray data = interior_img_archive.read_entry(int_col_entries[i]);
		if (!data.is_empty()) {
			models.load_col_bytes(data, int_col_entries[i]);
		}
	}
}
