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

// Getter/setter (defined outside class for bind_methods to work)
// We'll use the member directly since bind_methods needs these:
float MapBuilder::get_streaming_distance() const { return streaming_distance; }
void MapBuilder::set_streaming_distance(float p_dist) { streaming_distance = p_dist; }

int MapBuilder::get_spawns_per_frame_limit() const { return spawns_per_frame_limit; }
void MapBuilder::set_spawns_per_frame_limit(int p_limit) { spawns_per_frame_limit = p_limit; }

// ── Initialization ───────────────────────────────────────────────────────────

void MapBuilder::_ready() {
	// Initialize the asset loader (resolves GTA path + loads IMG archive)
	AssetLoader::get().initialize();

	const String &gta_path = AssetLoader::get().get_gta_path();

	// Read the master data file that tells us where all IDE/IPL/COL/IMG files are
	Ref<FileAccess> dat = FileAccess::open(gta_path + String("data/gta.dat"), FileAccess::READ);
	ERR_FAIL_COND_MSG(dat.is_null(), "Failed to open gta.dat: error " + String::num_int64(FileAccess::get_open_error()));

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

			// Skip interior and level design IPLs
			if (ipl_lower.contains("interior") || ipl_lower.contains("leveldes"))
				continue;

			// Skip path, cull, occlusion, and zone IPLs
			if (ipl_lower.contains("paths") || ipl_lower.contains("cull") ||
				ipl_lower.contains("occlu") || ipl_lower.contains("zon"))
				continue;

			UtilityFunctions::print("Loading IPL: " + ipl_path);

			// Load text-format IPLs
			_read_map_data(ipl_path, &MapBuilder::_read_ipl_line, ipl_path);

			// Load binary stream IPLs (e.g., countryN_stream0.ipl inside gta3.img)
			_load_binary_ipl_streams(ipl_path);
		} else if (command == "IMG") {
			String img_path = tokens[1].to_lower();
			if (img_path.contains("gta3.img")) {
				AssetLoader::get().load_cd_image(tokens[1]);
			} else {
				WARN_PRINT("Skipping IMG file: " + tokens[1] + " (only loading gta3.img)");
			}
		}
	}

	// ── Link 2DFX children to their parent items ─────────────────────────
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
			// Fall back to name-based matching
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

	UtilityFunctions::print("Loaded " + String::num_int64(placements.size()) + " placements total");
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
	float unload_distance = streaming_distance * 1.2f;

	// Determine which spatial grid cells are within streaming range
	int cell_radius = static_cast<int>(Math::ceil(streaming_distance / CELL_SIZE)) + 1;
	CellCoord cam_cell = _cell_for_position(cam_pos);

	int spawns_this_frame = 0;

	// Iterate only cells within range of the camera
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
				bool is_active = active_instances.has(idx);

				if (distance < streaming_distance && !is_active) {
					if (spawns_this_frame < spawns_per_frame_limit) {
						// Spawn this placement
						Node3D *instance = _spawn_placement(placement);
						if (instance != nullptr) {
							map_root->add_child(instance);
							active_instances.insert(idx, instance);
						}
						spawns_this_frame++;
					}
				} else if (distance > unload_distance && is_active) {
					// Despawn this placement
					Node3D *instance = active_instances[idx];
					instance->queue_free();
					active_instances.erase(idx);
				}
			}
		}
	}

	// Also check active instances that may now be out of range
	// (handles cases where the camera teleports far away)
	Vector<int> to_remove;
	for (auto &kv : active_instances) {
		const auto &placement = placements[kv.key];
		float distance = cam_pos.distance_to(placement->position);
		if (distance > unload_distance) {
			kv.value->queue_free();
			to_remove.push_back(kv.key);
		}
	}
	for (int i = 0; i < to_remove.size(); i++) {
		active_instances.erase(to_remove[i]);
	}
}

// ── Spatial grid ─────────────────────────────────────────────────────────────

