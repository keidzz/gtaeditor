#include "gta_vehicle_instance.h"

#include <godot_cpp/classes/collision_shape3d.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

namespace godot {

// =============================================================================
// Constructor / Destructor
// =============================================================================

GTAVehicleInstance::GTAVehicleInstance() {}
GTAVehicleInstance::~GTAVehicleInstance() {}

// =============================================================================
// Godot lifecycle
// =============================================================================

void GTAVehicleInstance::_ready() {
	refresh_vehicle();
}

// =============================================================================
// Frame filtering
// =============================================================================

bool GTAVehicleInstance::should_skip_frame(const String &p_frame_name) {
	String n = p_frame_name.to_lower();
	// Damaged-part variants -- SA vehicle DFFs carry both the undamaged and
	// damaged geometry for most panels (doors/bonnet/boot/bumpers/wings),
	// parented under the same dummy. Without filtering these out, both
	// versions render overlapped, which is what reads as "doubled"/LOD-like
	// geometry on vehicles.
	if (n.ends_with("_dam") || n.ends_with("_dam_l1") || n.ends_with("_dam_l0")) {
		return true;
	}
	// Embedded LOD duplicates some objects carry in the same clump (see the
	// analogous check in DffParser for non-vehicle two-clump objects).
	if (n.ends_with("_l1") || n.ends_with("_l2") || n.ends_with("_vlo")) {
		return true;
	}
	return false;
}

// =============================================================================
// Teardown
// =============================================================================

void GTAVehicleInstance::clear_parts() {
	for (int i = 0; i < part_nodes.size(); i++) {
		if (part_nodes[i] != nullptr) {
			part_nodes[i]->queue_free();
		}
	}
	part_nodes.clear();

	if (collision_body != nullptr) {
		collision_body->queue_free();
		collision_body = nullptr;
	}
}

// =============================================================================
// Core resolve/spawn logic
// =============================================================================

void GTAVehicleInstance::refresh_vehicle() {
	GtaResourceProvider *resources = GtaResourceProvider::get_singleton();
	if (!resources->ensure_loaded(gta_path)) {
		UtilityFunctions::printerr("[GTAVehicleInstance] '", get_name(),
				"': could not load GTA resources from '", gta_path, "'.");
		return;
	}

	String target_name = model_name.strip_edges().to_lower();
	if (target_name.ends_with(".dff")) {
		target_name = target_name.substr(0, target_name.length() - 4);
	}

	ItemDefinition def;
	bool have_def = false;

	if (!target_name.is_empty()) {
		have_def = resources->find_definition_by_model_name(target_name, def);
	} else if (model_id >= 0) {
		have_def = resources->find_definition(model_id, def);
		if (!have_def) {
			UtilityFunctions::printerr("[GTAVehicleInstance] '", get_name(), "': model id ", model_id,
					" was not found. This only resolves if IdeParser is indexing the CARS IDE "
					"section (see ide_parser.h) -- set Model Name directly otherwise.");
			return;
		}
		target_name = def.model_name.to_lower();
	} else {
		return;
	}

	ModelCollection *models = resources->get_models();
	if (!models->has_model(target_name)) {
		UtilityFunctions::printerr("[GTAVehicleInstance] '", get_name(), "': model '", target_name,
				"' was not found in the loaded IMG archive.");
		return;
	}

	Vector<DffFrame> frames = models->get_frames(target_name);
	if (frames.is_empty()) {
		UtilityFunctions::printerr("[GTAVehicleInstance] '", get_name(), "': model '", target_name,
				"' has no frame data.");
		return;
	}

	// Tear down whatever we built last time (model changed, paint changed, ...).
	clear_parts();

	// Resolve the carcols.dat preset against the final model name so it works
	// no matter which property the scene file sets first.
	apply_color_preset(target_name);

	// Texture dictionary: vehicles conventionally ship a same-named .txd
	// (greenwoo.dff -> greenwoo.txd); prefer the IDE-resolved one if we have it.
	String txd_name = have_def ? def.txd_name : target_name;
	uint32_t flags = have_def ? def.flags : 0;
	TextureCollection *textures = resources->get_textures();

	for (int i = 0; i < frames.size(); i++) {
		const DffFrame &frame = frames[i];

		if (frame.geometry_index < 0) {
			continue; // Pure dummy (suspension/hinge anchor), no geometry of its own.
		}
		if (should_skip_frame(frame.name)) {
			continue;
		}

		Ref<ArrayMesh> part_mesh = models->get_geometry_mesh(target_name, frame.geometry_index);
		if (part_mesh.is_null() || part_mesh->get_surface_count() == 0) {
			continue;
		}

		Vector3 local_pos = DffParser::accumulate_frame_position(frames, i);

		MeshInstance3D *part = memnew(MeshInstance3D);
		// Named after the frame (wheel_lf_dummy, door_lf_dummy, bonnet_dummy, ...)
		// -- same "meaningful names" reasoning as MapBuilder's placement nodes.
		part->set_name(frame.name.is_empty() ? String("part") + String::num_int64(i) : frame.name);
		part->set_position(local_pos);
		part->set_mesh(part_mesh);
		add_child(part);
		part_nodes.push_back(part);

		Vector<DffMaterial> part_materials = models->get_geometry_materials(target_name, frame.geometry_index);
		for (int s = 0; s < part_mesh->get_surface_count() && s < part_materials.size(); s++) {
			// Vertex colors on: the game multiplies them into every vehicle
			// material, and the baked darkness on tires/glass/trim lives
			// there — without it those parts render flat white.
			Ref<StandardMaterial3D> mat = MapMaterial::create(part_materials[s], txd_name, flags, *textures, &paint, true);
			if (mat.is_valid()) {
				part->set_surface_override_material(s, mat);
			}
		}

		// -- Use Streaming -- (fade each part; see GTAModelInstance for the
		// same documented "fade, not LOD-swap" caveat).
		if (use_streaming) {
			float vis_end = have_def && def.draw_distance > 0.0f ? def.draw_distance : 300.0f;
			part->set_visibility_range_end(vis_end);
			part->set_visibility_range_end_margin(vis_end * 0.1f);
		}
	}

	if (part_nodes.is_empty()) {
		UtilityFunctions::printerr("[GTAVehicleInstance] '", get_name(), "': model '", target_name,
				"' produced no visible parts (check the frame names logged above against "
				"should_skip_frame() in gta_vehicle_instance.cpp if this looks wrong).");
	}

	// -- Use Model Collision -- (whole-vehicle collision mesh, same reuse as
	// GTAModelInstance/MapBuilder -- GTA vehicle COL data isn't split per-part).
	if (use_model_collision) {
		ColModel col_model;
		if (models->get_col_model(target_name, col_model)) {
			collision_body = memnew(StaticBody3D);
			add_child(collision_body);
			for (int i = 0; i < col_model.shapes.size(); i++) {
				CollisionShape3D *col = memnew(CollisionShape3D);
				col->set_shape(col_model.shapes[i].shape);
				col->set_transform(col_model.shapes[i].transform);
				collision_body->add_child(col);
			}
		}
	}
}

// =============================================================================
// Property accessors
// =============================================================================

void GTAVehicleInstance::set_gta_path(const String &p_path) {
	gta_path = p_path;
	if (is_inside_tree()) {
		refresh_vehicle();
	}
}
String GTAVehicleInstance::get_gta_path() const { return gta_path; }

void GTAVehicleInstance::set_model_id(int32_t p_id) {
	model_id = p_id;
	if (is_inside_tree()) {
		refresh_vehicle();
	}
}
int32_t GTAVehicleInstance::get_model_id() const { return model_id; }

void GTAVehicleInstance::set_model_name(const String &p_name) {
	model_name = p_name;
	if (is_inside_tree()) {
		refresh_vehicle();
	}
}
String GTAVehicleInstance::get_model_name() const { return model_name; }

void GTAVehicleInstance::set_use_model_collision(bool p_enabled) {
	use_model_collision = p_enabled;
	if (is_inside_tree()) {
		refresh_vehicle();
	}
}
bool GTAVehicleInstance::get_use_model_collision() const { return use_model_collision; }

void GTAVehicleInstance::set_use_streaming(bool p_enabled) {
	use_streaming = p_enabled;
	if (is_inside_tree()) {
		refresh_vehicle();
	}
}
bool GTAVehicleInstance::get_use_streaming() const { return use_streaming; }

void GTAVehicleInstance::set_primary_color(const Color &p_color) {
	paint.primary = p_color;
	if (is_inside_tree()) {
		refresh_vehicle();
	}
}
Color GTAVehicleInstance::get_primary_color() const { return paint.primary; }

void GTAVehicleInstance::set_secondary_color(const Color &p_color) {
	paint.secondary = p_color;
	if (is_inside_tree()) {
		refresh_vehicle();
	}
}
Color GTAVehicleInstance::get_secondary_color() const { return paint.secondary; }

void GTAVehicleInstance::set_tertiary_color(const Color &p_color) {
	paint.tertiary = p_color;
	if (is_inside_tree()) {
		refresh_vehicle();
	}
}
Color GTAVehicleInstance::get_tertiary_color() const { return paint.tertiary; }

void GTAVehicleInstance::set_quaternary_color(const Color &p_color) {
	paint.quaternary = p_color;
	if (is_inside_tree()) {
		refresh_vehicle();
	}
}
Color GTAVehicleInstance::get_quaternary_color() const { return paint.quaternary; }

// =============================================================================
// Carcols presets
// =============================================================================

// carcols.dat is tiny (~15 KB); parse it once per gta_path and keep the
// result cached on the instance so the preset doesn't re-read the file on
// every refresh_vehicle().
void GTAVehicleInstance::apply_color_preset(const String &p_model_name) {
	if (color_preset == 0) {
		return; // 0 == Custom: don't touch existing colors.
	}
	if (p_model_name.is_empty()) {
		return; // Not resolvable yet; applied on a later refresh_vehicle().
	}

	if (carcols_gta_path != gta_path) {
		carcols = CarColsData::parse(gta_path.path_join("data/carcols.dat"));
		carcols_gta_path = gta_path;
	}

	const Vector<CarColorCombo> *combos = nullptr;
	if (carcols.vehicle_combos.has(p_model_name)) {
		combos = &carcols.vehicle_combos[p_model_name];
	}

	// Preset N = the model's N-th combo (1-based; combos usually 8 per car).
	// Presets beyond the vehicle's combo count index the full carcols.dat
	// palette directly (palette index = N - combos - 1, wrapping around), so
	// "any number you want" still yields a real game color — the palette has
	// ~250 entries, one per `col` line.
	if (combos != nullptr && !combos->is_empty() && color_preset <= combos->size()) {
		const CarColorCombo &combo = (*combos)[color_preset - 1];
		paint.primary = combo.colors[0];
		paint.secondary = combo.color_count > 1 ? combo.colors[1] : combo.colors[0];
		paint.tertiary = combo.color_count > 2 ? combo.colors[2] : paint.secondary;
		paint.quaternary = combo.color_count > 3 ? combo.colors[3] : paint.secondary;
	} else if (!carcols.palette.is_empty()) {
		int base = combos != nullptr && !combos->is_empty() ? combos->size() : 0;
		int idx = (color_preset - base - 1) % carcols.palette.size();
		Color c = carcols.palette[idx];
		paint.primary = c;
		paint.secondary = c;
		paint.tertiary = c;
		paint.quaternary = c;
	} else {
		UtilityFunctions::printerr("[GTAVehicleInstance] '", get_name(), "': carcols.dat palette is empty.");
	}
}

void GTAVehicleInstance::set_color_preset(int p_preset) {
	color_preset = p_preset;
	if (is_inside_tree()) {
		refresh_vehicle();
	}
}
int GTAVehicleInstance::get_color_preset() const { return color_preset; }

// =============================================================================
// Binding
// =============================================================================

void GTAVehicleInstance::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_gta_path", "path"), &GTAVehicleInstance::set_gta_path);
	ClassDB::bind_method(D_METHOD("get_gta_path"), &GTAVehicleInstance::get_gta_path);
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "gta_path"), "set_gta_path", "get_gta_path");

	ClassDB::bind_method(D_METHOD("set_model_id", "id"), &GTAVehicleInstance::set_model_id);
	ClassDB::bind_method(D_METHOD("get_model_id"), &GTAVehicleInstance::get_model_id);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "model_id"), "set_model_id", "get_model_id");

	ClassDB::bind_method(D_METHOD("set_model_name", "name"), &GTAVehicleInstance::set_model_name);
	ClassDB::bind_method(D_METHOD("get_model_name"), &GTAVehicleInstance::get_model_name);
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "model_name"), "set_model_name", "get_model_name");

	ClassDB::bind_method(D_METHOD("set_use_model_collision", "enabled"), &GTAVehicleInstance::set_use_model_collision);
	ClassDB::bind_method(D_METHOD("get_use_model_collision"), &GTAVehicleInstance::get_use_model_collision);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "use_model_collision"), "set_use_model_collision", "get_use_model_collision");

	ClassDB::bind_method(D_METHOD("set_use_streaming", "enabled"), &GTAVehicleInstance::set_use_streaming);
	ClassDB::bind_method(D_METHOD("get_use_streaming"), &GTAVehicleInstance::get_use_streaming);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "use_streaming"), "set_use_streaming", "get_use_streaming");

	ClassDB::bind_method(D_METHOD("set_primary_color", "color"), &GTAVehicleInstance::set_primary_color);
	ClassDB::bind_method(D_METHOD("get_primary_color"), &GTAVehicleInstance::get_primary_color);
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "primary_color"), "set_primary_color", "get_primary_color");

	ClassDB::bind_method(D_METHOD("set_secondary_color", "color"), &GTAVehicleInstance::set_secondary_color);
	ClassDB::bind_method(D_METHOD("get_secondary_color"), &GTAVehicleInstance::get_secondary_color);
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "secondary_color"), "set_secondary_color", "get_secondary_color");

	ClassDB::bind_method(D_METHOD("set_tertiary_color", "color"), &GTAVehicleInstance::set_tertiary_color);
	ClassDB::bind_method(D_METHOD("get_tertiary_color"), &GTAVehicleInstance::get_tertiary_color);
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "tertiary_color"), "set_tertiary_color", "get_tertiary_color");

	ClassDB::bind_method(D_METHOD("set_quaternary_color", "color"), &GTAVehicleInstance::set_quaternary_color);
	ClassDB::bind_method(D_METHOD("get_quaternary_color"), &GTAVehicleInstance::get_quaternary_color);
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "quaternary_color"), "set_quaternary_color", "get_quaternary_color");

	ClassDB::bind_method(D_METHOD("set_color_preset", "preset"), &GTAVehicleInstance::set_color_preset);
	ClassDB::bind_method(D_METHOD("get_color_preset"), &GTAVehicleInstance::get_color_preset);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "color_preset"),
			"set_color_preset", "get_color_preset");

	ClassDB::bind_method(D_METHOD("refresh_vehicle"), &GTAVehicleInstance::refresh_vehicle);
}

} // namespace godot
