#include "map_exporter.h"

#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/json.hpp>
#include <godot_cpp/classes/mesh.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <cfloat>

namespace godot {

MapExporter::MapExporter() {
	set_process(false);
}

MapExporter::~MapExporter() {
	models.clear();
	textures.clear();
}

void MapExporter::_process(double p_delta) {
	if (!exporting) {
		return;
	}

	if (cancelled) {
		exporting = false;
		status_text = "Export cancelled.";
		emit_signal("export_finished", false, status_text);
		set_process(false);
		return;
	}

	const uint64_t frame_start = Time::get_singleton()->get_ticks_msec();
	while (exporting && process_index < export_items.size()) {
		process_next_item();
		if (Time::get_singleton()->get_ticks_msec() - frame_start > 30) {
			break;
		}
	}

	if (exporting && process_index >= export_items.size()) {
		finish_export();
	}
}

void MapExporter::start_export(const String &p_export_dir, const String &p_gta_path, const String &p_blender_path, const Array &p_regions) {
	if (exporting) {
		return;
	}

	if (!prepare_export(p_export_dir, p_gta_path, p_blender_path, p_regions)) {
		emit_signal("export_finished", false, status_text);
		return;
	}

	exporting = true;
	cancelled = false;
	set_process(true);
	emit_progress_signal();
}

void MapExporter::cancel_export() {
	cancelled = true;
}

Array MapExporter::get_available_regions(const String &p_gta_path) {
	Array regions;
	String abs_gta_path = p_gta_path;
	if (abs_gta_path.is_empty()) {
		abs_gta_path = "res://gta/";
	}
	if (abs_gta_path.begins_with("res://")) {
		abs_gta_path = ProjectSettings::get_singleton()->globalize_path(abs_gta_path);
	}
	GtaPathResolver resolver;
	resolver.set_root(normalize_dir(abs_gta_path));

	HashSet<String> seen;
	Vector<String> dat_paths;
	String default_dat_path = resolver.resolve("data/default.dat");
	if (!default_dat_path.is_empty()) {
		dat_paths.push_back(default_dat_path);
	}
	String gta_dat_path = resolver.resolve("data/gta.dat");
	if (!gta_dat_path.is_empty()) {
		dat_paths.push_back(gta_dat_path);
	}

	for (int d = 0; d < dat_paths.size(); d++) {
		DatResult dat = DatParser::parse(dat_paths[d]);
		for (int i = 0; i < dat.ipl_paths.size(); i++) {
			String region = region_name_from_path(dat.ipl_paths[i]);
			if (!region.is_empty() && !seen.has(region)) {
				seen.insert(region);
				regions.push_back(region);
			}
		}
	}

	regions.sort();
	return regions;
}

bool MapExporter::is_exporting() const {
	return exporting;
}

int32_t MapExporter::get_total_models() const {
	return total_models;
}

int32_t MapExporter::get_exported_models() const {
	return exported_models;
}

int32_t MapExporter::get_remaining_models() const {
	return MAX(0, total_models - exported_models);
}

float MapExporter::get_progress() const {
	if (total_models <= 0) {
		return 0.0f;
	}
	return static_cast<float>(exported_models) / static_cast<float>(total_models);
}

String MapExporter::get_status_text() const {
	return status_text;
}

bool MapExporter::prepare_export(const String &p_export_dir, const String &p_gta_path, const String &p_blender_path, const Array &p_regions) {
	export_dir = normalize_dir(p_export_dir);
	blender_path = p_blender_path.strip_edges();
	if (blender_path.is_empty()) {
		blender_path = "blender";
	}

	process_index = 0;
	exported_models = 0;
	total_models = 0;
	export_items.clear();
	region_instances.clear();
	exported_model_names.clear();
	exported_texture_keys.clear();
	selected_regions.clear();
	manifest_models.clear();
	definitions.clear();
	models.clear();
	textures.clear();

	for (int i = 0; i < p_regions.size(); i++) {
		selected_regions.insert(sanitize_name(String(p_regions[i])));
	}

	if (export_dir.is_empty()) {
		status_text = "Choose an export directory.";
		return false;
	}

	if (p_regions.is_empty()) {
		status_text = "Select at least one region to export.";
		return false;
	}

	DirAccess::make_dir_recursive_absolute(export_dir);
	DirAccess::make_dir_recursive_absolute(export_dir + "models");
	DirAccess::make_dir_recursive_absolute(export_dir + "textures");

	String abs_gta_path = p_gta_path;
	if (abs_gta_path.is_empty()) {
		abs_gta_path = "res://gta/";
	}
	if (abs_gta_path.begins_with("res://")) {
		abs_gta_path = ProjectSettings::get_singleton()->globalize_path(abs_gta_path);
	}
	abs_gta_path = normalize_dir(abs_gta_path);
	path_resolver.set_root(abs_gta_path);

	String img_path = path_resolver.resolve("models/gta3.img");
	if (img_path.is_empty() || !img_archive.load(img_path)) {
		status_text = "Could not load models/gta3.img.";
		return false;
	}

	String default_dat_path = path_resolver.resolve("data/default.dat");
	if (!default_dat_path.is_empty()) {
		load_dat_file(default_dat_path);
	}

	String gta_dat_path = path_resolver.resolve("data/gta.dat");
	if (!gta_dat_path.is_empty()) {
		load_dat_file(gta_dat_path);
	}

	index_img_assets();
	total_models = export_items.size();
	status_text = String("Ready to export ") + String::num_int64(total_models) + " HD models.";
	return total_models > 0;
}

void MapExporter::load_dat_file(const String &p_dat_path) {
	DatResult dat = DatParser::parse(p_dat_path);

	for (int i = 0; i < dat.ide_paths.size(); i++) {
		load_ide_file(dat.ide_paths[i]);
	}

	for (int i = 0; i < dat.ipl_paths.size(); i++) {
		load_region_ipl(dat.ipl_paths[i]);
	}
}

void MapExporter::load_ide_file(const String &p_ide_path) {
	String resolved = path_resolver.resolve(p_ide_path);
	if (resolved.is_empty()) {
		return;
	}

	IdeResult result = IdeParser::parse(resolved);
	for (const KeyValue<int32_t, ItemDefinition> &kv : result.definitions) {
		definitions[kv.key] = kv.value;
	}
	for (int i = 0; i < result.texture_parents.size(); i++) {
		textures.add_parent(result.texture_parents[i].child_name, result.texture_parents[i].parent_name);
	}
}

void MapExporter::load_region_ipl(const String &p_ipl_path) {
	RegionData region;
	region.name = region_name_from_path(p_ipl_path);
	if (!selected_regions.is_empty() && !selected_regions.has(region.name)) {
		return;
	}

	String resolved = path_resolver.resolve(p_ipl_path);
	if (!resolved.is_empty()) {
		region.placements.append_array(IplParser::parse_text(resolved));
	}

	String clean_path = p_ipl_path.replace("\\", "/");
	String basename = clean_path.get_file().get_basename().to_lower();
	for (int stream_idx = 0;; stream_idx++) {
		String stream_name = basename + "_stream" + String::num_int64(stream_idx) + ".ipl";
		if (!img_archive.has_entry(stream_name)) {
			break;
		}

		PackedByteArray data = img_archive.read_entry(stream_name);
		if (data.size() < 4 || data[0] != 'b' || data[1] != 'n' || data[2] != 'r' || data[3] != 'y') {
			break;
		}

		Vector<ItemPlacement> stream_placements = IplParser::parse_binary(data);
		for (int i = 0; i < stream_placements.size(); i++) {
			int32_t def_id = stream_placements[i].definition_id;
			if (definitions.has(def_id)) {
				stream_placements.ptrw()[i].item_name = definitions[def_id].model_name.to_lower();
				stream_placements.ptrw()[i].draw_distance = definitions[def_id].draw_distance;
			}
		}
		region.placements.append_array(stream_placements);
	}

	resolve_region_lods(region);
	collect_region_items(region);
}

void MapExporter::index_img_assets() {
	Vector<String> dff_entries = img_archive.get_entries_with_extension(".dff");
	for (int i = 0; i < dff_entries.size(); i++) {
		models.register_dff(dff_entries[i], &img_archive);
	}

	Vector<String> txd_entries = img_archive.get_entries_with_extension(".txd");
	for (int i = 0; i < txd_entries.size(); i++) {
		textures.register_txd(txd_entries[i], &img_archive);
	}
}

void MapExporter::resolve_region_lods(RegionData &p_region) {
	for (int idx = 0; idx < p_region.placements.size(); idx++) {
		int32_t lod_idx = p_region.placements[idx].lod_index;
		if (lod_idx < 0 || lod_idx >= p_region.placements.size()) {
			continue;
		}

		int32_t def_id = p_region.placements[idx].definition_id;
		float hd_dist = definitions.has(def_id) ? definitions[def_id].draw_distance : p_region.placements[idx].draw_distance;
		if (p_region.placements[lod_idx].lod_begin_distance < 0.0f) {
			p_region.placements.ptrw()[lod_idx].lod_begin_distance = hd_dist;
		} else {
			p_region.placements.ptrw()[lod_idx].lod_begin_distance = MAX(p_region.placements[lod_idx].lod_begin_distance, hd_dist);
		}
	}
}

void MapExporter::collect_region_items(const RegionData &p_region) {
	for (int i = 0; i < p_region.placements.size(); i++) {
		const ItemPlacement &placement = p_region.placements[i];
		if (placement.lod_begin_distance >= 0.0f || !definitions.has(placement.definition_id)) {
			continue;
		}

		ExportItem item;
		item.region_name = p_region.name;
		item.placement = placement;
		item.definition = definitions[placement.definition_id];
		export_items.push_back(item);

		if (!region_instances.has(p_region.name)) {
			region_instances[p_region.name] = Array();
		}
	}
}

void MapExporter::process_next_item() {
	const ExportItem &item = export_items[process_index];
	String model_name = item.definition.model_name.to_lower();
	status_text = String("Exporting ") + item.region_name + "/" + model_name;

	if (export_model_if_needed(item.definition)) {
		Array instances = region_instances[item.region_name];
		instances.push_back(build_instance_dict(item));
		region_instances[item.region_name] = instances;
	}

	process_index++;
	exported_models = process_index;
	emit_progress_signal();
}

bool MapExporter::export_model_if_needed(const ItemDefinition &p_def) {
	String model_name = p_def.model_name.to_lower();
	if (exported_model_names.has(model_name)) {
		return true;
	}
	exported_model_names.insert(model_name);

	Array material_specs = export_materials(model_name, p_def);
	String gltf_path = export_dir + "models/" + sanitize_name(model_name) + ".gltf";
	Ref<ArrayMesh> mesh = models.get_mesh(model_name);
	if (mesh.is_null() || mesh->get_surface_count() == 0) {
		return false;
	}

	if (!write_gltf(model_name, mesh, gltf_path, material_specs)) {
		return false;
	}

	Dictionary model;
	model["gltf"] = gltf_path;
	manifest_models[model_name] = model;
	return true;
}

static void pad_binary_file(const Ref<FileAccess> &p_file, int64_t &r_offset) {
	while ((r_offset % 4) != 0) {
		p_file->store_8(0);
		r_offset++;
	}
}

static String json_num(float p_value) {
	return String::num(p_value);
}

bool MapExporter::write_gltf(const String &p_model_name, const Ref<ArrayMesh> &p_mesh, const String &p_path, const Array &p_materials) {
	String bin_path = p_path.get_basename() + ".bin";
	Ref<FileAccess> bin = FileAccess::open(bin_path, FileAccess::WRITE);
	if (bin.is_null()) {
		return false;
	}

	String buffer_views = "";
	String accessors = "";
	String primitives = "";
	int accessor_count = 0;
	int view_count = 0;
	int64_t byte_offset = 0;

	for (int s = 0; s < p_mesh->get_surface_count(); s++) {
		Array arrays = p_mesh->surface_get_arrays(s);
		PackedVector3Array vertices = arrays[Mesh::ARRAY_VERTEX];
		PackedVector3Array normals = arrays[Mesh::ARRAY_NORMAL];
		PackedVector2Array uvs = arrays[Mesh::ARRAY_TEX_UV];
		PackedColorArray colors = arrays[Mesh::ARRAY_COLOR];
		PackedInt32Array indices = arrays[Mesh::ARRAY_INDEX];
		if (vertices.is_empty() || indices.is_empty()) {
			continue;
		}

		int pos_view = view_count++;
		int pos_accessor = accessor_count++;
		int64_t pos_offset = byte_offset;
		Vector3 min_v = Vector3(FLT_MAX, FLT_MAX, FLT_MAX);
		Vector3 max_v = Vector3(-FLT_MAX, -FLT_MAX, -FLT_MAX);
		for (int i = 0; i < vertices.size(); i++) {
			Vector3 v = vertices[i];
			min_v.x = MIN(min_v.x, v.x);
			min_v.y = MIN(min_v.y, v.y);
			min_v.z = MIN(min_v.z, v.z);
			max_v.x = MAX(max_v.x, v.x);
			max_v.y = MAX(max_v.y, v.y);
			max_v.z = MAX(max_v.z, v.z);
			bin->store_float(v.x);
			bin->store_float(v.y);
			bin->store_float(v.z);
			byte_offset += 12;
		}
		pad_binary_file(bin, byte_offset);

		int normal_view = view_count++;
		int normal_accessor = accessor_count++;
		int64_t normal_offset = byte_offset;
		for (int i = 0; i < vertices.size(); i++) {
			Vector3 n = i < normals.size() ? normals[i] : Vector3(0, 1, 0);
			// Keep Blender's imported face orientation aligned with the Godot viewer
			// after the glTF Y-up to Blender Z-up conversion.
			bin->store_float(-n.x);
			bin->store_float(-n.y);
			bin->store_float(-n.z);
			byte_offset += 12;
		}
		pad_binary_file(bin, byte_offset);

		int uv_view = view_count++;
		int uv_accessor = accessor_count++;
		int64_t uv_offset = byte_offset;
		for (int i = 0; i < vertices.size(); i++) {
			Vector2 uv = i < uvs.size() ? uvs[i] : Vector2(0, 0);
			bin->store_float(uv.x);
			bin->store_float(uv.y);
			byte_offset += 8;
		}
		pad_binary_file(bin, byte_offset);

		int color_view = view_count++;
		int color_accessor = accessor_count++;
		int64_t color_offset = byte_offset;
		for (int i = 0; i < vertices.size(); i++) {
			Color c = i < colors.size() ? colors[i] : Color(1, 1, 1, 1);
			bin->store_float(c.r);
			bin->store_float(c.g);
			bin->store_float(c.b);
			bin->store_float(c.a);
			byte_offset += 16;
		}
		pad_binary_file(bin, byte_offset);

		int index_view = view_count++;
		int index_accessor = accessor_count++;
		int64_t index_offset = byte_offset;
		for (int i = 0; i + 2 < indices.size(); i += 3) {
			int32_t i0 = indices[i];
			int32_t i1 = indices[i + 2];
			int32_t i2 = indices[i + 1];

			if (i0 >= 0 && i1 >= 0 && i2 >= 0 &&
					i0 < vertices.size() && i1 < vertices.size() && i2 < vertices.size() &&
					i0 < normals.size() && i1 < normals.size() && i2 < normals.size()) {
				Vector3 face_normal = (vertices[i1] - vertices[i0]).cross(vertices[i2] - vertices[i0]);
				Vector3 vertex_normal = -(normals[i0] + normals[i1] + normals[i2]);
				if (face_normal.length_squared() > 0.000001f && vertex_normal.length_squared() > 0.000001f &&
						face_normal.dot(vertex_normal) > 0.0f) {
					SWAP(i1, i2);
				}
			}

			bin->store_32(static_cast<uint32_t>(i0));
			bin->store_32(static_cast<uint32_t>(i1));
			bin->store_32(static_cast<uint32_t>(i2));
			byte_offset += 12;
		}
		pad_binary_file(bin, byte_offset);

		if (!buffer_views.is_empty()) {
			buffer_views += ",";
		}
		buffer_views += "{\"buffer\":0,\"byteOffset\":" + String::num_int64(pos_offset) + ",\"byteLength\":" + String::num_int64(vertices.size() * 12) + ",\"target\":34962},";
		buffer_views += "{\"buffer\":0,\"byteOffset\":" + String::num_int64(normal_offset) + ",\"byteLength\":" + String::num_int64(vertices.size() * 12) + ",\"target\":34962},";
		buffer_views += "{\"buffer\":0,\"byteOffset\":" + String::num_int64(uv_offset) + ",\"byteLength\":" + String::num_int64(vertices.size() * 8) + ",\"target\":34962},";
		buffer_views += "{\"buffer\":0,\"byteOffset\":" + String::num_int64(color_offset) + ",\"byteLength\":" + String::num_int64(vertices.size() * 16) + ",\"target\":34962},";
		buffer_views += "{\"buffer\":0,\"byteOffset\":" + String::num_int64(index_offset) + ",\"byteLength\":" + String::num_int64(indices.size() * 4) + ",\"target\":34963}";

		if (!accessors.is_empty()) {
			accessors += ",";
		}
		accessors += "{\"bufferView\":" + String::num_int64(pos_view) + ",\"componentType\":5126,\"count\":" + String::num_int64(vertices.size()) + ",\"type\":\"VEC3\",\"min\":[" + json_num(min_v.x) + "," + json_num(min_v.y) + "," + json_num(min_v.z) + "],\"max\":[" + json_num(max_v.x) + "," + json_num(max_v.y) + "," + json_num(max_v.z) + "]},";
		accessors += "{\"bufferView\":" + String::num_int64(normal_view) + ",\"componentType\":5126,\"count\":" + String::num_int64(vertices.size()) + ",\"type\":\"VEC3\"},";
		accessors += "{\"bufferView\":" + String::num_int64(uv_view) + ",\"componentType\":5126,\"count\":" + String::num_int64(vertices.size()) + ",\"type\":\"VEC2\"},";
		accessors += "{\"bufferView\":" + String::num_int64(color_view) + ",\"componentType\":5126,\"count\":" + String::num_int64(vertices.size()) + ",\"type\":\"VEC4\"},";
		accessors += "{\"bufferView\":" + String::num_int64(index_view) + ",\"componentType\":5125,\"count\":" + String::num_int64(indices.size()) + ",\"type\":\"SCALAR\"}";

		if (!primitives.is_empty()) {
			primitives += ",";
		}
		int material_index = p_materials.size() > 0 ? MIN(s, p_materials.size() - 1) : 0;
		primitives += "{\"attributes\":{\"POSITION\":" + String::num_int64(pos_accessor) + ",\"NORMAL\":" + String::num_int64(normal_accessor) + ",\"TEXCOORD_0\":" + String::num_int64(uv_accessor) + ",\"COLOR_0\":" + String::num_int64(color_accessor) + "},\"indices\":" + String::num_int64(index_accessor) + ",\"material\":" + String::num_int64(material_index) + "}";
	}

	String images = "";
	String textures_json = "";
	String materials_json = "";
	int texture_count = 0;
	for (int i = 0; i < p_materials.size(); i++) {
		Dictionary mat = p_materials[i];
		Array color = mat["color"];
		String texture_path = mat["texture"];
		int texture_index = -1;
		if (!texture_path.is_empty()) {
			texture_index = texture_count++;
			if (!images.is_empty()) {
				images += ",";
				textures_json += ",";
			}
			images += "{\"uri\":\"../textures/" + texture_path.get_file() + "\"}";
			textures_json += "{\"source\":" + String::num_int64(texture_index) + ",\"sampler\":0}";
		}

		if (!materials_json.is_empty()) {
			materials_json += ",";
		}
		materials_json += "{\"name\":\"" + String(mat["name"]) + "\",\"pbrMetallicRoughness\":{\"baseColorFactor\":[" +
				json_num(color[0]) + "," + json_num(color[1]) + "," + json_num(color[2]) + "," + json_num(color[3]) + "]";
		if (texture_index >= 0) {
			materials_json += ",\"baseColorTexture\":{\"index\":" + String::num_int64(texture_index) + "}";
		}
		materials_json += ",\"metallicFactor\":0,\"roughnessFactor\":1}";
		bool texture_alpha = bool(mat["texture_alpha"]);
		bool material_alpha = bool(mat["material_alpha"]);
		bool is_additive = bool(mat["is_additive"]);
		bool double_sided = bool(mat["double_sided"]);
		
		if (is_additive) {
			materials_json += ",\"alphaMode\":\"BLEND\"";
		} else if (texture_alpha) {
			materials_json += ",\"alphaMode\":\"MASK\",\"alphaCutoff\":0.5";
		} else if (material_alpha) {
			materials_json += ",\"alphaMode\":\"BLEND\"";
		}
		if (double_sided) {
			materials_json += ",\"doubleSided\":true";
		}
		materials_json += "}";
	}
	if (materials_json.is_empty()) {
		materials_json = "{\"name\":\"default\",\"pbrMetallicRoughness\":{\"baseColorFactor\":[1,1,1,1],\"metallicFactor\":0,\"roughnessFactor\":1}}";
	}

	Ref<FileAccess> gltf = FileAccess::open(p_path, FileAccess::WRITE);
	if (gltf.is_null()) {
		return false;
	}

	String json = "{";
	json += "\"asset\":{\"version\":\"2.0\",\"generator\":\"gtaeditor\"},";
	json += "\"scene\":0,\"scenes\":[{\"nodes\":[0]}],";
	json += "\"nodes\":[{\"name\":\"" + sanitize_name(p_model_name) + "\",\"mesh\":0}],";
	json += "\"meshes\":[{\"name\":\"" + sanitize_name(p_model_name) + "\",\"primitives\":[" + primitives + "]}],";
	json += "\"buffers\":[{\"uri\":\"" + bin_path.get_file() + "\",\"byteLength\":" + String::num_int64(byte_offset) + "}],";
	json += "\"bufferViews\":[" + buffer_views + "],";
	json += "\"accessors\":[" + accessors + "],";
	json += "\"materials\":[" + materials_json + "]";
	if (!images.is_empty()) {
		json += ",\"images\":[" + images + "],\"textures\":[" + textures_json + "],\"samplers\":[{\"wrapS\":10497,\"wrapT\":10497,\"magFilter\":9729,\"minFilter\":9987}]";
	}
	json += "}";
	gltf->store_string(json);
	return true;
}

Array MapExporter::export_materials(const String &p_model_name, const ItemDefinition &p_def) {
	Array result;
	Vector<DffMaterial> materials = models.get_materials(p_model_name);
	for (int i = 0; i < materials.size(); i++) {
		const DffMaterial &src = materials[i];
		Dictionary mat;
		mat["name"] = sanitize_name(p_model_name) + "_mat_" + String::num_int64(i);
		mat["color"] = Array::make(src.color.r, src.color.g, src.color.b, src.color.a);
		mat["alpha"] = src.color.a;

		bool has_alpha = false;
		String texture_path;
		if (src.textured && !src.texture_name.is_empty()) {
			texture_path = export_texture(p_def.txd_name, src.texture_name, has_alpha);
		}
		mat["texture"] = texture_path;
		mat["texture_alpha"] = has_alpha;
		mat["material_alpha"] = src.color.a < 1.0f || (p_def.flags & FLAG_DRAW_LAST);
		mat["is_additive"] = (p_def.flags & FLAG_ALPHA_TRANSPARENCY) != 0;
		mat["double_sided"] = (p_def.flags & FLAG_FACE_CULLING_OFF) != 0;
		mat["has_alpha"] = has_alpha || bool(mat["material_alpha"]);
		result.push_back(mat);
	}
	return result;
}

String MapExporter::export_texture(const String &p_txd_name, const String &p_texture_name, bool &r_has_alpha) {
	String key = p_txd_name.to_lower() + "__" + p_texture_name.to_lower();
	String path = export_dir + "textures/" + sanitize_name(key) + ".png";

	Ref<Image> image;
	if (!textures.get_texture_image(p_txd_name, p_texture_name, image, r_has_alpha)) {
		return String();
	}

	if (!exported_texture_keys.has(key) || !FileAccess::file_exists(path)) {
		image->save_png(path);
	}
	exported_texture_keys.insert(key);
	return path;
}

Dictionary MapExporter::build_instance_dict(const ExportItem &p_item) {
	Dictionary instance;
	instance["model"] = p_item.definition.model_name.to_lower();
	instance["name"] = p_item.definition.model_name.to_lower() + "_" + String::num_int64(process_index);
	instance["position"] = Array::make(p_item.placement.position.x, -p_item.placement.position.z, p_item.placement.position.y);
	instance["rotation"] = Array::make(p_item.placement.rotation.x, p_item.placement.rotation.y, p_item.placement.rotation.z, p_item.placement.rotation.w);
	return instance;
}

void MapExporter::finish_export() {
	status_text = "Writing Blender manifest.";
	write_manifest();
	write_blender_script();
	status_text = "Launching Blender to create gta_sa_map.blend.";
	emit_progress_signal();

	exporting = false;
	set_process(false);
	if (launch_blender()) {
		status_text = "Export files written. Blender started in background.";
		emit_signal("export_finished", true, status_text);
	} else {
		status_text = "Export files written, but Blender could not be launched. Set the full blender.exe path and start again.";
		emit_signal("export_finished", false, status_text);
	}
}

void MapExporter::write_manifest() {
	Dictionary manifest;
	manifest["models"] = manifest_models;

	Dictionary regions;
	for (const KeyValue<String, Array> &kv : region_instances) {
		regions[kv.key] = kv.value;
	}
	manifest["regions"] = regions;

	Ref<FileAccess> file = FileAccess::open(export_dir + "manifest.json", FileAccess::WRITE);
	if (file.is_valid()) {
		file->store_string(JSON::stringify(manifest, "\t"));
	}
}

void MapExporter::write_blender_script() {
	String script = R"PY(
import json
import os
import bpy
from mathutils import Matrix, Quaternion, Vector

BASE_DIR = os.path.dirname(os.path.abspath(__file__))
MANIFEST_PATH = os.path.join(BASE_DIR, "manifest.json")
BLEND_PATH = os.path.join(BASE_DIR, "gta_sa_map.blend")

with open(MANIFEST_PATH, "r", encoding="utf-8") as fh:
    manifest = json.load(fh)

bpy.ops.object.select_all(action="SELECT")
bpy.ops.object.delete()

root = bpy.data.collections.new("GTA San Andreas")
bpy.context.scene.collection.children.link(root)

axis = Matrix(((1, 0, 0, 0), (0, 0, -1, 0), (0, 1, 0, 0), (0, 0, 0, 1)))
axis_inv = axis.inverted()
model_cache = {}

def fix_imported_mesh(obj):
    transform = obj.matrix_world.copy()
    flips_winding = transform.to_3x3().determinant() < 0.0
    obj.data.transform(transform)
    obj.matrix_world = Matrix.Identity(4)
    if flips_winding:
        obj.data.flip_normals()
    obj.data.update()

def material_key(name):
    if len(name) > 4 and name[-4] == "." and name[-3:].isdigit():
        return name[:-4]
    return name

def read_double_sided_materials(gltf_path):
    result = {}
    try:
        with open(gltf_path, "r", encoding="utf-8") as fh:
            data = json.load(fh)
        for mat in data.get("materials", []):
            result[mat.get("name", "")] = bool(mat.get("doubleSided", False))
    except Exception:
        pass
    return result

def tune_imported_materials(obj, double_sided_materials):
    for mat in obj.data.materials:
        if not mat:
            continue
        mat.use_nodes = True
        is_double_sided = double_sided_materials.get(material_key(mat.name), False)
        mat.use_backface_culling = not is_double_sided
        if hasattr(mat, "show_transparent_back"):
            mat.show_transparent_back = False
        if mat.blend_method == "CLIP":
            mat.alpha_threshold = 0.5
        elif mat.blend_method != "OPAQUE":
            mat.blend_method = "BLEND"
            mat.use_screen_refraction = False

def make_materials(model_name, material_specs):
    mats = []
    for spec in material_specs:
        mat = bpy.data.materials.new(spec.get("name") or (model_name + "_mat"))
        mat.use_nodes = True
        bsdf = mat.node_tree.nodes.get("Principled BSDF")
        color = spec.get("color", [1, 1, 1, 1])
        if bsdf:
            bsdf.inputs["Base Color"].default_value = color
            bsdf.inputs["Alpha"].default_value = color[3] if len(color) > 3 else 1
        tex_path = spec.get("texture") or ""
        if tex_path and os.path.exists(tex_path) and bsdf:
            image = bpy.data.images.load(tex_path, check_existing=True)
            tex = mat.node_tree.nodes.new("ShaderNodeTexImage")
            tex.image = image
            mat.node_tree.links.new(tex.outputs["Color"], bsdf.inputs["Base Color"])
            if spec.get("has_alpha"):
                mat.node_tree.links.new(tex.outputs["Alpha"], bsdf.inputs["Alpha"])
        if spec.get("has_alpha"):
            mat.blend_method = "BLEND"
            mat.use_screen_refraction = True
        mats.append(mat)
    return mats

def load_model(model_name):
    if model_name in model_cache:
        return model_cache[model_name]
    spec = manifest["models"].get(model_name)
    if not spec:
        return None
    double_sided_materials = read_double_sided_materials(spec["gltf"])
    before = set(bpy.data.objects)
    bpy.ops.import_scene.gltf(filepath=spec["gltf"])
    imported = [obj for obj in bpy.data.objects if obj not in before and obj.type == "MESH"]
    if not imported:
        return None
    source = imported[0]
    source.name = model_name + "_source"
    source.hide_viewport = True
    source.hide_render = True
    fix_imported_mesh(source)
    tune_imported_materials(source, double_sided_materials)
    for obj in imported[1:]:
        bpy.data.objects.remove(obj, do_unlink=True)
    model_cache[model_name] = source
    return source

for region_name, instances in manifest["regions"].items():
    collection = bpy.data.collections.new(region_name)
    root.children.link(collection)
    collection.hide_viewport = True
    collection.hide_render = True
    for inst in instances:
        source = load_model(inst["model"])
        if source is None:
            continue
        obj = bpy.data.objects.new(inst["name"], source.data)
        collection.objects.link(obj)
        pos = inst.get("position", [0, 0, 0])
        rot = inst.get("rotation", [0, 0, 0, 1])
        q = Quaternion((rot[3], rot[0], rot[1], rot[2]))
        rot_matrix = axis @ q.to_matrix().to_4x4() @ axis_inv
        obj.matrix_world = Matrix.Translation(Vector(pos)) @ rot_matrix

for obj in model_cache.values():
    if obj.name in bpy.context.scene.collection.objects:
        bpy.context.scene.collection.objects.unlink(obj)

view_layer = bpy.context.view_layer
def disable_layer_collection(layer_collection):
    if layer_collection.collection.name != "GTA San Andreas":
        layer_collection.exclude = True
    for child in layer_collection.children:
        disable_layer_collection(child)

for child in view_layer.layer_collection.children:
    if child.collection.name == "GTA San Andreas":
        for region_layer in child.children:
            region_layer.exclude = True

bpy.ops.wm.save_as_mainfile(filepath=BLEND_PATH)
)PY";

