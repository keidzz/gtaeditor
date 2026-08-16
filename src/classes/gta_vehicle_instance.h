#ifndef GTA_VEHICLE_INSTANCE_H
#define GTA_VEHICLE_INSTANCE_H

#include "gta_resource_provider.h"
#include "map_material.h"

#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/static_body3d.hpp>
#include <godot_cpp/variant/string.hpp>

namespace godot {

// =============================================================================
// GTAVehicleInstance — Editor-placeable Node3D dedicated to GTA SA vehicles.
//
// Unlike GTAModelInstance (which merges a whole DFF clump into one static
// mesh — fine for props, wrong for vehicles), this class walks the DFF's
// per-frame data (see DffResult::frames in rw/dff_parser.h) and spawns each
// named part — chassis, wheel_lf_dummy, door_lf_dummy, bonnet_dummy, etc. —
// as its own positioned MeshInstance3D. That's what SA vehicle DFFs actually
// are: since GTA SA (unlike III/VC), wheel geometry is embedded directly in
// the vehicle's own DFF rather than loaded from a separate wheels.dff.
//
// It also substitutes GTA's reserved "paintable" placeholder material colors
// with this instance's own primary/secondary/tertiary/quaternary colors
// (see VehiclePaintColors in map_material.h) — without this, paintable
// surfaces render as literal green/magenta, which is what raw, un-recolored
// DFF materials look like.
//
// Damaged-part variants (frame names ending in "_dam") and embedded LOD
// duplicates ("_l1", "_l2", "_vlo") are skipped, since GTA SA vehicle DFFs
// carry both the undamaged and damaged geometry for most panels.
//
// Fully self-contained, same as GTAModelInstance: resolves models/textures
// through the shared GtaResourceProvider singleton, no MapBuilder required.
// =============================================================================

class GTAVehicleInstance : public Node3D {
	GDCLASS(GTAVehicleInstance, Node3D)

public:
	GTAVehicleInstance();
	~GTAVehicleInstance();

	void _ready() override;

	void set_gta_path(const String &p_path);
	String get_gta_path() const;

	void set_model_id(int32_t p_id);
	int32_t get_model_id() const;

	void set_model_name(const String &p_name);
	String get_model_name() const;

	void set_use_model_collision(bool p_enabled);
	bool get_use_model_collision() const;

	// Same visibility_range-based fade as GTAModelInstance::use_streaming.
	void set_use_streaming(bool p_enabled);
	bool get_use_streaming() const;

	void set_primary_color(const Color &p_color);
	Color get_primary_color() const;

	void set_secondary_color(const Color &p_color);
	Color get_secondary_color() const;

	void set_tertiary_color(const Color &p_color);
	Color get_tertiary_color() const;

	void set_quaternary_color(const Color &p_color);
	Color get_quaternary_color() const;

	// Quick-pick palette (Custom, Black, White, Red, ...). Applying a preset
	// (anything other than Custom) overwrites primary_color/secondary_color
	// (and tertiary/quaternary, matched to secondary) with that preset's
	// colors — a faster starting point than typing RGB values by hand. Picking
	// Custom just leaves whatever colors are currently set alone.
	void set_color_preset(int p_preset);
	int get_color_preset() const;

	// Re-resolves and (re)builds all child parts from the current
	// model_id/model_name/gta_path/paint colors.
	void refresh_vehicle();

protected:
	static void _bind_methods();

private:
	String gta_path = "res://gta/";
	int32_t model_id = -1;
	String model_name;
	bool use_model_collision = false;
	bool use_streaming = false;

	VehiclePaintColors paint;
	int color_preset = 0; // 0 = Custom

	// Every part spawned by the last refresh_vehicle() call, so it can be
	// torn down cleanly before rebuilding (model changed, paint changed, ...).
	Vector<MeshInstance3D *> part_nodes;
	StaticBody3D *collision_body = nullptr;

	void clear_parts();
	static bool should_skip_frame(const String &p_frame_name);
};

} // namespace godot

#endif // GTA_VEHICLE_INSTANCE_H