void MapBuilder::_build_spatial_grid() {
	for (int i = 0; i < placements.size(); i++) {
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

// ── Map management ───────────────────────────────────────────────────────────

void MapBuilder::_clear_map() {
	if (map_root != nullptr) {
		map_root->queue_free();
	}
	map_root = memnew(Node3D);
	map_root->set_name("GTAMap");
	active_instances.clear();
}

// ── Spawning ─────────────────────────────────────────────────────────────────

Node3D *MapBuilder::_spawn_placement(const std::shared_ptr<ItemPlacement> &ipl) {
	if (!items.has(ipl->id))
		return nullptr;

	const auto &item = items[ipl->id];
	if (item == nullptr)
		return nullptr;

	// Flag 0x40: non-renderable / breakable — spawn an empty node
	if (item->flags & 0x40) {
		return memnew(Node3D);
	}

	// Create the streamed mesh instance
	StreamedMesh *instance = memnew(StreamedMesh);
	instance->init(item);
	instance->set_position(ipl->position);
	instance->set_scale(ipl->scale);
	instance->set_quaternion(ipl->rotation);
	instance->set_visibility_range_end(streaming_distance);

	// ── Attach 2DFX lights ───────────────────────────────────────────────
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

	// ── Attach collision shapes ──────────────────────────────────────────
	StaticBody3D *body = memnew(StaticBody3D);

	if (item->colfile != nullptr) {
		// Add box collision shapes
		for (int ci = 0; ci < item->colfile->collisions.size(); ci++) {
			const ColFile::Primitive &prim = item->colfile->collisions[ci];

			if (prim.type == ColFile::PrimitiveType::BOX) {
				// Calculate AABB from min/max, ensuring positive size
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
			// Sphere collisions could be added here if needed
		}

		// Add mesh collision from triangle data
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

		// Remove spaces and split by comma
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
		item->render_distance = tokens[4].to_float();
		item->flags = tokens[tokens.size() - 1].to_int();
		items.insert(id, item);
	} else if (section == "2dfx") {
		int parent = tokens[0].to_int();
		Vector3 position(
				tokens[1].to_float(),
				tokens[3].to_float(),
				-tokens[2].to_float());
		Color color(
				tokens[4].to_float() / 255.0f,
				tokens[5].to_float() / 255.0f,
				tokens[6].to_float() / 255.0f);

		int fx_type = tokens[8].to_int();
		if (fx_type == 0) {
			// Light effect
			auto light = std::make_shared<TDFXLight>();
			light->parent = parent;
			light->position = position;
			light->color = color;
			light->render_distance = tokens[11].to_float();
			light->light_range = tokens[12].to_float();
			light->shadow_intensity = tokens[15].to_int();
			item_children.push_back(light);
		} else {
			WARN_PRINT("Unimplemented 2DFX type: " + String::num_int64(fx_type));
		}
	}
}

// ── IPL parser (text format) ─────────────────────────────────────────────────

void MapBuilder::_read_ipl_line(const String &section, const PackedStringArray &tokens, const String &context) {
	if (section != "inst")
		return;

	if (tokens.size() < 11) {
		WARN_PRINT("Invalid inst line: expected at least 11 tokens, got " + String::num_int64(tokens.size()));
		return;
	}

	// Filter out LOD models (lod_index == -1 means this IS a LOD)
	if (tokens[10].to_int() == -1)
		return;

	auto placement = std::make_shared<ItemPlacement>();
	placement->id = tokens[0].to_int();
	placement->model_name = tokens[1].to_lower();
	placement->interior = tokens[2].to_int();

	// Convert GTA coordinates to Godot coordinates
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

	placements.push_back(placement);
}

// ── Binary IPL streams ───────────────────────────────────────────────────────

void MapBuilder::_load_binary_ipl_streams(const String &base_ipl_path) {
	String base_name = base_ipl_path.get_file().get_basename().to_lower();
	int stream_id = 0;

	while (true) {
		String stream_name = base_name + "_stream" + String::num_int64(stream_id) + ".ipl";
		if (AssetLoader::get().has_asset(stream_name)) {
			UtilityFunctions::print("Loading stream IPL: " + stream_name);
			_parse_binary_ipl(stream_name);
			stream_id++;
		} else {
			break;
		}
	}
}

void MapBuilder::_parse_binary_ipl(const String &asset_name) {
	String asset_key = asset_name.to_lower();

	const DirEntry *entry = AssetLoader::get().get_asset_entry(asset_key);
	if (entry == nullptr)
		return;

	uint64_t base_offset = entry->offset;

	Ref<FileAccess> file = AssetLoader::get().open_asset(asset_name);
	if (file.is_null())
		return;

	// Verify binary IPL header ("bnry")
	String header = file->get_buffer(4).get_string_from_ascii();
	if (header != "bnry")
		return;

	uint32_t num_instances = file->get_32();

	// Seek to the instance offset field (0x1C from the start of the IPL)
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

			// Convert GTA coordinates to Godot
			placement->position = Vector3(pos_x, pos_z, -pos_y);
			placement->rotation = Quaternion(-rot_x, -rot_z, -rot_y, rot_w);
			placement->scale = Vector3(1, 1, 1);

			placements.push_back(placement);
		}
	}
}