	Ref<FileAccess> file = FileAccess::open(export_dir + "blender_import.py", FileAccess::WRITE);
	if (file.is_valid()) {
		file->store_string(script);
	}
}

void MapExporter::write_blender_batch(const String &p_executable) {
	String batch = "@echo off\r\n";
	batch += "title GTA SA Blender Export\r\n";
	batch += "echo Running GTA SA Blender export...\r\n";
	batch += "echo Blender: " + p_executable + "\r\n";
	batch += "echo Script: " + export_dir + "blender_import.py\r\n";
	batch += "echo.\r\n";
	batch += "\"" + p_executable.replace("/", "\\") + "\" --background --python \"" + (export_dir + "blender_import.py").replace("/", "\\") + "\"\r\n";
	batch += "echo.\r\n";
	batch += "if errorlevel 1 (\r\n";
	batch += "    echo Blender export failed.\r\n";
	batch += ") else (\r\n";
	batch += "    echo Blender export finished.\r\n";
	batch += ")\r\n";
	batch += "echo.\r\n";
	batch += "pause\r\n";

	Ref<FileAccess> file = FileAccess::open(export_dir + "run_blender_export.bat", FileAccess::WRITE);
	if (file.is_valid()) {
		file->store_string(batch);
	}
}

bool MapExporter::launch_blender() {
	String executable = resolve_blender_executable(blender_path);
	if (executable.is_empty()) {
		UtilityFunctions::printerr("[MapExporter] Blender executable not found: ", blender_path);
		return false;
	}

	write_blender_batch(executable);

	PackedStringArray args;
	args.push_back("/K");
	args.push_back((export_dir + "run_blender_export.bat").replace("/", "\\"));
	int32_t pid = OS::get_singleton()->create_process("cmd.exe", args, true);
	if (pid <= 0) {
		UtilityFunctions::printerr("[MapExporter] Failed to launch Blender command window.");
		return false;
	}
	return true;
}

