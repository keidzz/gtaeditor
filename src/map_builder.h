#ifndef MAP_BUILDER_H
#define MAP_BUILDER_H

#include "classes/dat_parser.h"
#include "classes/gta_path_resolver.h"
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
#include <godot_cpp/classes/standard_material3d.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/templates/hash_map.hpp>
#include <godot_cpp/templates/hash_set.hpp>
#include <godot_cpp/templates/vector.hpp>

namespace godot {

// =============================================================================
// MapBuilder — Main GDExtension Node3D that loads the GTA SA map.
//
// On _ready(), it parses all game data files and spawns all objects at once
// (matching the Unity reference behavior). Godot's visibility_range is used
// for distance-based culling instead of manual streaming.
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

	void set_gta_path(const String &p_path);
	String get_gta_path() const;

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
	String gta_path = "res://gta/";

	// -- Internal state --
	bool loaded = false;
	GtaPathResolver path_resolver;
	ImgArchive img_archive;
	HashMap<int32_t, ItemDefinition> definitions;
	Vector<ItemPlacement> placements;
	ModelCollection models;
	TextureCollection textures;

	// -- Streaming state --
	int stream_process_index = 0;
	Vector<MeshInstance3D *> spawned_nodes;
	Vector<int32_t> stream_order; // Indices into placements, sorted by distance to camera
	Vector3 last_sort_position; // Camera position at last stream_order sort

	// -- Loading methods --
	void load_map();
	void load_dat_file(const String &p_dat_path);
	void load_ide_file(const String &p_ide_path);
	void load_text_ipl(const String &p_ipl_path);
	void load_streaming_ipls();
	void resolve_lods();
	void index_img_assets();
	void spawn_all();

	// -- Spawning methods --
	MeshInstance3D *spawn_placement(int32_t p_index);

	// -- Streaming order --
	void sort_stream_order(const Vector3 &cam_pos);
};

} // namespace godot

#endif // MAP_BUILDER_H
