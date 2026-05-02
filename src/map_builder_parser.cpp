#include "asset_loader.h"
#include "map_builder.h"

#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;

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
			if (debug_enabled) {
				UtilityFunctions::print("Loading stream IPL: " + stream_name);
			}
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