void MapExporter::emit_progress_signal() {
	emit_signal("export_progress", exported_models, get_remaining_models(), total_models, get_progress(), status_text);
}

String MapExporter::normalize_dir(String p_path) const {
	p_path = p_path.strip_edges().replace("\\", "/");
	if (!p_path.is_empty() && !p_path.ends_with("/")) {
		p_path += "/";
	}
	return p_path;
}

String MapExporter::sanitize_name(const String &p_name) const {
	String out = p_name.to_lower();
	const char *bad = "\\/:*?\"<>| ";
	for (int i = 0; bad[i] != '\0'; i++) {
		out = out.replace(String::chr(bad[i]), "_");
	}
	return out;
}

String MapExporter::region_name_from_path(const String &p_ipl_path) const {
	String clean_path = p_ipl_path.replace("\\", "/");
	return sanitize_name(clean_path.get_file().get_basename());
}

String MapExporter::resolve_blender_executable(const String &p_path) const {
	String requested = p_path.strip_edges().replace("\\", "/");
	if (requested.is_empty()) {
		requested = "blender";
	}

	Vector<String> candidates;
	candidates.push_back(requested);
	if (!requested.to_lower().ends_with(".exe")) {
		candidates.push_back(requested + ".exe");
	}

	if (!requested.contains("/") && !requested.contains("\\")) {
		String path_env = OS::get_singleton()->get_environment("PATH").replace("\\", "/");
		PackedStringArray path_dirs = path_env.split(";", false);
		for (int i = 0; i < path_dirs.size(); i++) {
			String dir = path_dirs[i].strip_edges();
			if (dir.is_empty()) {
				continue;
			}
			if (!dir.ends_with("/")) {
				dir += "/";
			}
			candidates.push_back(dir + requested);
			if (!requested.to_lower().ends_with(".exe")) {
				candidates.push_back(dir + requested + ".exe");
			}
		}
	}

	PackedStringArray common_roots;
	common_roots.push_back("C:/Program Files/Blender Foundation");
	common_roots.push_back("C:/Program Files (x86)/Blender Foundation");
	for (int r = 0; r < common_roots.size(); r++) {
		Ref<DirAccess> dir = DirAccess::open(common_roots[r]);
		if (dir.is_null()) {
			continue;
		}
		PackedStringArray subdirs = dir->get_directories();
		for (int i = 0; i < subdirs.size(); i++) {
			candidates.push_back(common_roots[r] + "/" + subdirs[i] + "/blender.exe");
		}
	}

	for (int i = 0; i < candidates.size(); i++) {
		String candidate = candidates[i].strip_edges().replace("\\", "/");
		if (FileAccess::file_exists(candidate)) {
			return candidate;
		}
	}

	return String();
}

