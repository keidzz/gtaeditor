#include "map_builder.h"
#include "asset_loader.h"
#include "streamed_mesh.h"

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

	ClassDB::bind_method(D_METHOD("get_spawns_per_frame_limit"), &MapBuilder::get_spawns_per_frame_limit);
	ClassDB::bind_method(D_METHOD("set_spawns_per_frame_limit", "limit"), &MapBuilder::set_spawns_per_frame_limit);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "spawns_per_frame_limit"), "set_spawns_per_frame_limit", "get_spawns_per_frame_limit");
}

float MapBuilder::get_streaming_distance() const { return streaming_distance; }
void MapBuilder::set_streaming_distance(float p_dist) { streaming_distance = p_dist; }

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
							String::num_int64(lod_to_parent.size()) + " LOD links)");
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
			auto &lod_pl = placements[lod_global_idx];

			// Mark the target as LOD
			lod_pl->is_lod = true;
			// Link: HD parent (i) → LOD child (lod_global_idx)
			pl->lod_child_index = lod_global_idx;
			// Reverse link for O(1) lookup in _process()
			lod_to_parent.insert(lod_global_idx, i);
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

	int cell_radius = static_cast<int>(Math::ceil(streaming_distance / CELL_SIZE)) + 1;
	CellCoord cam_cell = _cell_for_position(cam_pos);

	int spawns_this_frame = 0;

	for (int cx = cam_cell.x - cell_radius; cx <= cam_cell.x + cell_radius; cx++) {
		for (int cz = cam_cell.z - cell_radius; cz <= cam_cell.z + cell_radius; cz++) {
			CellCoord cell = { cx, cz };
			if (!spatial_grid.has(cell))
				continue;

			const Vector<int> &cell_placements = spatial_grid[cell];
			for (int i = 0; i < cell_placements.size(); i++) {
				int idx = cell_placements[i];
				const auto &placement = placements[idx];
				float distance = cam_pos.distance_to(placement->position);
				float draw_dist = _get_draw_distance(placement);
				float unload_dist = draw_dist * 1.2f;
				bool is_active = active_instances.has(idx);

				if (distance < draw_dist && !is_active) {
					// ── LOD visibility rule (like SanAndreasUnity) ───────
					// A LOD model is visible ONLY when its HD parent is NOT active.
					if (placement->is_lod) {
						if (lod_to_parent.has(idx)) {
							int parent_idx = lod_to_parent[idx];
							if (active_instances.has(parent_idx)) {
								continue; // HD parent is visible, hide LOD
							}
						}
					}

					if (spawns_this_frame < spawns_per_frame_limit) {
						bool near = (distance < PHYSICS_DISTANCE);
						Node3D *instance = _spawn_placement(placement, near);
						if (instance != nullptr) {
							map_root->add_child(instance);
							active_instances.insert(idx, instance);
						}
						spawns_this_frame++;
					}
				} else if (distance > unload_dist && is_active) {
					Node3D *instance = active_instances[idx];
					instance->queue_free();
					active_instances.erase(idx);
				}
			}
		}
	}

	// Cleanup out-of-range instances (handles teleportation)
	Vector<int> to_remove;
	for (auto &kv : active_instances) {
		const auto &placement = placements[kv.key];
		float distance = cam_pos.distance_to(placement->position);
		float unload_dist = _get_draw_distance(placement) * 1.2f;

		if (distance > unload_dist) {
			kv.value->queue_free();
			to_remove.push_back(kv.key);
		}
	}
	for (int i = 0; i < to_remove.size(); i++) {
		active_instances.erase(to_remove[i]);
	}
}

// ── Helpers ──────────────────────────────────────────────────────────────────

float MapBuilder::_get_draw_distance(const std::shared_ptr<ItemPlacement> &pl) const {
	if (items.has(pl->id)) {
		float dd = items[pl->id]->render_distance;
		if (dd > 0.0f) {
			// LOD models can use their full draw distance (they're cheap)
			// HD models are capped to streaming_distance
			if (pl->is_lod) {
				return MIN(dd, streaming_distance * 2.0f);
			}
			return MIN(dd, streaming_distance);
		}
	}
	return streaming_distance;
}

void MapBuilder::_build_spatial_grid() {
	for (int i = 0; i < placements.size(); i++) {
		// Skip interior placements
		if (placements[i]->interior != 0 && placements[i]->interior != 13)
			continue;

		CellCoord cell = _cell_for_position(placements[i]->position);
		if (!spatial_grid.has(cell)) {
			spatial_grid.insert(cell, Vector<int>());
		}
		spatial_grid[cell].push_back(i);
	}
}

MapBuilder::CellCoord MapBuilder::_cell_for_position(const Vector3 &pos) const {
	return {
		static_cast<int>(Math::floor(pos.x / CELL_SIZE)),
		static_cast<int>(Math::floor(pos.z / CELL_SIZE))
	};
}

void MapBuilder::_clear_map() {
	if (map_root != nullptr) {
		map_root->queue_free();
	}
	map_root = memnew(Node3D);
	map_root->set_name("GTAMap");
	active_instances.clear();
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
	instance->init(item);
	instance->set_position(ipl->position);
	instance->set_scale(ipl->scale);
	instance->set_quaternion(ipl->rotation);

	float vis_range = _get_draw_distance(ipl);
	instance->set_visibility_range_end(vis_range);

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

		int mesh_count = tokens[3].to_int();
		item->draw_distance_count = mesh_count;

		if (mesh_count >= 1 && tokens.size() > 4) {
			item->render_distance = tokens[4].to_float();
		}
		if (mesh_count >= 2 && tokens.size() > 5) {
			item->render_distance_2 = tokens[5].to_float();
		}
		if (mesh_count >= 3 && tokens.size() > 6) {
			item->render_distance_3 = tokens[6].to_float();
		}

		item->flags = tokens[tokens.size() - 1].to_int();

		// Detect LOD models by name prefix
		String name_lower = item->model_name.to_lower();
		if (name_lower.begins_with("lod") || name_lower.begins_with("islandlod")) {
			item->is_lod = true;
		}

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

	// Tag LOD by name prefix (resolved properly in _load_ipl_group)
	String name_lower = placement->model_name.to_lower();
	if (name_lower.begins_with("lod") || name_lower.begins_with("islandlod")) {
		placement->is_lod = true;
	}

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

			String name_lower = placement->model_name.to_lower();
			if (name_lower.begins_with("lod") || name_lower.begins_with("islandlod")) {
				placement->is_lod = true;
			}

			out.push_back(placement);
		}
	}
}
