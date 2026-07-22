#ifndef GTA_MODEL_INSTANCE_H
#define GTA_MODEL_INSTANCE_H

#include "gta_resource_provider.h"

#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/static_body3d.hpp>
#include <godot_cpp/variant/node_path.hpp>
#include <godot_cpp/variant/string.hpp>

namespace godot {

// =============================================================================
// GTAModelInstance — Editor-placeable Node3D that spawns a single GTA model
// by IDE id or by .dff name.
//
// Fully self-contained: it resolves its own copy of the game data through
// the shared GtaResourceProvider singleton (see gta_resource_provider.h),
// which is loaded at most once per session no matter how many
// GTAModelInstance/GTAVehicleInstance/MapBuilder nodes ask for it. It does
// NOT require a MapBuilder node anywhere in the scene.
//
// Limitation: model_id lookups only work for ids IdeParser actually indexes
// (OBJS/TOBJ/ANIM, and CARS if that section is enabled — see ide_parser.h).
// Vehicles are best spawned by model_name via GTAVehicleInstance instead,
// which also handles paint colors, wheels, and per-part positioning that
// this class does not.
// =============================================================================

class GTAModelInstance : public Node3D {
	GDCLASS(GTAModelInstance, Node3D)

public:
	GTAModelInstance();
	~GTAModelInstance();

	void _ready() override;

	// Path to the player's own legally-owned GTA:SA installation (or a
	// res://-relative folder containing it). Same default/format as
	// MapBuilder.gta_path — if a MapBuilder elsewhere in the project already
	// loaded resources from this same path, this is a no-op reuse.
	void set_gta_path(const String &p_path);
	String get_gta_path() const;

	void set_model_id(int32_t p_id);
	int32_t get_model_id() const;

	void set_model_name(const String &p_name);
	String get_model_name() const;

	// Spawns a StaticBody3D+CollisionShape3D child using the model's own
	// GTA collision data (ModelCollection::get_col_model()), same mechanism
	// MapBuilder uses for streamed placements.
	void set_use_model_collision(bool p_enabled);
	bool get_use_model_collision() const;

	// Fades the model out beyond the IDE definition's draw_distance using
	// Godot's visibility_range (the same mechanism MapBuilder uses for its
	// own streamed placements). NOTE: this only fades the model out — it does
	// not swap in a separate LOD mesh. True HD<->LOD swapping in GTA is a
	// relationship between two paired IPL placements, which a standalone,
	// hand-placed instance doesn't have.
	void set_use_streaming(bool p_enabled);
	bool get_use_streaming() const;

	// Re-resolves and (re)builds the child MeshInstance3D from the current
	// model_id/model_name/gta_path. Called automatically by the property
	// setters and by _ready(); exposed/bound so it can be called manually
	// from GDScript too.
	void refresh_model();

protected:
	static void _bind_methods();

private:
	String gta_path = "res://gta/";
	int32_t model_id = -1;
	String model_name;
	bool use_model_collision = false;
	bool use_streaming = false;

	MeshInstance3D *mesh_instance = nullptr;
	StaticBody3D *collision_body = nullptr;
};

} // namespace godot

#endif // GTA_MODEL_INSTANCE_H
