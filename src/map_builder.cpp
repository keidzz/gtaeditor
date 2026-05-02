#include "map_builder.h"
#include "asset_loader.h"

#include <godot_cpp/templates/hash_set.hpp>

#include <godot_cpp/classes/box_shape3d.hpp>
#include <godot_cpp/classes/collision_shape3d.hpp>
#include <godot_cpp/classes/concave_polygon_shape3d.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/light3d.hpp>
#include <godot_cpp/classes/omni_light3d.hpp>
#include <godot_cpp/classes/static_body3d.hpp>
#include <godot_cpp/classes/viewport.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;

// ── Construction ─────────────────────────────────────────────────────────────

MapBuilder::MapBuilder() {}
MapBuilder::~MapBuilder() {}

void MapBuilder::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_streaming_distance"), &MapBuilder::get_streaming_distance);
	ClassDB::bind_method(D_METHOD("set_streaming_distance", "distance"), &MapBuilder::set_streaming_distance);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "streaming_distance"), "set_streaming_distance", "get_streaming_distance");

	ClassDB::bind_method(D_METHOD("get_draw_distance_multiplier"), &MapBuilder::get_draw_distance_multiplier);
	ClassDB::bind_method(D_METHOD("set_draw_distance_multiplier", "multiplier"), &MapBuilder::set_draw_distance_multiplier);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "draw_distance_multiplier"), "set_draw_distance_multiplier", "get_draw_distance_multiplier");

	ClassDB::bind_method(D_METHOD("get_spawns_per_frame_limit"), &MapBuilder::get_spawns_per_frame_limit);
	ClassDB::bind_method(D_METHOD("set_spawns_per_frame_limit", "limit"), &MapBuilder::set_spawns_per_frame_limit);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "spawns_per_frame_limit"), "set_spawns_per_frame_limit", "get_spawns_per_frame_limit");

	ClassDB::bind_method(D_METHOD("get_debug_enabled"), &MapBuilder::get_debug_enabled);
	ClassDB::bind_method(D_METHOD("set_debug_enabled", "enabled"), &MapBuilder::set_debug_enabled);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "debug_enabled"), "set_debug_enabled", "get_debug_enabled");

	ClassDB::bind_method(D_METHOD("get_debug_label_distance"), &MapBuilder::get_debug_label_distance);
	ClassDB::bind_method(D_METHOD("set_debug_label_distance", "distance"), &MapBuilder::set_debug_label_distance);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "debug_label_distance"), "set_debug_label_distance", "get_debug_label_distance");
}

float MapBuilder::get_streaming_distance() const { return streaming_distance; }
void MapBuilder::set_streaming_distance(float p_dist) { streaming_distance = p_dist; }

float MapBuilder::get_draw_distance_multiplier() const { return draw_distance_multiplier; }
void MapBuilder::set_draw_distance_multiplier(float p_mult) { draw_distance_multiplier = p_mult; }

int MapBuilder::get_spawns_per_frame_limit() const { return spawns_per_frame_limit; }
void MapBuilder::set_spawns_per_frame_limit(int p_limit) { spawns_per_frame_limit = p_limit; }

bool MapBuilder::get_debug_enabled() const { return debug_enabled; }
void MapBuilder::set_debug_enabled(bool p_enabled) {
	debug_enabled = p_enabled;
	AssetLoader::get().debug_enabled = p_enabled;
}

float MapBuilder::get_debug_label_distance() const { return debug_label_distance; }
void MapBuilder::set_debug_label_distance(float p_dist) { debug_label_distance = p_dist; }

// ── Initialization ───────────────────────────────────────────────────────────

