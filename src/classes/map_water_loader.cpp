#include "map_water_loader.h"

namespace godot {

MeshInstance3D *MapWaterLoader::build_water_mesh(const Vector<WaterPlane> &water_planes) {
	if (water_planes.size() == 0) {
		return nullptr;
	}

	PackedVector3Array vertices;
	PackedInt32Array indices;
	int idx = 0;

	for (int i = 0; i < water_planes.size(); i++) {
		const WaterPlane &wp = water_planes[i];
		if (wp.is_triangle) {
			vertices.push_back(wp.p1);
			vertices.push_back(wp.p2);
			vertices.push_back(wp.p3);
			indices.push_back(idx++);
			indices.push_back(idx++);
			indices.push_back(idx++);
		} else {
			vertices.push_back(wp.p1);
			vertices.push_back(wp.p2);
			vertices.push_back(wp.p3);
			vertices.push_back(wp.p4);
			indices.push_back(idx);
			indices.push_back(idx + 1);
			indices.push_back(idx + 2);
			indices.push_back(idx + 2);
			indices.push_back(idx + 3);
			indices.push_back(idx);
			idx += 4;
		}
	}

	Array arrays;
	arrays.resize(Mesh::ARRAY_MAX);
	arrays[Mesh::ARRAY_VERTEX] = vertices;
	arrays[Mesh::ARRAY_INDEX] = indices;

	Ref<ArrayMesh> water_mesh;
	water_mesh.instantiate();
	water_mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays);

	Ref<StandardMaterial3D> water_mat;
	water_mat.instantiate();
	water_mat->set_albedo(Color(0.2f, 0.4f, 0.8f, 0.6f));
	water_mat->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
	water_mesh->surface_set_material(0, water_mat);

	MeshInstance3D *water_instance = memnew(MeshInstance3D);
	water_instance->set_mesh(water_mesh);
	water_instance->set_name("WaterPlanes");

	return water_instance;
}

} // namespace godot
