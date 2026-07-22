#ifndef MAP_BUILDER_H
#define MAP_BUILDER_H

#include "classes/dat_parser.h"
#include "classes/gta_path_resolver.h"
#include "classes/gta_resource_provider.h"
#include "classes/ide_parser.h"
#include "classes/img_archive.h"
#include "classes/ipl_parser.h"
#include "classes/item_definition.h"
#include "classes/item_placement.h"
#include "classes/map_material.h"
#include "classes/map_water_loader.h"
#include "classes/model_collection.h"
#include "classes/texture_collection.h"

#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/omni_light3d.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/templates/hash_map.hpp>
#include <godot_cpp/templates/hash_set.hpp>
#include <godot_cpp/templates/vector.hpp>

namespace godot {

// =============================================================================
// MapBuilder — Main GDExtension Node3D that loads the GTA SA map.
//
// On _ready(), it resolves the shared GtaResourceProvider (parsing game data
// files / indexing the IMG archive at most once per session), then loads its
// own placement (IPL) data and streams placements in/out via _process()
// based on distance to the active camera. Godot's visibility_range is used
// for distance-based fade in addition to the manual add/remove streaming.
//
// By default this only runs at actual runtime; set load_map_in_editor to
// also stream while editing the scene, using the editor's 3D viewport camera.
// =============================================================================

class MapBuilder : public Node3D {
	GDCLASS(MapBuilder, Node3D)

public:
	MapBuilder();
	~MapBuilder();

	void _ready() override;
	void _process(double delta) override;

	// -- Exported property accessors --
	void set_streaming_distance(float p_dist);
	float get_streaming_distance() const;

	void set_draw_distance_multiplier(float p_mult);
	float get_draw_distance_multiplier() const;

	void set_debug_enabled(bool p_enabled);
	bool get_debug_enabled() const;

	void set_load_interiors(bool p_load);
	bool get_load_interiors() const;

	void set_load_collisions(bool p_load);
	bool get_load_collisions() const;

	void set_load_water(bool p_load);
	bool get_load_water() const;

	void set_streetlight_energy(float p_energy);
	float get_streetlight_energy() const;

	void set_streetlight_shadows(bool p_enabled);
	bool get_streetlight_shadows() const;

	void set_gta_path(const String &p_path);
	String get_gta_path() const;

	// When enabled, the map loads and streams while simply editing the scene
	// (no Play required), using the editor's active 3D viewport camera as the
	// streaming origin instead of the runtime game camera.
	void set_load_map_in_editor(bool p_enabled);
	bool get_load_map_in_editor() const;

	// -- Shared-resource access -------------------------------------------------
	// Lets other nodes (e.g. GTAModelInstance) reuse the SAME ImgArchive-backed
	// ModelCollection/TextureCollection and parsed IDE definitions instead of
	// opening a second copy of the IMG archive / parsing IDE files again.
	bool is_loaded() const;
	ModelCollection *get_model_collection();
	TextureCollection *get_texture_collection();

	// Looks up a parsed IDE definition by its numeric id. Forwards to the
	// shared GtaResourceProvider. See ide_parser.h for which sections
	// (OBJS/TOBJ/ANIM/CARS) are currently indexed.
	bool find_definition(int32_t p_id, ItemDefinition &r_definition);

	// Looks up a parsed IDE definition by model (.dff) name, case-insensitive.
	bool find_definition_by_model_name(const String &p_model_name, ItemDefinition &r_definition);

protected:
	static void _bind_methods();

private:
	// -- Exported properties --
	float streaming_distance = 300.0f;
	float draw_distance_multiplier = 1.5f;
	bool debug_enabled = true;
	bool load_interiors = false;
	bool load_collisions = true;
	bool load_water = true;
	float streetlight_energy = 1.0f;
	bool streetlight_shadows = false;
	String gta_path = "res://gta/";
	bool load_map_in_editor = false;

	// -- Internal state --
	bool loaded = false;
	// Models/textures/IDE definitions/IMG archive are now owned by the shared
	// GtaResourceProvider singleton (set on first load_map()) instead of by
	// MapBuilder itself, so GTAModelInstance/GTAVehicleInstance can reuse the
	// exact same loaded data without needing a MapBuilder node in the scene.
	GtaResourceProvider *resources = nullptr;
	Vector<ItemPlacement> placements;

	// -- Streaming state --
	int stream_process_index = 0;
	Vector<Node3D *> spawned_nodes;
	Vector<OmniLight3D *> spawned_lights;
	Vector<int32_t> stream_order; // Indices into placements, sorted by distance to camera
	Vector3 last_sort_position; // Camera position at last stream_order sort
	Node *time_of_day = nullptr;
	float last_night_light_factor = -1.0f;

	// -- Loading methods --
	// NOTE: IDE parsing and IMG asset indexing (load_ide_file/index_img_assets)
	// moved to GtaResourceProvider — MapBuilder's load_dat_file() now only
	// handles the placement-specific parts (COL files, text/streaming IPLs,
	// LOD resolution) that depend on this MapBuilder's own `placements` array.
	void load_map();
	void load_dat_file(const String &p_dat_path);
	void load_text_ipl(const String &p_ipl_path);
	void load_streaming_ipls();
	void resolve_lods();
	void spawn_all();

	// -- Spawning methods --
	Node3D *spawn_placement(int32_t p_index);
	void spawn_2dfx_lights(Node3D *p_placement_root, const ItemDefinition &p_definition);
	void remove_2dfx_lights(Node3D *p_placement_root);
	void update_2dfx_lights();
	float get_night_light_factor(float p_hour) const;

	// -- Streaming order --
	void sort_stream_order(const Vector3 &cam_pos);

	// Returns the editor's active 3D viewport camera when load_map_in_editor
	// is enabled and we're running inside the editor; otherwise the normal
	// runtime viewport camera (get_viewport()->get_camera_3d()), same as before.
	Camera3D *get_active_camera() const;
};

} // namespace godot

#endif // MAP_BUILDER_H