void MapBuilder::_ready() {
	// ── Setup debug UI ───────────────────────────────────────────────────
	debug_canvas = memnew(CanvasLayer);
	debug_canvas->set_layer(100);
	add_child(debug_canvas);

	AssetLoader::get().initialize();
	const String &gta_path = AssetLoader::get().get_gta_path();

	Ref<FileAccess> dat = FileAccess::open(gta_path + String("data/gta.dat"), FileAccess::READ);
	ERR_FAIL_COND_MSG(dat.is_null(), "Failed to open gta.dat");

	while (!dat->eof_reached()) {
		String line = dat->get_line();
		if (line.begins_with("#") || line.is_empty())
			continue;

		PackedStringArray tokens = line.split(" ", false);
		if (tokens.size() == 0)
			continue;

		String command = tokens[0];

		if (command == "IDE") {
			_read_map_data(tokens[1], &MapBuilder::_read_ide_line, "");
		} else if (command == "COLFILE") {
			Ref<FileAccess> colfile = AssetLoader::get().open(gta_path + String(tokens[2]));
			if (colfile.is_valid()) {
				while (colfile->get_position() < colfile->get_length()) {
					auto col = std::make_shared<ColFile>();
					col->parse(colfile);
					collisions.push_back(col);
				}
			}
		} else if (command == "IPL") {
			String ipl_path = tokens[1];
			String ipl_lower = ipl_path.to_lower();

			if (ipl_lower.contains("interior") || ipl_lower.contains("leveldes"))
				continue;
			if (ipl_lower.contains("paths") || ipl_lower.contains("cull") ||
				ipl_lower.contains("occlu") || ipl_lower.contains("zon"))
				continue;

			if (debug_enabled) {
				UtilityFunctions::print("Loading IPL: " + ipl_path);
			}
			_load_ipl_group(ipl_path);
		} else if (command == "IMG") {
			String img_path = tokens[1].to_lower();
			if (img_path.contains("gta3.img")) {
				AssetLoader::get().load_cd_image(tokens[1]);
			}
		}
	}

	// ── Link 2DFX children to parent items ───────────────────────────────
	for (int i = 0; i < item_children.size(); i++) {
		int parent_id = item_children[i]->parent;
		if (items.has(parent_id)) {
			items[parent_id]->children.push_back(item_children[i]);
		}
	}

	// ── Link collision files to items ────────────────────────────────────
	for (int i = 0; i < collisions.size(); i++) {
		const auto &col = collisions[i];
		if (items.has(col->model_id)) {
			items[col->model_id]->colfile = col;
		} else {
			for (auto &kv : items) {
				if (kv.value->model_name.matchn(col->model_name)) {
					kv.value->colfile = col;
					break;
				}
			}
		}
	}

	// ── Build spatial grid and map ───────────────────────────────────────
	_build_spatial_grid();
	_clear_map();
	call_deferred("add_child", map_root);

	int lod_count = 0, hd_count = 0;
	for (int i = 0; i < placements.size(); i++) {
		if (placements[i]->is_lod)
			lod_count++;
		else
			hd_count++;
	}
	if (debug_enabled) {
		UtilityFunctions::print("Loaded " + String::num_int64(placements.size()) +
								" placements (" + String::num_int64(hd_count) + " HD, " +
								String::num_int64(lod_count) + " LOD, " +
								String::num_int64(lod_to_parents.size()) + " LOD links)");
	}
}

// ── Per-frame streaming ──────────────────────────────────────────────────────

