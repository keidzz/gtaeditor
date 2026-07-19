#ifndef GTA_MODEL_INSTANCE_H
#define GTA_MODEL_INSTANCE_H

// NOTE: adjust this path if map_builder.h doesn't live one directory above
// classes/ in your tree — it must resolve to the same map_builder.h that
// ships with the importer.
#include "../map_builder.h"

#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/variant/node_path.hpp>
#include <godot_cpp/variant/string.hpp>

namespace godot {

// =============================================================================
// GTAModelInstance — Editor-placeable Node3D that spawns a single GTA model
// by IDE id or by .dff name.
//
// It intentionally does NOT open its own IMG archive or keep its own model
// cache: it looks up an existing MapBuilder node (assigned via
// map_builder_path) and pulls the mesh/materials from that MapBuilder's
// already-loaded ModelCollection/TextureCollection. This means:
//   - The model is only parsed once, no matter how many GTAModelInstance
//     nodes reference it (ModelCollection's own lazy cache is shared).
//   - A GTAModelInstance placed in the editor will only resolve once its
//     MapBuilder has finished loading (see map_loaded signal on MapBuilder).
//
// Limitation: model_id lookups only work for objects defined in the IDE
// OBJS/TOBJ/ANIM sections, because IdeParser does not currently parse the
// CARS (vehicle) section. Vehicles must be spawned by model_name.
// =============================================================================

class GTAModelInstance : public Node3D {
	GDCLASS(GTAModelInstance, Node3D)

public:
	GTAModelInstance();
	~GTAModelInstance();

	void _ready() override;

	void set_map_builder_path(const NodePath &p_path);
	NodePath get_map_builder_path() const;

	void set_model_id(int32_t p_id);
	int32_t get_model_id() const;

	void set_model_name(const String &p_name);
	String get_model_name() const;

	// Re-resolves and (re)builds the child MeshInstance3D from the current
	// model_id/model_name. Called automatically by the property setters and
	// by _ready(); exposed publicly/bound so it can also be used as the
	// target of MapBuilder's "map_loaded" signal and called manually from
	// GDScript if ever needed.
	void refresh_model();

protected:
	static void _bind_methods();

private:
	NodePath map_builder_path;
	int32_t model_id = -1;
	String model_name;

	MeshInstance3D *mesh_instance = nullptr;
	bool waiting_for_map = false;

	MapBuilder *find_map_builder() const;
};

} // namespace godot

#endif // GTA_MODEL_INSTANCE_H
