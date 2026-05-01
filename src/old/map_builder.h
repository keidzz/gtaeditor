#ifndef GTAEDITOR_MAP_BUILDER_H
#define GTAEDITOR_MAP_BUILDER_H

#include "classes/col_file.h"
#include "classes/item_def.h"
#include "classes/item_placement.h"
#include "classes/tdfx.h"
#include "classes/tdfx_light.h"

#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/templates/hash_map.hpp>
#include <godot_cpp/templates/vector.hpp>

#include <memory>

using namespace godot;

/// Builds and streams the GTA San Andreas world map.
///
/// On _ready():
///   - Initializes AssetLoader (resolves game path, loads IMG archive)
///   - Reads gta.dat to discover IDE, IPL, COLFILE, and IMG entries
///   - Parses all item definitions, placements, and collision files
///   - Links 2DFX children and collision data to their parent items
///   - Assigns placements to a spatial grid for efficient streaming
///
/// On _process():
///   - Checks camera position against the spatial grid
///   - Spawns/despawns placement instances based on streaming distance
class MapBuilder : public Node {
	GDCLASS(MapBuilder, Node);

public:
	MapBuilder();
	~MapBuilder();

	void _ready() override;
	void _process(double p_delta) override;

	/// Streaming distance (how far the camera can see models).
	float streaming_distance = 100.0f;

	float get_streaming_distance() const;
	void set_streaming_distance(float p_dist);

	/// Maximum number of placement instances to spawn per frame to avoid stuttering.
	int spawns_per_frame_limit = 50;

	int get_spawns_per_frame_limit() const;
	void set_spawns_per_frame_limit(int p_limit);

protected:
	static void _bind_methods();

private:
	// ── Data stores ──────────────────────────────────────────────────────
	HashMap<int, std::shared_ptr<ItemDef>> items; // ID → ItemDef
	Vector<std::shared_ptr<TDFX>> item_children; // 2DFX effects pending linkage
	Vector<std::shared_ptr<ItemPlacement>> placements; // All world placements
	Vector<std::shared_ptr<ColFile>> collisions; // All collision files

	// ── Scene nodes ──────────────────────────────────────────────────────
	Node3D *map_root = nullptr;
	Camera3D *camera = nullptr;

	// ── Streaming state ──────────────────────────────────────────────────

	/// Key for spatial grid cells (integer grid coordinates).
	struct CellCoord {
		int x = 0;
		int z = 0;
		bool operator==(const CellCoord &other) const { return x == other.x && z == other.z; }
	};

	/// Hash function for CellCoord used in HashMap.
	struct CellCoordHash {
		static _FORCE_INLINE_ uint32_t hash(const CellCoord &c) {
			// Combine x and z with a simple hash mix
			uint32_t h = static_cast<uint32_t>(c.x) * 73856093;
			h ^= static_cast<uint32_t>(c.z) * 19349663;
			return h;
		}
	};

	/// Spatial grid: maps cell coordinates to indices into the placements array.
	/// Populated once after all placements are loaded, then read-only during streaming.
	static constexpr float CELL_SIZE = 200.0f;
	HashMap<CellCoord, Vector<int>, CellCoordHash> spatial_grid;

	/// Active (spawned) instances: placement index → scene node.
	HashMap<int, Node3D *> active_instances;

	/// Populate the spatial grid from the placements array.
	void _build_spatial_grid();

	/// Get the cell coordinate for a world position.
	CellCoord _cell_for_position(const Vector3 &pos) const;

	// ── Parsing helpers ──────────────────────────────────────────────────

	/// Read a section-based GTA data file (IDE or IPL).
	/// Calls the handler for each non-comment, non-section-header line.
	void _read_map_data(const String &path,
						void (MapBuilder::*handler)(const String &, const PackedStringArray &, const String &),
						const String &context);

	/// Parse one line from an IDE file (item definitions).
	void _read_ide_line(const String &section, const PackedStringArray &tokens, const String &context);

	/// Parse one line from a text IPL file (item placements).
	void _read_ipl_line(const String &section, const PackedStringArray &tokens, const String &context);

	/// Try to load binary IPL stream files (e.g., countryN_stream0.ipl).
	void _load_binary_ipl_streams(const String &base_ipl_path);

	/// Parse a single binary IPL stream file from the IMG archive.
	void _parse_binary_ipl(const String &asset_name);

	/// Clear the current map and create a fresh root node.
	void _clear_map();

	/// Spawn a placement instance as a scene node. Returns null if the item is unknown.
	Node3D *_spawn_placement(const std::shared_ptr<ItemPlacement> &ipl);
};

#endif // GTAEDITOR_MAP_BUILDER_H
