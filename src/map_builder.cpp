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
}

float MapBuilder::get_streaming_distance() const { return streaming_distance; }
void MapBuilder::set_streaming_distance(float p_dist) { streaming_distance = p_dist; }

float MapBuilder::get_draw_distance_multiplier() const { return draw_distance_multiplier; }
void MapBuilder::set_draw_distance_multiplier(float p_mult) { draw_distance_multiplier = p_mult; }

int MapBuilder::get_spawns_per_frame_limit() const { return spawns_per_frame_limit; }
void MapBuilder::set_spawns_per_frame_limit(int p_limit) { spawns_per_frame_limit = p_limit; }

// ── Initialization ───────────────────────────────────────────────────────────

void MapBuilder::_ready() {
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

			UtilityFunctions::print("Loading IPL: " + ipl_path);
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
	UtilityFunctions::print("Loaded " + String::num_int64(placements.size()) +
							" placements (" + String::num_int64(hd_count) + " HD, " +
							String::num_int64(lod_count) + " LOD, " +
							String::num_int64(lod_to_parents.size()) + " LOD links)");
}

// ── IPL group loading with per-group LOD resolution ──────────────────────────

void MapBuilder::_load_ipl_group(const String &ipl_path) {
	// Collect all placements from this IPL group (text + binary streams)
	// into a local list, then resolve LOD within it, then append to global list.
	int group_base = placements.size();

	// Load text IPL
	_read_map_data(ipl_path, &MapBuilder::_read_ipl_line, ipl_path);

	// Load binary stream IPLs
	String base_name = ipl_path.get_file().get_basename().to_lower();
	int stream_id = 0;

	while (true) {
		String stream_name = base_name + "_stream" + String::num_int64(stream_id) + ".ipl";
		if (AssetLoader::get().has_asset(stream_name)) {
			UtilityFunctions::print("Loading stream IPL: " + stream_name);
			// Parse directly into global placements array
			Vector<std::shared_ptr<ItemPlacement>> stream_placements;
			_parse_binary_ipl(stream_name, stream_placements);
			for (int i = 0; i < stream_placements.size(); i++) {
				placements.push_back(stream_placements[i]);
			}
			stream_id++;
		} else {
			break;
		}
	}

	int group_end = placements.size();

	// ── Resolve LOD links within this IPL group ──────────────────────────
	// Like SanAndreasUnity's ResolveLod(): lod_index is an index INTO THIS GROUP.
	// HD placements point TO their LOD child via lod_index.
	for (int i = group_base; i < group_end; i++) {
		auto &pl = placements[i];
		if (pl->lod_index >= 0 && pl->lod_index < (group_end - group_base)) {
			int lod_global_idx = group_base + pl->lod_index;

			// Skip self-references (some IPL entries point to themselves)
			if (lod_global_idx == i)
				continue;

			auto &lod_pl = placements[lod_global_idx];

			// Mark the target as LOD (primary mechanism)
			lod_pl->is_lod = true;
			// Link: HD parent (i) → LOD child (lod_global_idx)
			pl->lod_child_index = lod_global_idx;
			// Reverse link for O(1) lookup in _process()
			if (!lod_to_parents.has(lod_global_idx)) {
				lod_to_parents.insert(lod_global_idx, Vector<int>());
			}
			lod_to_parents[lod_global_idx].push_back(i);
		}
	}

	// ── Fallback: mark orphan LODs by name prefix ────────────────────────
	// Some LOD models (e.g. islandlod*, lod*) exist without an HD parent
	// pointing to them via lod_index. Mark these as LODs too so they don't
	// render as duplicate full-quality objects.
	for (int i = group_base; i < group_end; i++) {
		auto &pl = placements[i];
		if (!pl->is_lod) {
			String name_lower = pl->model_name.to_lower();
			if (name_lower.begins_with("lod") || name_lower.begins_with("islandlod")) {
				pl->is_lod = true;
			}
		}
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

					if (!printed_debug && is_active) {
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
							
							if (placement->is_lod) {
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
						
						if (!placement->is_lod) {
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
}

// ── Helpers ──────────────────────────────────────────────────────────────────

float MapBuilder::_get_draw_distance(const std::shared_ptr<ItemPlacement> &pl) const {
	if (items.has(pl->id)) {
		float dd = items[pl->id]->render_distance * draw_distance_multiplier;
		if (dd > 0.0f) {
			if (pl->is_lod) {
				// LOD models are cheap — allow up to 3x streaming distance
				return MIN(dd, streaming_distance * 3.0f);
			}
			// HD models: respect game's draw distance capped to streaming_distance
			return MIN(dd, streaming_distance);
		}
	}
	return streaming_distance;
}

void MapBuilder::_build_spatial_grid() {
	grid_tiers.clear();

	// Tier 0: HD small props (0 - 300)
	grid_tiers.push_back({ 100.0f, 300.0f, {} });
	// Tier 1: Medium objects / Medium LODs (300 - 1000)
	grid_tiers.push_back({ 400.0f, 1000.0f, {} });
	// Tier 2: Far LODs (1000+)
	grid_tiers.push_back({ 1000.0f, 10000.0f, {} });

	for (int i = 0; i < placements.size(); i++) {
		// Skip interior placements
		if (placements[i]->interior != 0 && placements[i]->interior != 13)
			continue;

		float dd = 300.0f;
		if (items.has(placements[i]->id)) {
			dd = items[placements[i]->id]->render_distance;
			if (placements[i]->is_lod) {
				dd *= 3.0f; // Roughly estimate max possible distance
			}
		}

		int tier_idx = 0;
		if (dd > 1000.0f)
			tier_idx = 2;
		else if (dd > 300.0f)
			tier_idx = 1;

		float cell_size = grid_tiers[tier_idx].cell_size;
		CellCoord cell = _cell_for_position(placements[i]->position, cell_size);

		if (!grid_tiers.ptrw()[tier_idx].cells.has(cell)) {
			grid_tiers.ptrw()[tier_idx].cells.insert(cell, Vector<int>());
		}
		grid_tiers.ptrw()[tier_idx].cells[cell].push_back(i);
	}
}

MapBuilder::CellCoord MapBuilder::_cell_for_position(const Vector3 &pos, float cell_size) const {
	return {
		static_cast<int>(Math::floor(pos.x / cell_size)),
		static_cast<int>(Math::floor(pos.z / cell_size))
	};
}

void MapBuilder::_clear_map() {
	if (map_root != nullptr) {
		map_root->queue_free();
	}
	map_root = memnew(Node3D);
	map_root->set_name("GTAMap");
	active_instances.clear();
	loaded_instances.clear();
	loading_meshes.clear();
	hidden_instances.clear();
	hidden_lru.clear();
}

void MapBuilder::_evict_hidden_pool() {
	for (int i = 0; i < hidden_lru.size();) {
		if (hidden_instances.size() <= MAX_HIDDEN_POOL)
			break;

		int idx = hidden_lru[i];
		Node3D *instance = hidden_instances[idx];

		// Do not evict if still loading (prevent dangling pointers)
		StreamedMesh *sm = Object::cast_to<StreamedMesh>(instance);
		if (sm && sm->get_load_state() == StreamedMesh::LOADING) {
			i++;
			continue;
		}

		hidden_lru.remove_at(i);
		hidden_instances.erase(idx);

		// Remove from loading_meshes if present
		for (int j = loading_meshes.size() - 1; j >= 0; j--) {
			if (loading_meshes[j].first == idx) {
				loading_meshes.remove_at(j);
				break;
			}
		}

		instance->queue_free();
	}
}

// ── Spawning ─────────────────────────────────────────────────────────────────

Node3D *MapBuilder::_spawn_placement(const std::shared_ptr<ItemPlacement> &ipl, bool near) {
	if (!items.has(ipl->id))
		return nullptr;

	const auto &item = items[ipl->id];
	if (item == nullptr)
		return nullptr;

	if (item->flags & 0x40) {
		return memnew(Node3D);
	}

	StreamedMesh *instance = memnew(StreamedMesh);
	bool cache_hit = instance->init(item);
	instance->set_position(ipl->position);
	instance->set_scale(ipl->scale);
	instance->set_quaternion(ipl->rotation);

	// Start background loading if not served from cache
	if (!cache_hit) {
		instance->start_loading();
	}

	// Only attach lights and collision for nearby objects
	if (near) {
		// ── 2DFX lights ──────────────────────────────────────────────────
		for (int ci = 0; ci < item->children.size(); ci++) {
			TDFXLight *light_def = dynamic_cast<TDFXLight *>(item->children[ci].get());
			if (light_def != nullptr) {
				OmniLight3D *light = memnew(OmniLight3D);
				light->set_position(light_def->position);
				light->set_color(light_def->color);
				light->set_param(Light3D::PARAM_RANGE, light_def->light_range);
				light->set_param(Light3D::PARAM_ENERGY, static_cast<float>(light_def->shadow_intensity) / 20.0f);
				light->set_enable_distance_fade(true);
				light->set_distance_fade_begin(light_def->render_distance / 2.0f);
				instance->add_child(light);
			}
		}

		// ── Collision shapes ─────────────────────────────────────────────
		StaticBody3D *body = memnew(StaticBody3D);

		if (item->colfile != nullptr) {
			for (int ci = 0; ci < item->colfile->collisions.size(); ci++) {
				const ColFile::Primitive &prim = item->colfile->collisions[ci];
				if (prim.type == ColFile::PrimitiveType::BOX) {
					Vector3 aabb_min(
							MIN(prim.box_min.x, prim.box_max.x),
							MIN(prim.box_min.y, prim.box_max.y),
							MIN(prim.box_min.z, prim.box_max.z));
					Vector3 aabb_max(
							MAX(prim.box_min.x, prim.box_max.x),
							MAX(prim.box_min.y, prim.box_max.y),
							MAX(prim.box_min.z, prim.box_max.z));
					Vector3 aabb_size = aabb_max - aabb_min;

					if (aabb_size.x > 0 && aabb_size.y > 0 && aabb_size.z > 0) {
						Ref<BoxShape3D> shape;
						shape.instantiate();
						shape->set_size(aabb_size);

						CollisionShape3D *col = memnew(CollisionShape3D);
						col->set_shape(shape);
						col->set_position((aabb_min + aabb_max) * 0.5f);
						body->add_child(col);
					}
				}
			}

			if (item->colfile->vertices.size() > 0) {
				Ref<ConcavePolygonShape3D> shape;
				shape.instantiate();
				shape->set_faces(item->colfile->vertices);

				CollisionShape3D *col = memnew(CollisionShape3D);
				col->set_shape(shape);
				body->add_child(col);
			}
		}
		instance->add_child(body);
	}

	return instance;
}

// ── Data file parsing ────────────────────────────────────────────────────────

void MapBuilder::_read_map_data(const String &path,
								void (MapBuilder::*handler)(const String &, const PackedStringArray &, const String &),
								const String &context) {
	Ref<FileAccess> file = AssetLoader::get().open(path);
	if (file.is_null())
		return;

	String section;
	while (!file->eof_reached()) {
		String line = file->get_line();
		if (line.is_empty() || line.begins_with("#"))
			continue;

		PackedStringArray tokens = line.replace(" ", "").split(",", false);
		if (tokens.size() == 1) {
			section = tokens[0];
		} else {
			(this->*handler)(section, tokens, context);
		}
	}
}

// ── IDE parser ───────────────────────────────────────────────────────────────

void MapBuilder::_read_ide_line(const String &section, const PackedStringArray &tokens, const String &context) {
	int id = tokens[0].to_int();

	if (section == "objs" || section == "tobj") {
		auto item = std::make_shared<ItemDef>();
		item->model_name = tokens[1];
		item->txd_name = tokens[2];

		// GTA SA format: ID, ModelName, TxdName, DrawDistance, Flags
		if (tokens.size() > 4) {
			item->render_distance = tokens[3].to_float();
			item->flags = tokens[4].to_int();
		}

		// NOTE: is_lod is NOT set here. It is ONLY set by the LOD linking
		// pass in _load_ipl_group(). Name-prefix detection is unreliable
		// because many GTA SA LOD models don't have the "lod" prefix.

		items.insert(id, item);
	} else if (section == "2dfx") {
		int parent = tokens[0].to_int();
		Vector3 position(tokens[1].to_float(), tokens[3].to_float(), -tokens[2].to_float());
		Color color(tokens[4].to_float() / 255.0f, tokens[5].to_float() / 255.0f, tokens[6].to_float() / 255.0f);

		int fx_type = tokens[8].to_int();
		if (fx_type == 0) {
			auto light = std::make_shared<TDFXLight>();
			light->parent = parent;
			light->position = position;
			light->color = color;
			light->render_distance = tokens[11].to_float();
			light->light_range = tokens[12].to_float();
			light->shadow_intensity = tokens[15].to_int();
			item_children.push_back(light);
		}
	}
}

// ── IPL parser (text format) ─────────────────────────────────────────────────

void MapBuilder::_read_ipl_line(const String &section, const PackedStringArray &tokens, const String &context) {
	if (section != "inst")
		return;

	if (tokens.size() < 11)
		return;

	// Keep ALL placements including LOD models — they are used for distant rendering.

	auto placement = std::make_shared<ItemPlacement>();
	placement->id = tokens[0].to_int();
	placement->model_name = tokens[1].to_lower();
	placement->interior = tokens[2].to_int();

	placement->position = Vector3(
			tokens[3].to_float(),
			tokens[5].to_float(),
			-tokens[4].to_float());

	placement->rotation = Quaternion(
			-tokens[6].to_float(),
			-tokens[8].to_float(),
			-tokens[7].to_float(),
			tokens[9].to_float());

	placement->lod_index = tokens[10].to_int();
	placement->scale = Vector3(1, 1, 1);

	// NOTE: is_lod is NOT set here — only set by LOD linking pass.

	placements.push_back(placement);
}

// ── Binary IPL streams ───────────────────────────────────────────────────────

void MapBuilder::_parse_binary_ipl(const String &asset_name, Vector<std::shared_ptr<ItemPlacement>> &out) {
	String asset_key = asset_name.to_lower();

	const DirEntry *entry = AssetLoader::get().get_asset_entry(asset_key);
	if (entry == nullptr)
		return;

	uint64_t base_offset = entry->offset;

	Ref<FileAccess> file = AssetLoader::get().open_asset(asset_name);
	if (file.is_null())
		return;

	String header = file->get_buffer(4).get_string_from_ascii();
	if (header != "bnry")
		return;

	uint32_t num_instances = file->get_32();

	file->seek(base_offset + 0x1C);
	uint32_t offset_instances = file->get_32();

	if (num_instances > 0) {
		file->seek(base_offset + offset_instances);

		for (uint32_t i = 0; i < num_instances; i++) {
			float pos_x = file->get_float();
			float pos_y = file->get_float();
			float pos_z = file->get_float();

			float rot_x = file->get_float();
			float rot_y = file->get_float();
			float rot_z = file->get_float();
			float rot_w = file->get_float();

			uint32_t id = file->get_32();
			uint32_t interior = file->get_32();
			int32_t lod_index = file->get_32();

			auto placement = std::make_shared<ItemPlacement>();
			placement->id = id;
			placement->interior = interior;
			placement->lod_index = lod_index;

			if (items.has(id)) {
				placement->model_name = items[id]->model_name;
			}

			placement->position = Vector3(pos_x, pos_z, -pos_y);
			placement->rotation = Quaternion(-rot_x, -rot_z, -rot_y, rot_w);
			placement->scale = Vector3(1, 1, 1);

			// NOTE: is_lod is NOT set here — only set by LOD linking pass.

			out.push_back(placement);
		}
	}
}
