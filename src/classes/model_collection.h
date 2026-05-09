#ifndef MODEL_COLLECTION_H
#define MODEL_COLLECTION_H

#include "../rw/dff_parser.h"
#include "col_parser.h"
#include "img_archive.h"

#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/concave_polygon_shape3d.hpp>
#include <godot_cpp/templates/hash_map.hpp>
#include <godot_cpp/variant/string.hpp>

using namespace godot;

// =============================================================================
// ModelCollection — Manages all DFF model files with lazy loading.
// Models are only parsed from the IMG archive when first requested.
// =============================================================================

class ModelCollection {
public:
	// Register a DFF entry from the IMG archive (stores name only, no parsing).
	void register_dff(const String &p_dff_name, const ImgArchive *p_archive);

	// Check if a model is registered.
	bool has_model(const String &p_name) const;

	// Get the parsed mesh for a model. Parses the DFF on first access.
	Ref<ArrayMesh> get_mesh(const String &p_name);

	// Load a global COLFILE archive containing multiple collision models.
	void load_col_file(const String &p_absolute_path);
	void load_col_bytes(const PackedByteArray &p_bytes, const String &p_name);

	// Get the collision model for a name (checks COLFILEs, then IMG .col, then generates fallback).
	bool get_col_model(const String &p_name, ColModel &r_model);

	// Get material info for a model. Must call get_mesh() first.
	Vector<DffMaterial> get_materials(const String &p_name);

	// Stats.
	int get_model_count() const;

	// Free all models.
	void clear();

private:
	struct DffEntry {
		String dff_name;
		const ImgArchive *archive = nullptr;
		bool loaded = false;
		DffResult result;
	};

	HashMap<String, DffEntry> dff_entries;
	HashMap<String, ColModel> col_models;

	// Ensure a DFF is loaded (parse if needed).
	void ensure_loaded(DffEntry &entry);
};

#endif // MODEL_COLLECTION_H
