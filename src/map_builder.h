#ifndef GTAEDITOR_MAP_BUILDER_H
#define GTAEDITOR_MAP_BUILDER_H

#include "classes/col_file.h"
#include "classes/item_def.h"
#include "classes/item_placement.h"
#include "classes/tdfx.h"
#include "classes/tdfx_light.h"
#include "streamed_mesh.h"

#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/classes/canvas_layer.hpp>
#include <godot_cpp/classes/label.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/templates/hash_map.hpp>
#include <godot_cpp/templates/hash_set.hpp>
#include <godot_cpp/templates/vector.hpp>

#include <memory>

using namespace godot;

/// Builds and streams the GTA San Andreas world map with LOD support.
///
/// Streaming model (inspired by SanAndreasUnity):
///   - Each placement uses its ItemDef::render_distance for streaming range
///   - LOD placements are resolved per-IPL-group (text + binary streams)
///   - A LOD model is shown only when its HD parent is NOT active with a loaded mesh
///   - Collision and lights are only attached within physics_distance
///   - StreamedMesh instances do NOT run _process(); MapBuilder polls loading meshes
///   - Hidden instances are pooled instead of destroyed to reduce node churn
class MapBuilder : public Node {
	GDCLASS(MapBuilder, Node);

public:
	MapBuilder();
	~MapBuilder();

	void _ready() override;
	void _process(double p_delta) override;

	/// Maximum streaming distance (caps per-object render_distance).
	float streaming_distance = 300.0f;

	float get_streaming_distance() const;
	void set_streaming_distance(float p_dist);

	/// Multiplier for extending HD model visibility range.
	float draw_distance_multiplier = 3.0f;

	float get_draw_distance_multiplier() const;
	void set_draw_distance_multiplier(float p_mult);

	/// Maximum number of placements to spawn per frame.
	int spawns_per_frame_limit = 50;

	int get_spawns_per_frame_limit() const;
	void set_spawns_per_frame_limit(int p_limit);

	/// Enable global debug features (prints, UI).
	bool debug_enabled = false;

	float debug_label_distance = 100.0f;
	float get_debug_label_distance() const;
	void set_debug_label_distance(float p_dist);

	bool get_debug_enabled() const;
	void set_debug_enabled(bool p_enabled);

protected:
	static void _bind_methods();

private:
	// ── Data stores ──────────────────────────────────────────────────────
	HashMap<int, std::shared_ptr<ItemDef>> items;
	Vector<std::shared_ptr<TDFX>> item_children;
	Vector<std::shared_ptr<ItemPlacement>> placements;
	Vector<std::shared_ptr<ColFile>> collisions;

	// ── Scene nodes ──────────────────────────────────────────────────────
	Node3D *map_root = nullptr;
	Camera3D *camera = nullptr;
	CanvasLayer *debug_canvas = nullptr;

	// ── Spatial grid ─────────────────────────────────────────────────────
	struct CellCoord {
		int x = 0;
		int z = 0;
		bool operator==(const CellCoord &other) const { return x == other.x && z == other.z; }
	};

	struct CellCoordHash {
		static _FORCE_INLINE_ uint32_t hash(const CellCoord &c) {
			uint32_t h = static_cast<uint32_t>(c.x) * 73856093;
			h ^= static_cast<uint32_t>(c.z) * 19349663;
			return h;
		}
	};

	struct SpatialGridTier {
		float cell_size = 200.0f;
		float max_distance = 300.0f;
		HashMap<CellCoord, Vector<int>, CellCoordHash> cells;
	};

	Vector<SpatialGridTier> grid_tiers;

	// ── Active instances ─────────────────────────────────────────────────

	/// Active (spawned and visible) instances: placement index → scene node.
	HashMap<int, Node3D *> active_instances;

	/// Set of placement indices whose StreamedMesh has fully loaded its mesh.
	/// Used for LOD visibility: LOD is hidden only when parent is in this set.
	HashSet<int> loaded_instances;

	/// StreamedMesh instances currently in LOADING state, need polling.
	/// Pair: (placement_index, StreamedMesh pointer)
	Vector<std::pair<int, StreamedMesh *>> loading_meshes;

	// ── Hidden instance pool (reduces node churn) ────────────────────────

	/// Hidden (out-of-range) instances kept alive for fast re-show.
	HashMap<int, Node3D *> hidden_instances;

	/// LRU order for hidden instances (front = oldest).
	Vector<int> hidden_lru;

	/// Maximum number of hidden instances to keep pooled.
	static constexpr int MAX_HIDDEN_POOL = 2000;

	/// Reverse LOD map: LOD placement index → HD parent placement indices.
	/// Used for O(1) lookup: "are any of my HD parents currently active?"
	HashMap<int, Vector<int>> lod_to_parents;

	/// Distance within which collision/lights are attached.
	static constexpr float PHYSICS_DISTANCE = 120.0f;

	/// Hysteresis multiplier for unloading (unload at draw_dist * this).
	static constexpr float UNLOAD_HYSTERESIS = 1.1f;

	void _build_spatial_grid();
	CellCoord _cell_for_position(const Vector3 &pos, float cell_size) const;

	/// Get the effective streaming distance for a placement.
	float _get_draw_distance(const std::shared_ptr<ItemPlacement> &pl) const;

	// ── Parsing helpers ──────────────────────────────────────────────────
	void _read_map_data(const String &path,
						void (MapBuilder::*handler)(const String &, const PackedStringArray &, const String &),
						const String &context);

	void _read_ide_line(const String &section, const PackedStringArray &tokens, const String &context);
	void _read_ipl_line(const String &section, const PackedStringArray &tokens, const String &context);

	/// Load a full IPL group (text + binary streams) and resolve LOD links within it.
	void _load_ipl_group(const String &ipl_path);

	void _parse_binary_ipl(const String &asset_name, Vector<std::shared_ptr<ItemPlacement>> &out);
	void _clear_map();
	Node3D *_spawn_placement(const std::shared_ptr<ItemPlacement> &ipl, bool near);

	/// Evict oldest hidden instances if pool exceeds MAX_HIDDEN_POOL.
	void _evict_hidden_pool();

	/// Temporary buffer for per-IPL-group LOD resolution.
	int _ipl_group_base = 0;
};

#endif // GTAEDITOR_MAP_BUILDER_H
