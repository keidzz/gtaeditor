#include "map_builder.h"

#include <godot_cpp/classes/box_shape3d.hpp>
#include <godot_cpp/classes/collision_shape3d.hpp>
#include <godot_cpp/classes/concave_polygon_shape3d.hpp>
#include <godot_cpp/classes/light3d.hpp>
#include <godot_cpp/classes/omni_light3d.hpp>
#include <godot_cpp/classes/static_body3d.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;

// ── Helpers ──────────────────────────────────────────────────────────────────

float MapBuilder::_get_draw_distance(const std::shared_ptr<ItemPlacement> &pl) const {
	if (items.has(pl->id)) {
		float dd = items[pl->id]->render_distance * draw_distance_multiplier;
		if (dd > 0.0f) {
			if (pl->is_lod) {
				// LODs stream out to full streaming distance
				return streaming_distance;
			}
			// HD: respect game distance but cap to a fraction of streaming_distance
			// so LODs always take over beyond this point
			return MIN(dd, streaming_distance * 0.4f);
		}
	}
	return streaming_distance * 0.4f;
}
void MapBuilder::_build_spatial_grid() {
	grid_tiers.clear();
	grid_tiers.push_back({ 100.0f, 300.0f, {} }); // Tier 0: HD small props
	grid_tiers.push_back({ 400.0f, 1000.0f, {} }); // Tier 1: HD large objects
	grid_tiers.push_back({ 1000.0f, 10000.0f, {} }); // Tier 2: LODs only

	for (int i = 0; i < placements.size(); i++) {
		if (placements[i]->interior != 0 && placements[i]->interior != 13)
			continue;

		float dd = 300.0f;
		if (items.has(placements[i]->id)) {
			dd = items[placements[i]->id]->render_distance;
		}

		int tier_idx;
		if (placements[i]->is_lod) {
			// All LODs go to tier 2 regardless of their render_distance
			tier_idx = 2;
		} else if (dd > 300.0f) {
			tier_idx = 1;
		} else {
			tier_idx = 0;
		}

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
