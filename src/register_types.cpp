#include "register_types.h"
#include "map_exporter.h"
#include "map_builder.h"

#include <gdextension_interface.h>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/defs.hpp>
#include <godot_cpp/godot.hpp>
#include "classes/gta_model_instance.h"
#include "classes/gta_vehicle_instance.h"
#include "classes/gta_ped_on_foot.h"
#include "rw/gta_dff_geometry.h"
#include "rw/gta_dff_skeleton.h"
#include "rw/gta_ifp_animation.h"
#include "rw/gta_img_archive.h"
#include "rw/gta_txd_texture.h"
using namespace godot;

void initialize_gdextension_types(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}

	ClassDB::register_class<MapBuilder>();
	ClassDB::register_class<MapExporter>();
	ClassDB::register_class<GTAModelInstance>();
	ClassDB::register_class<GTAVehicleInstance>();
	ClassDB::register_class<GTAPedOnFoot>();
	ClassDB::register_class<GTAImgArchive>();
	ClassDB::register_class<GTAIfpAnimation>();
	ClassDB::register_class<GTADffSkeleton>();
	ClassDB::register_class<GTADffGeometry>();
	ClassDB::register_class<GTATxdTexture>();
}

void uninitialize_gdextension_types(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}
}

extern "C" {
// Initialization
GDExtensionBool GDE_EXPORT gtaeditor_library_init(GDExtensionInterfaceGetProcAddress p_get_proc_address, GDExtensionClassLibraryPtr p_library, GDExtensionInitialization *r_initialization) {
	GDExtensionBinding::InitObject init_obj(p_get_proc_address, p_library, r_initialization);
	init_obj.register_initializer(initialize_gdextension_types);
	init_obj.register_terminator(uninitialize_gdextension_types);
	init_obj.set_minimum_library_initialization_level(MODULE_INITIALIZATION_LEVEL_SCENE);

	return init_obj.init();
}
}
