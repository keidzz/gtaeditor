#ifndef MAP_EXPORTER_H
#define MAP_EXPORTER_H

#include "classes/dat_parser.h"
#include "classes/gta_path_resolver.h"
#include "classes/ide_parser.h"
#include "classes/img_archive.h"
#include "classes/ipl_parser.h"
#include "classes/item_definition.h"
#include "classes/item_placement.h"
#include "classes/model_collection.h"
#include "classes/texture_collection.h"

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/templates/hash_map.hpp>
#include <godot_cpp/templates/hash_set.hpp>
#include <godot_cpp/templates/vector.hpp>

namespace godot {

class MapExporter : public Node {
	GDCLASS(MapExporter, Node)

public:
	MapExporter();
	~MapExporter();

	void _process(double p_delta) override;

	void start_export(const String &p_export_dir, const String &p_gta_path, const String &p_blender_path, const Array &p_regions);
	void cancel_export();
	Array get_available_regions(const String &p_gta_path);

	bool is_exporting() const;
	int32_t get_total_models() const;
	int32_t get_exported_models() const;
	int32_t get_remaining_models() const;
	float get_progress() const;
	String get_status_text() const;

protected:
	static void _bind_methods();

private:
	struct RegionData {
		String name;
		Vector<ItemPlacement> placements;
	};

	struct ExportItem {
		String region_name;
		ItemPlacement placement;
		ItemDefinition definition;
	};

	bool exporting = false;
	bool cancelled = false;
	int32_t process_index = 0;
	int32_t exported_models = 0;
	int32_t total_models = 0;
	String status_text;
	String export_dir;
	String blender_path;

	GtaPathResolver path_resolver;
	ImgArchive img_archive;
	HashMap<int32_t, ItemDefinition> definitions;
	ModelCollection models;
	TextureCollection textures;
	Vector<ExportItem> export_items;
	HashMap<String, Array> region_instances;
	HashSet<String> exported_model_names;
	HashSet<String> exported_texture_keys;
	HashSet<String> selected_regions;
	Dictionary manifest_models;

	bool prepare_export(const String &p_export_dir, const String &p_gta_path, const String &p_blender_path, const Array &p_regions);
	void load_dat_file(const String &p_dat_path);
	void load_ide_file(const String &p_ide_path);
	void load_region_ipl(const String &p_ipl_path);
	void index_img_assets();
	void resolve_region_lods(RegionData &p_region);
	void collect_region_items(const RegionData &p_region);

	void process_next_item();
	bool export_model_if_needed(const ItemDefinition &p_def);
	bool write_gltf(const String &p_model_name, const Ref<ArrayMesh> &p_mesh, const String &p_path, const Array &p_materials);
	Array export_materials(const String &p_model_name, const ItemDefinition &p_def);
	String export_texture(const String &p_txd_name, const String &p_texture_name, bool &r_has_alpha);
	Dictionary build_instance_dict(const ExportItem &p_item);

	void finish_export();
	void write_manifest();
	void write_blender_script();
	void write_blender_batch(const String &p_executable);
	bool launch_blender();
	void emit_progress_signal();

	String normalize_dir(String p_path) const;
	String sanitize_name(const String &p_name) const;
	String region_name_from_path(const String &p_ipl_path) const;
	String resolve_blender_executable(const String &p_path) const;
};

} // namespace godot

#endif // MAP_EXPORTER_H