void MapExporter::_bind_methods() {
	ClassDB::bind_method(D_METHOD("start_export", "export_dir", "gta_path", "blender_path", "regions"), &MapExporter::start_export);
	ClassDB::bind_method(D_METHOD("cancel_export"), &MapExporter::cancel_export);
	ClassDB::bind_method(D_METHOD("get_available_regions", "gta_path"), &MapExporter::get_available_regions);
	ClassDB::bind_method(D_METHOD("is_exporting"), &MapExporter::is_exporting);
	ClassDB::bind_method(D_METHOD("get_total_models"), &MapExporter::get_total_models);
	ClassDB::bind_method(D_METHOD("get_exported_models"), &MapExporter::get_exported_models);
	ClassDB::bind_method(D_METHOD("get_remaining_models"), &MapExporter::get_remaining_models);
	ClassDB::bind_method(D_METHOD("get_progress"), &MapExporter::get_progress);
	ClassDB::bind_method(D_METHOD("get_status_text"), &MapExporter::get_status_text);

	ADD_SIGNAL(MethodInfo("export_progress",
			PropertyInfo(Variant::INT, "exported"),
			PropertyInfo(Variant::INT, "remaining"),
			PropertyInfo(Variant::INT, "total"),
			PropertyInfo(Variant::FLOAT, "progress"),
			PropertyInfo(Variant::STRING, "status")));
	ADD_SIGNAL(MethodInfo("export_finished",
			PropertyInfo(Variant::BOOL, "success"),
			PropertyInfo(Variant::STRING, "message")));
}

} // namespace godot
