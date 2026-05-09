#include "model_collection.h"

#include <godot_cpp/variant/utility_functions.hpp>

// =============================================================================
// ModelCollection implementation
// =============================================================================

void ModelCollection::register_dff(const String &p_dff_name, const ImgArchive *p_archive) {
	String key = p_dff_name.to_lower();
	// Strip .dff extension if present.
	if (key.ends_with(".dff")) {
		key = key.substr(0, key.length() - 4);
	}

	DffEntry entry;
	entry.dff_name = key;
	entry.archive = p_archive;
	entry.loaded = false;
	dff_entries[key] = entry;
}

bool ModelCollection::has_model(const String &p_name) const {
	return dff_entries.has(p_name.to_lower());
}

Ref<ArrayMesh> ModelCollection::get_mesh(const String &p_name) {
	String key = p_name.to_lower();
	if (!dff_entries.has(key)) {
		return Ref<ArrayMesh>();
	}

	DffEntry &entry = dff_entries[key];
	ensure_loaded(entry);
	return entry.result.mesh;
}

void ModelCollection::load_col_file(const String &p_absolute_path) {
	HashMap<String, ColModel> parsed = ColParser::parse(p_absolute_path);
	for (const KeyValue<String, ColModel> &kv : parsed) {
		col_models[kv.key] = kv.value;
	}
}

void ModelCollection::load_col_bytes(const PackedByteArray &p_bytes, const String &p_name) {
	HashMap<String, ColModel> parsed = ColParser::parse_bytes(p_bytes, p_name);
	for (const KeyValue<String, ColModel> &kv : parsed) {
		col_models[kv.key] = kv.value;
	}
}

bool ModelCollection::get_col_model(const String &p_name, ColModel &r_model) {
	String key = p_name.to_lower();

	if (col_models.has(key)) {
		r_model = col_models[key];
		return true;
	}

	// No fallback! If a map object doesn't have a corresponding .col entry,
	// it is intentionally non-colliding in GTA SA (like cables, grass, etc.).
	return false;
}

Vector<DffMaterial> ModelCollection::get_materials(const String &p_name) {
	String key = p_name.to_lower();
	if (!dff_entries.has(key)) {
		return Vector<DffMaterial>();
	}

	DffEntry &entry = dff_entries[key];
	ensure_loaded(entry);
	return entry.result.materials;
}

int ModelCollection::get_model_count() const {
	return dff_entries.size();
}

void ModelCollection::clear() {
	dff_entries.clear();
	col_models.clear();
}

void ModelCollection::ensure_loaded(DffEntry &entry) {
	if (entry.loaded) {
		return;
	}
	entry.loaded = true;

	if (entry.archive == nullptr) {
		return;
	}

	// Read DFF data from IMG archive and parse it.
	String dff_filename = entry.dff_name + ".dff";
	if (!entry.archive->has_entry(dff_filename)) {
		return;
	}

	PackedByteArray data = entry.archive->read_entry(dff_filename);
	if (data.is_empty()) {
		return;
	}

	entry.result = DffParser::parse(data);
}
