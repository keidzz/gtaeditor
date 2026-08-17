#include "gta_model_instance.h"
#include "map_material.h"

#include <godot_cpp/classes/collision_shape3d.hpp>
#include <godot_cpp/classes/node.hpp>
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
// Core resolve/spawn logic
// =============================================================================

void GTAModelInstance::refresh_model() {
	GtaResourceProvider *resources = GtaResourceProvider::get_singleton();
	if (!resources->ensure_loaded(gta_path)) {
		UtilityFunctions::printerr("[GTAModelInstance] '", get_name(),
				"': could not load GTA resources from '", gta_path, "'.");
		return;
	}

	String target_name = model_name.strip_edges().to_lower();
	if (target_name.ends_with(".dff")) {
		target_name = target_name.substr(0, target_name.length() - 4);
	}

	// Resolve the IDE definition if we can find one — it gives us the correct
	// txd_name/flags/draw_distance. Optional for props (falls back to a
	// name-based guess) but mandatory for the model_id path, since there's no
	// name to fall back to without it.
	ItemDefinition def;
	bool have_def = false;

	if (!target_name.is_empty()) {
		have_def = resources->find_definition_by_model_name(target_name, def);
	} else if (model_id >= 0) {
		have_def = resources->find_definition(model_id, def);
		if (!have_def) {
			UtilityFunctions::printerr("[GTAModelInstance] '", get_name(), "': model id ", model_id,
					" was not found among parsed IDE definitions. If this is a vehicle id, use "
					"GTAVehicleInstance instead, or set Model Name directly.");
			return;
		}
		target_name = def.model_name.to_lower();
	} else {
		// Nothing configured yet — nothing to do.
		return;
	}

	ModelCollection *models = resources->get_models();
	if (!models->has_model(target_name)) {
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
		mesh_instance->set_name(target_name);
		add_child(mesh_instance);
		// Deliberately NOT calling set_owner() here. A plain child added via
		// add_child() renders correctly and shows up in the Remote scene
		// tree, but — unlike an owned node — is never written into the
		// .tscn on save and doesn't linger as permanent scene content. That
		// set_owner() call used to be here (targeting
		// get_tree()->get_edited_scene_root()) and is the actual cause of
		// the "leftover MeshInstance3D after stopping the game" bug: since
		// GDExtension classes run _ready() in the editor too (not just
		// during Play), it was handing this generated node real, persistent
		// ownership of the live edited scene the moment the scene was
		// simply opened.
	}
	mesh_instance->set_mesh(mesh);

	// Texture dictionary: prefer the matched IDE definition's txd_name. If no
	// definition was found (typical for vehicles, since CARS may not be
	// parsed — see ide_parser.h), fall back to the model's own name — most
	// GTA SA props ship a same-named .txd, but this is a best-effort guess,
	// not a guarantee. For vehicles, prefer GTAVehicleInstance instead.
	String txd_name = have_def ? def.txd_name : target_name;
	uint32_t flags = have_def ? def.flags : 0;

	TextureCollection *textures = resources->get_textures();
	Vector<DffMaterial> materials = models->get_materials(target_name);
	for (int s = 0; s < mesh->get_surface_count() && s < materials.size(); s++) {
		Ref<StandardMaterial3D> mat = MapMaterial::create(materials[s], txd_name, flags, *textures, nullptr, true);
		if (mat.is_valid()) {
			mesh_instance->set_surface_override_material(s, mat);
		}
	}

	// -- Use Model Collision --
	if (use_model_collision) {
		ColModel col_model;
		if (models->get_col_model(target_name, col_model)) {
			if (collision_body == nullptr) {
				collision_body = memnew(StaticBody3D);
				mesh_instance->add_child(collision_body);
			} else {
				// Rebuild shapes in case refresh_model() re-ran with a different model.
				while (collision_body->get_child_count() > 0) {
					Node *child = collision_body->get_child(0);
					collision_body->remove_child(child);
					memdelete(child);
				}
			}
			for (int i = 0; i < col_model.shapes.size(); i++) {
				CollisionShape3D *col = memnew(CollisionShape3D);
				col->set_shape(col_model.shapes[i].shape);
				col->set_transform(col_model.shapes[i].transform);
				collision_body->add_child(col);
			}
		}
	} else if (collision_body != nullptr) {
		collision_body->queue_free();
		collision_body = nullptr;
	}

	// -- Use Streaming --
	// Fades the model out beyond its IDE draw_distance using the same
	// visibility_range mechanism MapBuilder uses for its own placements.
	// This does not swap to a separate LOD mesh (see the class doc comment
	// in gta_model_instance.h for why not).
	if (use_streaming) {
		float vis_end = have_def && def.draw_distance > 0.0f ? def.draw_distance : 300.0f;
		mesh_instance->set_visibility_range_end(vis_end);
		mesh_instance->set_visibility_range_end_margin(vis_end * 0.1f);
	} else {
		mesh_instance->set_visibility_range_end(0.0f);
		mesh_instance->set_visibility_range_end_margin(0.0f);
	}
}

// =============================================================================
// Property accessors
// =============================================================================

void GTAModelInstance::set_gta_path(const String &p_path) {
	gta_path = p_path;
	if (is_inside_tree()) {
		refresh_model();
	}
}
String GTAModelInstance::get_gta_path() const { return gta_path; }

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

void GTAModelInstance::set_use_model_collision(bool p_enabled) {
	use_model_collision = p_enabled;
	if (is_inside_tree()) {
		refresh_model();
	}
}
bool GTAModelInstance::get_use_model_collision() const { return use_model_collision; }

void GTAModelInstance::set_use_streaming(bool p_enabled) {
	use_streaming = p_enabled;
	if (is_inside_tree()) {
		refresh_model();
	}
}
bool GTAModelInstance::get_use_streaming() const { return use_streaming; }

// =============================================================================
// Binding
// =============================================================================

void GTAModelInstance::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_gta_path", "path"), &GTAModelInstance::set_gta_path);
	ClassDB::bind_method(D_METHOD("get_gta_path"), &GTAModelInstance::get_gta_path);
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "gta_path"), "set_gta_path", "get_gta_path");

	ClassDB::bind_method(D_METHOD("set_model_id", "id"), &GTAModelInstance::set_model_id);
	ClassDB::bind_method(D_METHOD("get_model_id"), &GTAModelInstance::get_model_id);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "model_id"), "set_model_id", "get_model_id");

	ClassDB::bind_method(D_METHOD("set_model_name", "name"), &GTAModelInstance::set_model_name);
	ClassDB::bind_method(D_METHOD("get_model_name"), &GTAModelInstance::get_model_name);
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "model_name"), "set_model_name", "get_model_name");

	ClassDB::bind_method(D_METHOD("set_use_model_collision", "enabled"), &GTAModelInstance::set_use_model_collision);
	ClassDB::bind_method(D_METHOD("get_use_model_collision"), &GTAModelInstance::get_use_model_collision);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "use_model_collision"), "set_use_model_collision", "get_use_model_collision");

	ClassDB::bind_method(D_METHOD("set_use_streaming", "enabled"), &GTAModelInstance::set_use_streaming);
	ClassDB::bind_method(D_METHOD("get_use_streaming"), &GTAModelInstance::get_use_streaming);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "use_streaming"), "set_use_streaming", "get_use_streaming");

	ClassDB::bind_method(D_METHOD("refresh_model"), &GTAModelInstance::refresh_model);
}

} // namespace godot
