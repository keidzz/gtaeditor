#include "map_builder.h"

#include <godot_cpp/classes/collision_shape3d.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/static_body3d.hpp>
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
	models.clear();
	textures.clear();
	spawned_nodes.clear();
}

// =============================================================================
// Godot lifecycle
// =============================================================================

void MapBuilder::_ready() {
	// Don't load the map in the editor — only when running the game.
	//if (Engine::get_singleton()->is_editor_hint()) return;
	load_map();
}

void MapBuilder::_process(double delta) {
	// if (Engine::get_singleton()->is_editor_hint()) return;
	if (!loaded)
		return;

	int placements_count = placements.size();
	if (placements_count == 0)
		return;

	Vector3 cam_pos = Vector3(0, 0, 0);
	Camera3D *cam = get_viewport()->get_camera_3d();
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
				spawned_nodes.write[i] = (MeshInstance3D *)1;
			} else {
				MeshInstance3D *instance = spawn_placement(i);
				if (instance) {
					add_child(instance);
					spawned_nodes.write[i] = instance;
				} else {
					// Use (MeshInstance3D*)1 as a marker to prevent retrying failed spawns every frame
					spawned_nodes.write[i] = (MeshInstance3D *)1;
				}
			}
		} else if (out_of_range && spawned_nodes[i] != nullptr) {
			if (spawned_nodes[i] != (MeshInstance3D *)1) {
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

	// Resolve GTA path to absolute.
	String abs_gta_path;
	if (gta_path.begins_with("res://")) {
		abs_gta_path = ProjectSettings::get_singleton()->globalize_path(gta_path);
	} else {
		abs_gta_path = gta_path;
	}

	if (!abs_gta_path.ends_with("/")) {
		abs_gta_path += "/";
	}

	path_resolver.set_root(abs_gta_path);

	if (debug_enabled) {
		UtilityFunctions::print("[MapBuilder] GTA path: ", abs_gta_path);
	}

	// 1. Load IMG archive (models/gta3.img).
	String img_path = path_resolver.resolve("models/gta3.img");
	if (img_path.is_empty()) {
		UtilityFunctions::printerr("[MapBuilder] Could not find models/gta3.img");
		return;
	}
	img_archive.load(img_path);

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

	// 4. Load streaming binary IPLs from IMG.
	load_streaming_ipls();

	// 5. Resolve LOD links.
	resolve_lods();

	// 6. Index all DFF/TXD entries from IMG.
	index_img_assets();

	// 7. Load water if enabled.
	if (load_water) {
		String water_path = path_resolver.resolve("data/water.dat");
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

	if (debug_enabled) {
		UtilityFunctions::print("[MapBuilder] ===== Loading Complete =====");
		UtilityFunctions::print("[MapBuilder]   Definitions: ", definitions.size());
		UtilityFunctions::print("[MapBuilder]   Placements:  ", placements.size());
		UtilityFunctions::print("[MapBuilder]   Models:      ", models.get_model_count());
		UtilityFunctions::print("[MapBuilder]   Textures:    ", textures.get_txd_count());
		UtilityFunctions::print("[MapBuilder] ==============================");
	}

	// 8. Initialize streaming system with distance-priority ordering.
	spawned_nodes.resize(placements.size());
	spawned_nodes.fill(nullptr);

	// Build stream_order index array and sort by distance to initial camera position.
	Vector3 start_pos = Vector3(0, 0, 0);
	Camera3D *cam = get_viewport()->get_camera_3d();
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

	// Load IDEs.
	for (int i = 0; i < dat.ide_paths.size(); i++) {
		load_ide_file(dat.ide_paths[i]);
	}

	// Load COLs.
	for (int i = 0; i < dat.col_paths.size(); i++) {
		String resolved = path_resolver.resolve(dat.col_paths[i]);
		if (!resolved.is_empty()) {
			models.load_col_file(resolved);
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

			if (!img_archive.has_entry(stream_name)) {
				break;
			}

			PackedByteArray data = img_archive.read_entry(stream_name);
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
				if (definitions.has(def_id)) {
					stream_placements.ptrw()[j].item_name = definitions[def_id].model_name.to_lower();
					stream_placements.ptrw()[j].draw_distance = definitions[def_id].draw_distance;
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
					float hd_dist = definitions.has(def_id) ? definitions[def_id].draw_distance * draw_distance_multiplier : streaming_distance;

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

void MapBuilder::load_ide_file(const String &p_ide_path) {
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

void MapBuilder::load_text_ipl(const String &p_ipl_path) {
	String resolved = path_resolver.resolve(p_ipl_path);
	if (resolved.is_empty()) {
		return;
	}

	Vector<ItemPlacement> ipl_placements = IplParser::parse_text(resolved);

	for (int i = 0; i < ipl_placements.size(); i++) {
		int32_t def_id = ipl_placements[i].definition_id;
		if (definitions.has(def_id)) {
			ipl_placements.ptrw()[i].draw_distance = definitions[def_id].draw_distance;
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

void MapBuilder::index_img_assets() {
	// Register all DFF files from the IMG archive.
	Vector<String> dff_entries = img_archive.get_entries_with_extension(".dff");
	for (int i = 0; i < dff_entries.size(); i++) {
		models.register_dff(dff_entries[i], &img_archive);
	}

	// Register all TXD files from the IMG archive.
	Vector<String> txd_entries = img_archive.get_entries_with_extension(".txd");
	for (int i = 0; i < txd_entries.size(); i++) {
		textures.register_txd(txd_entries[i], &img_archive);
	}

	// Parse all COL files from the IMG archive.
	Vector<String> col_entries = img_archive.get_entries_with_extension(".col");
	for (int i = 0; i < col_entries.size(); i++) {
		PackedByteArray data = img_archive.read_entry(col_entries[i]);
		if (!data.is_empty()) {
			models.load_col_bytes(data, col_entries[i]);
		}
	}
}

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

MeshInstance3D *MapBuilder::spawn_placement(int32_t p_index) {
	const ItemPlacement &placement = placements[p_index];

	// Look up the item definition.
	if (!definitions.has(placement.definition_id)) {
		return nullptr;
	}

	const ItemDefinition &def = definitions[placement.definition_id];

	// Get the mesh (lazy DFF parse).
	String model_name = def.model_name.to_lower();
	Ref<ArrayMesh> mesh = models.get_mesh(model_name);
	if (mesh.is_null() || mesh->get_surface_count() == 0) {
		return nullptr;
	}

	// Get material info.
	Vector<DffMaterial> materials = models.get_materials(model_name);

	// Create the MeshInstance3D.
	MeshInstance3D *instance = memnew(MeshInstance3D);
	instance->set_mesh(mesh);
	instance->set_position(placement.position);
	instance->set_quaternion(placement.rotation);

	bool is_lod = placement.lod_begin_distance >= 0.0f;

	// Add static collision body if enabled (only for HD models, skip LODs)
	if (load_collisions && !is_lod) {
		ColModel col_model;
		if (models.get_col_model(model_name, col_model)) {
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
		// This is a LOD model referenced by an HD model.
		// It only becomes visible when the HD model disappears!
		// Crucially, it has NO visibility_range_end, so it stays visible to infinity.
		float begin_dist = placement.lod_begin_distance;
		instance->set_visibility_range_begin(begin_dist);
		instance->set_visibility_range_begin_margin(begin_dist * 0.1f);
		// Disable shadows on LODs to greatly improve performance when looking at the city from afar.
		instance->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
	} else {
		// This is a standard/HD model. It disappears at its draw distance.
		float vis_end = def.draw_distance * draw_distance_multiplier;
		if (vis_end < streaming_distance) {
			instance->set_visibility_range_end(vis_end);
			instance->set_visibility_range_end_margin(vis_end * 0.1f);
		}
	}

	// Apply materials to each surface.
	for (int s = 0; s < mesh->get_surface_count() && s < materials.size(); s++) {
		Ref<StandardMaterial3D> mat = MapMaterial::create(materials[s], def.txd_name, def.flags, textures);
		if (mat.is_valid()) {
			instance->set_surface_override_material(s, mat);
		}
	}

	return instance;
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
}

} // namespace godot
