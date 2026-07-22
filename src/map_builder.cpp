#include "map_builder.h"

#include <godot_cpp/classes/collision_shape3d.hpp>
#include <godot_cpp/classes/editor_interface.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/static_body3d.hpp>
#include <godot_cpp/classes/sub_viewport.hpp>
#include <godot_cpp/classes/viewport.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include "classes/water_parser.h"

#include <algorithm>

namespace godot {

// =============================================================================
// Constructor / Destructor
// =============================================================================

MapBuilder::MapBuilder() {}
MapBuilder::~MapBuilder() {
	// models/textures now live on the shared GtaResourceProvider singleton —
	// other GTAModelInstance/GTAVehicleInstance/MapBuilder nodes may still be
	// using them, so this destructor must NOT clear them anymore.
	spawned_nodes.clear();
	spawned_lights.clear();
}

// =============================================================================
// Godot lifecycle
// =============================================================================

void MapBuilder::_ready() {
	// Only load automatically at runtime, unless load_map_in_editor opts in.
	if (Engine::get_singleton()->is_editor_hint() && !load_map_in_editor) {
		return;
	}
	load_map();
}

void MapBuilder::_process(double delta) {
	if (Engine::get_singleton()->is_editor_hint() && !load_map_in_editor) {
		return;
	}
	if (!loaded)
		return;

	update_2dfx_lights();

	int placements_count = placements.size();
	if (placements_count == 0)
		return;

	Vector3 cam_pos = Vector3(0, 0, 0);
	Camera3D *cam = get_active_camera();
	if (cam) {
		cam_pos = cam->get_global_position();
	}

	// Re-sort streaming order when camera moves significantly (>100 units).
	float moved_sq = cam_pos.distance_squared_to(last_sort_position);
	if (moved_sq > 100.0f * 100.0f) {
		sort_stream_order(cam_pos);
		stream_process_index = 0;
	}

	int batch_size = 2000;

	uint64_t start_time = Time::get_singleton()->get_ticks_msec();
	int checked_this_frame = 0;

	// Time budget: max ~8ms per frame to maintain >60FPS
	while (checked_this_frame < batch_size && (Time::get_singleton()->get_ticks_msec() - start_time) < 8) {
		int i = stream_order[stream_process_index];
		const ItemPlacement &placement = placements[i];

		float dist_sq = placement.position.distance_squared_to(cam_pos);

		bool is_lod = placement.lod_begin_distance >= 0.0f;
		float stream_dist = is_lod ? streaming_distance : MIN(placement.draw_distance * draw_distance_multiplier, streaming_distance);

		float stream_dist_sq = stream_dist * stream_dist;
		// Hysteresis to prevent objects from popping in and out at the border
		float despawn_margin = is_lod ? 100.0f : 50.0f;
		float despawn_dist_sq = (stream_dist + despawn_margin) * (stream_dist + despawn_margin);

		bool in_range = dist_sq < stream_dist_sq;
		bool out_of_range = dist_sq > despawn_dist_sq;

		if (in_range && spawned_nodes[i] == nullptr) {
			if (placement.interior != 0 && !load_interiors) {
				// Mark as skipped so we don't check again
				spawned_nodes.write[i] = (Node3D *)1;
			} else {
				Node3D *instance = spawn_placement(i);
				if (instance) {
					add_child(instance);
					spawned_nodes.write[i] = instance;
				} else {
					// Use (Node3D*)1 as a marker to prevent retrying failed spawns every frame.
					spawned_nodes.write[i] = (Node3D *)1;
				}
			}
		} else if (out_of_range && spawned_nodes[i] != nullptr) {
			if (spawned_nodes[i] != (Node3D *)1) {
				remove_2dfx_lights(spawned_nodes[i]);
				spawned_nodes[i]->queue_free();
			}
			spawned_nodes.write[i] = nullptr;
		}

		stream_process_index = (stream_process_index + 1) % placements_count;
		checked_this_frame++;
	}
}

// =============================================================================
// Loading pipeline
// =============================================================================

void MapBuilder::load_map() {
	if (loaded)
		return;

	// 1-3, 6 (path resolution, IMG archive, default.dat/gta.dat IDE parsing,
	// DFF/TXD/COL indexing) now live on the shared provider so any
	// GTAModelInstance/GTAVehicleInstance can trigger/reuse the same load.
	resources = GtaResourceProvider::get_singleton();
	if (!resources->ensure_loaded(gta_path)) {
		UtilityFunctions::printerr("[MapBuilder] Failed to load shared GTA resources from '", gta_path, "'.");
		return;
	}

	GtaPathResolver *path_resolver = resources->get_path_resolver();

	if (debug_enabled) {
		UtilityFunctions::print("[MapBuilder] Using GTA path: ", path_resolver->get_root());
	}

	// 4/5. Parse default.dat/gta.dat again here for their IPL/COL lists and
	// build this MapBuilder's own `placements` array (streaming binary IPLs +
	// LOD resolution). Re-parsing these two small text files is cheap; the
	// heavy IMG/IDE work above already happened at most once via the provider.
	String default_dat_path = path_resolver->resolve("data/default.dat");
	if (!default_dat_path.is_empty()) {
		load_dat_file(default_dat_path);
	}

	String gta_dat_path = path_resolver->resolve("data/gta.dat");
	if (!gta_dat_path.is_empty()) {
		load_dat_file(gta_dat_path);
	}

	load_streaming_ipls();
	resolve_lods();

	// 7. Load water if enabled.
	if (load_water) {
		String water_path = path_resolver->resolve("data/water.dat");
		if (!water_path.is_empty()) {
			Vector<WaterPlane> water_planes = WaterParser::parse(water_path);
			MeshInstance3D *water_instance = MapWaterLoader::build_water_mesh(water_planes);
			if (water_instance) {
				add_child(water_instance);
				if (debug_enabled) {
					UtilityFunctions::print("[MapBuilder] Loaded ", water_planes.size(), " water planes");
				}
			}
		}
	}

	loaded = true;
	emit_signal("map_loaded");

	const HashMap<int32_t, ItemDefinition> &definitions = resources->get_all_definitions();

	int total_lights = 0;
	for (const KeyValue<int32_t, ItemDefinition> &kv : definitions) {
		total_lights += kv.value.lights.size();
	}
	UtilityFunctions::print("[MapBuilder]   2dfx Lights:  ", total_lights);

	for (const KeyValue<int32_t, ItemDefinition> &kv : definitions) {
		if (kv.value.lights.size() > 0) {
			UtilityFunctions::print("[MapBuilder]   Light def: id=", kv.key,
					" model=", kv.value.model_name, " lights=", kv.value.lights.size());
		}
	}

	int placements_with_lights = 0;
	for (int i = 0; i < placements.size(); i++) {
		if (definitions.has(placements[i].definition_id) &&
				definitions[placements[i].definition_id].lights.size() > 0) {
			placements_with_lights++;
		}
	}
	UtilityFunctions::print("[MapBuilder]   Placements referencing lit defs: ", placements_with_lights);
	if (debug_enabled) {
		UtilityFunctions::print("[MapBuilder] ===== Loading Complete =====");
		UtilityFunctions::print("[MapBuilder]   Definitions: ", definitions.size());
		UtilityFunctions::print("[MapBuilder]   Placements:  ", placements.size());
		UtilityFunctions::print("[MapBuilder]   Models:      ", resources->get_models()->get_model_count());
		UtilityFunctions::print("[MapBuilder]   Textures:    ", resources->get_textures()->get_txd_count());
		UtilityFunctions::print("[MapBuilder] ==============================");
	}

	// 8. Initialize streaming system with distance-priority ordering.
	spawned_nodes.resize(placements.size());
	spawned_nodes.fill(nullptr);

	// Build stream_order index array and sort by distance to initial camera position.
	Vector3 start_pos = Vector3(0, 0, 0);
	Camera3D *cam = get_active_camera();
	if (cam) {
		start_pos = cam->get_global_position();
	}

	stream_order.resize(placements.size());
	for (int i = 0; i < placements.size(); i++) {
		stream_order.write[i] = i;
	}
	sort_stream_order(start_pos);

	if (debug_enabled) {
		UtilityFunctions::print("[MapBuilder] Ready for dynamic streaming (", placements.size(), " placements)");
	}
}

void MapBuilder::load_dat_file(const String &p_dat_path) {
	DatResult dat = DatParser::parse(p_dat_path);

	// NOTE: IDE loading used to happen here — it's now handled once, up
	// front, by resources->ensure_loaded() (see GtaResourceProvider), so
	// GTAModelInstance/GTAVehicleInstance get the exact same definitions
	// without needing a MapBuilder to run first. Everything below is
	// placement-specific and stays here since only MapBuilder owns `placements`.

	ImgArchive *img_archive = resources->get_img_archive();
	GtaPathResolver *path_resolver = resources->get_path_resolver();

	// Load COLs.
	for (int i = 0; i < dat.col_paths.size(); i++) {
		String resolved = path_resolver->resolve(dat.col_paths[i]);
		if (!resolved.is_empty()) {
			resources->get_models()->load_col_file(resolved);
		}
	}

	// Load text IPLs and their streaming binary counterparts.
	// LOD resolution is per-region: each text IPL + its _stream{N} IPLs
	// form a region, and LOD indices are resolved within that combined list.
	for (int i = 0; i < dat.ipl_paths.size(); i++) {
		String ipl_path = dat.ipl_paths[i];

		// Track the start index in our global placements array.
		int region_start = placements.size();

		// Load text IPL.
		load_text_ipl(ipl_path);

		// Load matching streaming binary IPLs: {basename}_stream{0}.ipl, {1}, etc.
		String clean_path = ipl_path.replace("\\", "/");
		String basename = clean_path.get_file().get_basename().to_lower();

		for (int stream_idx = 0;; stream_idx++) {
			String stream_name = basename + "_stream" + String::num_int64(stream_idx) + ".ipl";

			if (!img_archive->has_entry(stream_name)) {
				break;
			}

			PackedByteArray data = img_archive->read_entry(stream_name);
			if (data.size() < 4)
				break;

			// Check for binary magic.
			if (data[0] != 'b' || data[1] != 'n' || data[2] != 'r' || data[3] != 'y') {
				break;
			}

			Vector<ItemPlacement> stream_placements = IplParser::parse_binary(data);

			// Fill in model names and draw distances from definitions.
			for (int j = 0; j < stream_placements.size(); j++) {
				int32_t def_id = stream_placements[j].definition_id;
				ItemDefinition def;
				if (resources->find_definition(def_id, def)) {
					stream_placements.ptrw()[j].item_name = def.model_name.to_lower();
					stream_placements.ptrw()[j].draw_distance = def.draw_distance;
				}
			}

			placements.append_array(stream_placements);
		}

		int region_end = placements.size();

		// Resolve LOD indices within this region.
		for (int idx = region_start; idx < region_end; idx++) {
			int32_t lod_idx = placements[idx].lod_index;
			if (lod_idx >= 0) {
				// LOD index is relative to this region's combined placement list.
				int32_t global_lod_idx = region_start + lod_idx;
				if (global_lod_idx < region_end) {
					int32_t def_id = placements[idx].definition_id;
					ItemDefinition def;
					float hd_dist = resources->find_definition(def_id, def) ? def.draw_distance * draw_distance_multiplier : streaming_distance;

					if (placements[global_lod_idx].lod_begin_distance < 0.0f) {
						placements.ptrw()[global_lod_idx].lod_begin_distance = hd_dist;
					} else {
						// If multiple HD models reference the same LOD, use the max distance so the LOD
						// doesn't pop in too early while some HD models are still visible.
						placements.ptrw()[global_lod_idx].lod_begin_distance = MAX(placements[global_lod_idx].lod_begin_distance, hd_dist);
					}
				}
			}
		}
	}

	// We stream the placements dynamically, so we don't need to sort them here.
}

void MapBuilder::load_text_ipl(const String &p_ipl_path) {
	String resolved = resources->get_path_resolver()->resolve(p_ipl_path);
	if (resolved.is_empty()) {
		return;
	}

	Vector<ItemPlacement> ipl_placements = IplParser::parse_text(resolved);

	for (int i = 0; i < ipl_placements.size(); i++) {
		int32_t def_id = ipl_placements[i].definition_id;
		ItemDefinition def;
		if (resources->find_definition(def_id, def)) {
			ipl_placements.ptrw()[i].draw_distance = def.draw_distance;
		}
	}

	if (debug_enabled && !ipl_placements.is_empty()) {
		UtilityFunctions::print("[MapBuilder] Text IPL: ", p_ipl_path,
								" -> ", ipl_placements.size(), " placements");
	}

	placements.append_array(ipl_placements);
}

void MapBuilder::load_streaming_ipls() {
	// Streaming IPLs are now loaded per-region in load_dat_file().
	// This function is kept for any remaining binary IPLs not matched
	// by the streaming naming convention.
	if (debug_enabled) {
		UtilityFunctions::print("[MapBuilder] Streaming IPLs loaded per-region");
	}
}

void MapBuilder::resolve_lods() {
	// LOD resolution is now done per-region in load_dat_file().
	// This function is kept for compatibility.
}

// NOTE: index_img_assets() (DFF/TXD/COL registration into ModelCollection/
// TextureCollection) moved to GtaResourceProvider::index_img_assets() — it's
// now shared, one-time, session-wide state instead of a per-MapBuilder step.

// =============================================================================
// Spawn all placements at once
// =============================================================================

// spawn_all() is no longer used — spawning is deferred via _process().
void MapBuilder::spawn_all() {
	// Kept for compatibility. Batch spawning is handled by _process().
}

// =============================================================================
// Spawn a single placement
// =============================================================================

Node3D *MapBuilder::spawn_placement(int32_t p_index) {
	const ItemPlacement &placement = placements[p_index];

	// Look up the item definition (zero-copy — this runs on every streamed-in object).
	const ItemDefinition *def_ptr = resources->find_definition_ptr(placement.definition_id);
	if (def_ptr == nullptr) {
		return nullptr;
	}
	const ItemDefinition &def = *def_ptr;

	ModelCollection *models = resources->get_models();
	TextureCollection *textures = resources->get_textures();

	Node3D *placement_root = memnew(Node3D);
	// Name it after the GTA model instead of leaving Godot's default
	// "@Node3D@N" — makes the Remote scene tree actually debuggable.
	//
	// IMPORTANT: appending p_index makes this name unique by construction.
	// Godot enforces unique names among siblings, and when a name collides
	// it retries name2, name3, name4... until it finds a free slot — cheap
	// once in a while, but ruinous here: the same prop (e.g. "lamppost1")
	// repeats hundreds of times across a GTA map, and this function runs on
	// every single streamed-in object, so a colliding name means Godot
	// re-scans an ever-growing run of siblings on every spawn as more of
	// that same prop accumulates in the tree. Suffixing with p_index (unique
	// per placement, guaranteed no collision) keeps the name meaningful
	// while making that retry loop a no-op every time.
	placement_root->set_name(def.model_name + String("_") + String::num_int64(p_index));
	placement_root->set_position(placement.position);
	placement_root->set_quaternion(placement.rotation);

	// Get the mesh (lazy DFF parse).
	String model_name = def.model_name.to_lower();
	Ref<ArrayMesh> mesh = models->get_mesh(model_name);
	if (mesh.is_valid() && mesh->get_surface_count() > 0) {
		// Get material info.
		Vector<DffMaterial> materials = models->get_materials(model_name);

		MeshInstance3D *instance = memnew(MeshInstance3D);
		instance->set_name(model_name); // Single child of placement_root — never collides, cheap either way.
		instance->set_mesh(mesh);
		placement_root->add_child(instance);

		bool is_lod = placement.lod_begin_distance >= 0.0f;

		// Add static collision body if enabled (only for HD models, skip LODs).
		if (load_collisions && !is_lod) {
			ColModel col_model;
			if (models->get_col_model(model_name, col_model)) {
				StaticBody3D *body = memnew(StaticBody3D);
				for (int i = 0; i < col_model.shapes.size(); i++) {
					CollisionShape3D *col = memnew(CollisionShape3D);
					col->set_shape(col_model.shapes[i].shape);
					col->set_transform(col_model.shapes[i].transform);
					body->add_child(col);
				}
				instance->add_child(body);
			}
		}

		// Set visibility range for automatic distance culling.
		if (is_lod) {
			// This is a LOD model referenced by an HD model. It only becomes visible
			// after the HD model disappears and stays visible to infinity.
			float begin_dist = placement.lod_begin_distance;
			instance->set_visibility_range_begin(begin_dist);
			instance->set_visibility_range_begin_margin(begin_dist * 0.1f);
			instance->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
		} else {
			float vis_end = def.draw_distance * draw_distance_multiplier;
			if (vis_end < streaming_distance) {
				instance->set_visibility_range_end(vis_end);
				instance->set_visibility_range_end_margin(vis_end * 0.1f);
			}
		}

		// Apply materials to each surface.
		for (int s = 0; s < mesh->get_surface_count() && s < materials.size(); s++) {
			Ref<StandardMaterial3D> mat = MapMaterial::create(materials[s], def.txd_name, def.flags, *textures);
			if (mat.is_valid()) {
				instance->set_surface_override_material(s, mat);
			}
		}
	}

	spawn_2dfx_lights(placement_root, def);
	if (placement_root->get_child_count() == 0) {
		memdelete(placement_root);
		return nullptr;
	}
	return placement_root;
}

void MapBuilder::spawn_2dfx_lights(Node3D *p_placement_root, const ItemDefinition &p_definition) {
	auto spawn_one = [&](const Vector3 &p_offset, uint8_t p_r, uint8_t p_g, uint8_t p_b, uint8_t p_a, float p_range) {
		OmniLight3D *light = memnew(OmniLight3D);
		light->set_position(p_offset);
		light->set_color(Color(p_r / 255.0f, p_g / 255.0f, p_b / 255.0f, p_a / 255.0f));
		light->set_param(Light3D::PARAM_RANGE, MAX(p_range, 0.1f));
		light->set_shadow(streetlight_shadows);

		float factor = MAX(last_night_light_factor, 0.0f);
		light->set_param(Light3D::PARAM_ENERGY, streetlight_energy * factor);
		light->set_visible(factor > 0.0f);
		p_placement_root->add_child(light);
		spawned_lights.push_back(light);
	};

	// Legacy: 2dfx definido en texto en el IDE (herencia III/VC).
	for (int i = 0; i < p_definition.lights.size(); i++) {
		const TwoDFXLight &source = p_definition.lights[i];
		spawn_one(source.local_offset, source.red, source.green, source.blue, source.alpha, source.pointlight_range);
	}

	// SA: 2dfx embebido en el propio DFF (farolas, neones, etc.)
	Vector<Dff2dfxLight> dff_lights = resources->get_models()->get_2dfx_lights(p_definition.model_name.to_lower());
	for (int i = 0; i < dff_lights.size(); i++) {
		const Dff2dfxLight &source = dff_lights[i];
		spawn_one(source.local_offset, source.red, source.green, source.blue, source.alpha, source.pointlight_range);
	}
}

void MapBuilder::remove_2dfx_lights(Node3D *p_placement_root) {
	for (int i = spawned_lights.size() - 1; i >= 0; i--) {
		if (spawned_lights[i]->get_parent() == p_placement_root) {
			spawned_lights.remove_at(i);
		}
	}
}

float MapBuilder::get_night_light_factor(float p_hour) const {
	// GTA-style two-hour fades: 18:00-20:00 on, 05:00-07:00 off.
	if (p_hour >= 20.0f || p_hour < 5.0f) {
		return 1.0f;
	}
	if (p_hour >= 18.0f) {
		return (p_hour - 18.0f) * 0.5f;
	}
	if (p_hour < 7.0f) {
		return 1.0f - (p_hour - 5.0f) * 0.5f;
	}
	return 0.0f;
}

void MapBuilder::update_2dfx_lights() {
	if (time_of_day == nullptr) {
		Node *parent = get_parent();
		if (parent) {
			time_of_day = parent->find_child("TimeOfDay", true, false);
		}
	}

	// A map scene without Sky3D has no cycle to query; retain visible lights so
	// the importer remains useful in standalone test scenes.
	float factor = 1.0f;
	if (time_of_day) {
		Variant current_time = time_of_day->get("current_time");
		if (current_time.get_type() == Variant::FLOAT || current_time.get_type() == Variant::INT) {
			factor = get_night_light_factor(static_cast<float>(current_time));
		}
	}

	if (factor == last_night_light_factor) {
		return;
	}
	last_night_light_factor = factor;
	for (int i = 0; i < spawned_lights.size(); i++) {
		OmniLight3D *light = spawned_lights[i];
		Color color = light->get_color();
		light->set_param(Light3D::PARAM_ENERGY, streetlight_energy * factor);
		light->set_visible(factor > 0.0f);
	}
}

// =============================================================================
// Streaming order — sorts placement indices by distance to camera
// =============================================================================

void MapBuilder::sort_stream_order(const Vector3 &cam_pos) {
	int32_t *ptr = stream_order.ptrw();
	int count = stream_order.size();
	const ItemPlacement *pl = placements.ptr();

	std::sort(ptr, ptr + count, [pl, cam_pos](int32_t a, int32_t b) {
		return pl[a].position.distance_squared_to(cam_pos) < pl[b].position.distance_squared_to(cam_pos);
	});

	last_sort_position = cam_pos;
}

Camera3D *MapBuilder::get_active_camera() const {
	if (Engine::get_singleton()->is_editor_hint() && load_map_in_editor) {
		EditorInterface *ei = EditorInterface::get_singleton();
		if (ei != nullptr) {
			SubViewport *editor_vp = ei->get_editor_viewport_3d(0);
			if (editor_vp != nullptr && editor_vp->get_camera_3d() != nullptr) {
				return editor_vp->get_camera_3d();
			}
		}
		// Fall through if the editor camera isn't available yet (e.g. the
		// very first frame after enabling the option).
	}
	return get_viewport()->get_camera_3d();
}

// =============================================================================
// Property accessors
// =============================================================================

void MapBuilder::set_streaming_distance(float p_dist) { streaming_distance = p_dist; }
float MapBuilder::get_streaming_distance() const { return streaming_distance; }

void MapBuilder::set_draw_distance_multiplier(float p_mult) { draw_distance_multiplier = p_mult; }
float MapBuilder::get_draw_distance_multiplier() const { return draw_distance_multiplier; }

void MapBuilder::set_debug_enabled(bool p_enabled) { debug_enabled = p_enabled; }
bool MapBuilder::get_debug_enabled() const { return debug_enabled; }

void MapBuilder::set_load_interiors(bool p_load) { load_interiors = p_load; }
bool MapBuilder::get_load_interiors() const { return load_interiors; }

void MapBuilder::set_load_collisions(bool p_load) { load_collisions = p_load; }
bool MapBuilder::get_load_collisions() const { return load_collisions; }

void MapBuilder::set_load_water(bool p_load) { load_water = p_load; }
bool MapBuilder::get_load_water() const { return load_water; }

void MapBuilder::set_gta_path(const String &p_path) { gta_path = p_path; }
String MapBuilder::get_gta_path() const { return gta_path; }

void MapBuilder::set_load_map_in_editor(bool p_enabled) {
	load_map_in_editor = p_enabled;
	if (p_enabled && is_inside_tree() && Engine::get_singleton()->is_editor_hint() && !loaded) {
		load_map();
	}
}
bool MapBuilder::get_load_map_in_editor() const { return load_map_in_editor; }

// =============================================================================
// Shared-resource access
// =============================================================================

bool MapBuilder::is_loaded() const {
	return loaded;
}

ModelCollection *MapBuilder::get_model_collection() {
	return resources != nullptr ? resources->get_models() : GtaResourceProvider::get_singleton()->get_models();
}

TextureCollection *MapBuilder::get_texture_collection() {
	return resources != nullptr ? resources->get_textures() : GtaResourceProvider::get_singleton()->get_textures();
}

bool MapBuilder::find_definition(int32_t p_id, ItemDefinition &r_definition) {
	return GtaResourceProvider::get_singleton()->find_definition(p_id, r_definition);
}

bool MapBuilder::find_definition_by_model_name(const String &p_model_name, ItemDefinition &r_definition) {
	return GtaResourceProvider::get_singleton()->find_definition_by_model_name(p_model_name, r_definition);
}

// =============================================================================
// Binding
// =============================================================================

void MapBuilder::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_streaming_distance", "distance"), &MapBuilder::set_streaming_distance);
	ClassDB::bind_method(D_METHOD("get_streaming_distance"), &MapBuilder::get_streaming_distance);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "streaming_distance"), "set_streaming_distance", "get_streaming_distance");

	ClassDB::bind_method(D_METHOD("set_draw_distance_multiplier", "multiplier"), &MapBuilder::set_draw_distance_multiplier);
	ClassDB::bind_method(D_METHOD("get_draw_distance_multiplier"), &MapBuilder::get_draw_distance_multiplier);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "draw_distance_multiplier"), "set_draw_distance_multiplier", "get_draw_distance_multiplier");

	ClassDB::bind_method(D_METHOD("set_debug_enabled", "enabled"), &MapBuilder::set_debug_enabled);
	ClassDB::bind_method(D_METHOD("get_debug_enabled"), &MapBuilder::get_debug_enabled);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "debug_enabled"), "set_debug_enabled", "get_debug_enabled");

	ClassDB::bind_method(D_METHOD("set_load_interiors", "load"), &MapBuilder::set_load_interiors);
	ClassDB::bind_method(D_METHOD("get_load_interiors"), &MapBuilder::get_load_interiors);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "load_interiors"), "set_load_interiors", "get_load_interiors");

	ClassDB::bind_method(D_METHOD("set_load_collisions", "load"), &MapBuilder::set_load_collisions);
	ClassDB::bind_method(D_METHOD("get_load_collisions"), &MapBuilder::get_load_collisions);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "load_collisions"), "set_load_collisions", "get_load_collisions");

	ClassDB::bind_method(D_METHOD("set_load_water", "load"), &MapBuilder::set_load_water);
	ClassDB::bind_method(D_METHOD("get_load_water"), &MapBuilder::get_load_water);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "load_water"), "set_load_water", "get_load_water");

	ClassDB::bind_method(D_METHOD("set_gta_path", "path"), &MapBuilder::set_gta_path);
	ClassDB::bind_method(D_METHOD("get_gta_path"), &MapBuilder::get_gta_path);
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "gta_path"), "set_gta_path", "get_gta_path");

	ClassDB::bind_method(D_METHOD("set_load_map_in_editor", "enabled"), &MapBuilder::set_load_map_in_editor);
	ClassDB::bind_method(D_METHOD("get_load_map_in_editor"), &MapBuilder::get_load_map_in_editor);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "load_map_in_editor"), "set_load_map_in_editor", "get_load_map_in_editor");

	ADD_SIGNAL(MethodInfo("map_loaded"));
}

} // namespace godot
