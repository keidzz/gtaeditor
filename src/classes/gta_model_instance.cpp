#include "gta_model_instance.h"
#include "map_material.h"

#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

namespace godot {

// =============================================================================
// Constructor / Destructor
// =============================================================================

GTAModelInstance::GTAModelInstance() {}
GTAModelInstance::~GTAModelInstance() {}

// =============================================================================
// Godot lifecycle
// =============================================================================

void GTAModelInstance::_ready() {
	refresh_model();
}

// =============================================================================
// MapBuilder lookup
// =============================================================================

MapBuilder *GTAModelInstance::find_map_builder() const {
	if (map_builder_path.is_empty()) {
		return nullptr;
	}
	Node *node = get_node_or_null(map_builder_path);
	return Object::cast_to<MapBuilder>(node);
}

// =============================================================================
// Core resolve/spawn logic
// =============================================================================

void GTAModelInstance::refresh_model() {
	MapBuilder *builder = find_map_builder();
	if (builder == nullptr) {
		UtilityFunctions::printerr("[GTAModelInstance] '", get_name(),
				"': map_builder_path is not set to a valid MapBuilder node.");
		return;
	}

	// MapBuilder loads its data in _ready(), and scene-tree readiness order
	// between siblings isn't something we want to depend on. If it hasn't
	// finished yet, wait for its map_loaded signal and try again once.
	if (!builder->is_loaded()) {
		if (!waiting_for_map) {
			waiting_for_map = true;
			builder->connect("map_loaded", Callable(this, "refresh_model"), Object::CONNECT_ONE_SHOT);
		}
		return;
	}
	waiting_for_map = false;

	String target_name = model_name.strip_edges().to_lower();
	if (target_name.ends_with(".dff")) {
		target_name = target_name.substr(0, target_name.length() - 4);
	}

	// Resolve the IDE definition if we can find one — it gives us the correct
	// txd_name/flags for texturing. This is optional for props (falls back to
	// a name-based guess) but currently mandatory for the model_id path,
	// since there's no name to fall back to without it.
	ItemDefinition def;
	bool have_def = false;

	if (!target_name.is_empty()) {
		have_def = builder->find_definition_by_model_name(target_name, def);
	} else if (model_id >= 0) {
		have_def = builder->find_definition(model_id, def);
		if (!have_def) {
			UtilityFunctions::printerr("[GTAModelInstance] '", get_name(), "': model id ", model_id,
					" was not found among parsed IDE definitions (OBJS/TOBJ/ANIM sections). ",
					"Vehicle ids from the CARS section aren't indexed yet — set Model Name instead.");
			return;
		}
		target_name = def.model_name.to_lower();
	} else {
		// Nothing configured yet — nothing to do.
		return;
	}

	ModelCollection *models = builder->get_model_collection();
	if (models == nullptr || !models->has_model(target_name)) {
		UtilityFunctions::printerr("[GTAModelInstance] '", get_name(), "': model '", target_name,
				"' was not found in the loaded IMG archive.");
		return;
	}

	// Lazy-parses the DFF on first request; subsequent requests (from this or
	// any other GTAModelInstance/MapBuilder placement using the same model)
	// hit ModelCollection's cache instead of re-parsing.
	Ref<ArrayMesh> mesh = models->get_mesh(target_name);
	if (mesh.is_null() || mesh->get_surface_count() == 0) {
		UtilityFunctions::printerr("[GTAModelInstance] '", get_name(), "': model '", target_name,
				"' produced an empty mesh.");
		return;
	}

	if (mesh_instance == nullptr) {
		mesh_instance = memnew(MeshInstance3D);
		mesh_instance->set_name("MeshInstance3D");
		add_child(mesh_instance);

		// Keep the generated child visible in the editor and saved with the
		// scene. Prefer the edited scene root (editor context); fall back to
		// this node's own owner (runtime-instantiated scenes).
		Node *edited_root = get_tree() != nullptr ? get_tree()->get_edited_scene_root() : nullptr;
		mesh_instance->set_owner(edited_root != nullptr ? edited_root : get_owner());
	}
	mesh_instance->set_mesh(mesh);

	// Texture dictionary: prefer the matched IDE definition's txd_name. If no
	// definition was found (typical for vehicles, since CARS isn't parsed),
	// fall back to the model's own name — most GTA SA vehicles and many props
	// ship a same-named .txd, but this is a best-effort guess, not a guarantee.
	String txd_name = have_def ? def.txd_name : target_name;
	uint32_t flags = have_def ? def.flags : 0;

	TextureCollection *textures = builder->get_texture_collection();
	if (textures != nullptr) {
		Vector<DffMaterial> materials = models->get_materials(target_name);
		for (int s = 0; s < mesh->get_surface_count() && s < materials.size(); s++) {
			Ref<StandardMaterial3D> mat = MapMaterial::create(materials[s], txd_name, flags, *textures);
			if (mat.is_valid()) {
				mesh_instance->set_surface_override_material(s, mat);
			}
		}
	}
}

// =============================================================================
// Property accessors
// =============================================================================

void GTAModelInstance::set_map_builder_path(const NodePath &p_path) {
	map_builder_path = p_path;
	if (is_inside_tree()) {
		refresh_model();
	}
}
NodePath GTAModelInstance::get_map_builder_path() const { return map_builder_path; }

void GTAModelInstance::set_model_id(int32_t p_id) {
	model_id = p_id;
	if (is_inside_tree()) {
		refresh_model();
	}
}
int32_t GTAModelInstance::get_model_id() const { return model_id; }

void GTAModelInstance::set_model_name(const String &p_name) {
	model_name = p_name;
	if (is_inside_tree()) {
		refresh_model();
	}
}
String GTAModelInstance::get_model_name() const { return model_name; }

// =============================================================================
// Binding
// =============================================================================

void GTAModelInstance::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_map_builder_path", "path"), &GTAModelInstance::set_map_builder_path);
	ClassDB::bind_method(D_METHOD("get_map_builder_path"), &GTAModelInstance::get_map_builder_path);
	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "map_builder_path", PROPERTY_HINT_NODE_PATH_VALID_TYPES, "MapBuilder"),
			"set_map_builder_path", "get_map_builder_path");

	ClassDB::bind_method(D_METHOD("set_model_id", "id"), &GTAModelInstance::set_model_id);
	ClassDB::bind_method(D_METHOD("get_model_id"), &GTAModelInstance::get_model_id);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "model_id"), "set_model_id", "get_model_id");

	ClassDB::bind_method(D_METHOD("set_model_name", "name"), &GTAModelInstance::set_model_name);
	ClassDB::bind_method(D_METHOD("get_model_name"), &GTAModelInstance::get_model_name);
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "model_name"), "set_model_name", "get_model_name");

	ClassDB::bind_method(D_METHOD("refresh_model"), &GTAModelInstance::refresh_model);
}

} // namespace godot
