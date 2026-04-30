#ifndef GTAEDITOR_STREAMED_MESH_H
#define GTAEDITOR_STREAMED_MESH_H

#include "classes/item_def.h"

#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/classes/worker_thread_pool.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <memory>

using namespace godot;

/// Data payload for the background mesh loading task.
/// Inherits RefCounted so it survives even if the StreamedMesh is freed
/// before the background task completes, preventing crashes and freezes.
class MeshLoadTaskData : public RefCounted {
	GDCLASS(MeshLoadTaskData, RefCounted);

public:
	std::shared_ptr<ItemDef> idef;
	Ref<Mesh> mesh_result;
	bool is_completed = false; // Flag to indicate completion

	void load_mesh();

protected:
	static void _bind_methods() {}
};

/// A MeshInstance3D that lazily loads its mesh data on a background thread.
class StreamedMesh : public MeshInstance3D {
	GDCLASS(StreamedMesh, MeshInstance3D);

public:
	StreamedMesh();
	~StreamedMesh();

	/// Initialize with an item definition. Must be called before adding to tree.
	void init(const std::shared_ptr<ItemDef> &p_idef);

	void _process(double p_delta) override;
	void _exit_tree() override;

protected:
	static void _bind_methods();

private:
	enum LoadState {
		IDLE, // Not loading, no mesh
		LOADING, // Task submitted, waiting for completion
		LOADED, // Mesh data ready, assigned
	};

	std::shared_ptr<ItemDef> _idef;
	LoadState _state = IDLE;

	// Shared payload with the background thread
	Ref<MeshLoadTaskData> _task_data;
};

#endif // GTAEDITOR_STREAMED_MESH_H