void MapBuilder::_process(double p_delta) {
	if (camera == nullptr) {
		Viewport *vp = get_viewport();
		if (vp != nullptr) {
			camera = vp->get_camera_3d();
		}
		return;
	}

	if (debug_canvas) {
		debug_canvas->set_visible(debug_enabled);
	}

	Vector3 cam_pos = camera->get_global_position();
	int spawns_this_frame = 0;

	// ── Step 1: Poll loading meshes ──────────────────────────────────────
	// Only iterate meshes that are actively loading (not all instances).
	for (int i = loading_meshes.size() - 1; i >= 0; i--) {
		auto &pair = loading_meshes[i];
		StreamedMesh *sm = pair.second;
		if (sm != nullptr && sm->poll_loading()) {
			// Loading complete — mark as loaded for LOD visibility ONLY if still active
			if (sm->is_mesh_loaded() && active_instances.has(pair.first)) {
				loaded_instances.insert(pair.first);
			}
			loading_meshes.remove_at(i);
		}
	}

	// ── Step 1b: Retroactively hide LODs whose HD parent just loaded ─────
	// This is critical: a LOD may have been spawned while its HD parent was
	// still loading. Now that the parent is loaded, the LOD must be hidden.
	// (Matches SanAndreasUnity's StaticGeometry.UpdateVisibility() pattern)
	{
		Vector<int> lods_to_hide;
		for (auto &kv : active_instances) {
			int idx = kv.key;
			const auto &placement = placements[idx];
			if (placement->is_lod && lod_to_parents.has(idx)) {
				const Vector<int> &parents = lod_to_parents[idx];
				bool any_parent_loaded = false;
				for (int p = 0; p < parents.size(); p++) {
					if (loaded_instances.has(parents[p])) {
						any_parent_loaded = true;
						break;
					}
				}
				if (any_parent_loaded) {
					lods_to_hide.push_back(idx);
				}
			}
		}
		for (int i = 0; i < lods_to_hide.size(); i++) {
			int idx = lods_to_hide[i];
			if (active_instances.has(idx)) {
				Node3D *instance = active_instances[idx];
				instance->set_visible(false);
				active_instances.erase(idx);
				loaded_instances.erase(idx);
				hidden_instances.insert(idx, instance);
				hidden_lru.push_back(idx);
			}
		}
	}

	// ── Step 2: Spawn in-range placements ────────────────────────────────
	bool printed_debug = false;
	for (int t = 0; t < grid_tiers.size(); t++) {
		auto &tier = grid_tiers[t];

		// Use the tier's max distance capped by the global streaming distance limit
		// (LODs can use up to 3x streaming distance)
		float check_dist = tier.max_distance;
		if (t == 0)
			check_dist = MIN(check_dist, streaming_distance);
		else
			check_dist = MIN(check_dist, streaming_distance * 3.0f);

		int cell_radius = static_cast<int>(Math::ceil(check_dist / tier.cell_size)) + 1;
		CellCoord cam_cell = _cell_for_position(cam_pos, tier.cell_size);

		for (int cx = cam_cell.x - cell_radius; cx <= cam_cell.x + cell_radius; cx++) {
			for (int cz = cam_cell.z - cell_radius; cz <= cam_cell.z + cell_radius; cz++) {
				CellCoord cell = { cx, cz };
				if (!tier.cells.has(cell))
					continue;

				const Vector<int> &cell_placements = tier.cells[cell];
				for (int i = 0; i < cell_placements.size(); i++) {
					int idx = cell_placements[i];
					const auto &placement = placements[idx];
					float distance = cam_pos.distance_to(placement->position);
					float draw_dist = _get_draw_distance(placement);

					bool is_active = active_instances.has(idx);
					bool is_hidden = hidden_instances.has(idx);

					if (debug_enabled && !printed_debug && is_active) {
						UtilityFunctions::print("Active ID ", placement->id, " draw_dist: ", draw_dist);
						printed_debug = true;
					}

					if (distance < draw_dist && !is_active) {
						// ── LOD visibility rule (like SanAndreasUnity) ───────
						// A LOD model is visible ONLY when its HD parent has
						// a LOADED mesh (not just spawned).
						if (placement->is_lod) {
							if (lod_to_parents.has(idx)) {
								const Vector<int> &parents = lod_to_parents[idx];
								bool any_parent_loaded = false;
								for (int p = 0; p < parents.size(); p++) {
									if (loaded_instances.has(parents[p])) {
										any_parent_loaded = true;
										break;
									}
								}
								if (any_parent_loaded) {
									continue; // HD parent mesh loaded, hide LOD
								}
							}
						}

						if (spawns_this_frame >= spawns_per_frame_limit)
							continue;

						// ── Try to re-show a hidden instance first ───────────
						if (is_hidden) {
							Node3D *instance = hidden_instances[idx];
							instance->set_visible(true);
							active_instances.insert(idx, instance);
							hidden_instances.erase(idx);
							hidden_lru.erase(idx);
							// Restore loaded state
							StreamedMesh *sm = Object::cast_to<StreamedMesh>(instance);
							if (sm != nullptr && sm->is_mesh_loaded()) {
								loaded_instances.insert(idx);
							}
							spawns_this_frame++;

							if (debug_enabled && placement->is_lod) {
								UtilityFunctions::print("LOD model reappeared: ", placement->model_name, " at distance ", distance);
							}
							continue;
						}

						// ── Spawn a new instance ─────────────────────────────
						bool near = (distance < PHYSICS_DISTANCE);
						Node3D *instance = _spawn_placement(placement, near);
						if (instance != nullptr) {
							map_root->add_child(instance);
							active_instances.insert(idx, instance);

							// Check if it loaded from cache (immediate)
							StreamedMesh *sm = Object::cast_to<StreamedMesh>(instance);
							if (sm != nullptr) {
								if (sm->is_mesh_loaded()) {
									loaded_instances.insert(idx);
								} else if (sm->get_load_state() == StreamedMesh::LOADING) {
									loading_meshes.push_back({ idx, sm });
								}
							}
						}
						spawns_this_frame++;

					} else if (distance > draw_dist * UNLOAD_HYSTERESIS && is_active) {
						// ── Hide instead of destroy ──────────────────────────
						Node3D *instance = active_instances[idx];
						instance->set_visible(false);
						active_instances.erase(idx);
						loaded_instances.erase(idx);
						hidden_instances.insert(idx, instance);
						hidden_lru.push_back(idx);

						if (debug_enabled && !placement->is_lod) {
							UtilityFunctions::print("HD model unloaded: ", placement->model_name, " at distance ", distance);
						}
					}
				}
			}
		}
	}

	// ── Step 3: Cleanup far-away active instances (teleportation) ────────
	Vector<int> to_remove;
	for (auto &kv : active_instances) {
		const auto &placement = placements[kv.key];
		float distance = cam_pos.distance_to(placement->position);
		float unload_dist = _get_draw_distance(placement) * UNLOAD_HYSTERESIS;

		if (distance > unload_dist) {
			kv.value->set_visible(false);
			hidden_instances.insert(kv.key, kv.value);
			hidden_lru.push_back(kv.key);
			to_remove.push_back(kv.key);
		}
	}
	for (int i = 0; i < to_remove.size(); i++) {
		active_instances.erase(to_remove[i]);
		loaded_instances.erase(to_remove[i]);
	}

	// ── Step 4: Evict oldest hidden instances if pool overflows ─────────
	_evict_hidden_pool();

	// ── Step 5: Debug UI — one floating label per visible HD model ───────
	if (debug_enabled && camera != nullptr && debug_canvas != nullptr) {
		// ── Recycle label pool: remove children beyond what we need ──────
		// We'll reuse existing Label children and create new ones as needed.
		TypedArray<Node> existing = debug_canvas->get_children();

		int label_index = 0;

		for (auto &kv : active_instances) {
			const auto &placement = placements[kv.key];
			if (placement->is_lod)
				continue;

			if (camera->is_position_behind(placement->position))
				continue;

			Vector2 screen_pos = camera->unproject_position(placement->position);
			Rect2 visible_rect = camera->get_viewport()->get_visible_rect();
			if (!visible_rect.has_point(screen_pos))
				continue;

			float dist = camera->get_global_position().distance_to(placement->position);
			// ── Skip labels beyond this distance ─────────────────────────────
			if (dist > debug_label_distance)
				continue;
			// Scale font 18px at 50m → 8px at 300m
			int font_size = (int)Math::clamp(18.0f - (dist / 300.0f) * 10.0f, 8.0f, 24.0f);

			// ── Get or create a Label for this slot ──────────────────────
			Label *lbl = nullptr;
			if (label_index < existing.size()) {
				lbl = Object::cast_to<Label>(existing[label_index]);
			}
			if (lbl == nullptr) {
				lbl = memnew(Label);
				// Outline (stroke)
				lbl->add_theme_color_override("font_outline_color", Color(0, 0, 0, 1));
				lbl->add_theme_constant_override("outline_size", 3);
				// Drop shadow — needs an offset so it's actually visible
				lbl->add_theme_color_override("font_shadow_color", Color(0, 0, 0, 0.6f));
				lbl->add_theme_constant_override("shadow_offset_x", 2);
				lbl->add_theme_constant_override("shadow_offset_y", 2);
				debug_canvas->add_child(lbl);
			}

			lbl->set_text(placement->model_name);
			lbl->add_theme_font_size_override("font_size", font_size);
			lbl->set_visible(true);

			// Center the label on the projected position
			lbl->set_position(screen_pos - lbl->get_size() / 2.0f);

			label_index++;
		}

		// ── Hide unused labels from the pool ─────────────────────────────
		for (int i = label_index; i < existing.size(); i++) {
			Label *lbl = Object::cast_to<Label>(existing[i]);
			if (lbl != nullptr) {
				lbl->set_visible(false);
			}
		}
	}
}
