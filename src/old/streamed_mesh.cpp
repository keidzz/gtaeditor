#include "streamed_mesh.h"
#include "asset_loader.h"
#include "rw/rw_clump.h"
#include "rw/rw_texture_dict.h"

#include <godot_cpp/classes/base_material3d.hpp>
#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/semaphore.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>
#include <godot_cpp/classes/thread.hpp>
#include <godot_cpp/classes/viewport.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;

// ── Dedicated Background Queue ───────────────────────────────────────────────

class MeshLoaderQueue {
public:
	static MeshLoaderQueue &get() {
		static MeshLoaderQueue instance;
		return instance;
	}

	MeshLoaderQueue() {
		mutex.instantiate();
		semaphore.instantiate();
		thread.instantiate();
		running = true;
		thread->start(callable_mp_static(&MeshLoaderQueue::_thread_func));
	}

	~MeshLoaderQueue() {
		running = false;
		semaphore->post();
		thread->wait_to_finish();
	}

	void add_task(Ref<MeshLoadTaskData> task) {
		mutex->lock();
		queue.push_back(task);
		mutex->unlock();
		semaphore->post();
	}

	void cancel_task(Ref<MeshLoadTaskData> task) {
		mutex->lock();
		queue.erase(task);
		mutex->unlock();
	}

private:
	Ref<Thread> thread;
	Ref<Semaphore> semaphore;
	Ref<Mutex> mutex;
	Vector<Ref<MeshLoadTaskData>> queue;
	bool running = false;

	static void _thread_func() {
		MeshLoaderQueue &q = get();
		while (q.running) {
			q.semaphore->wait();
			if (!q.running)
				break;

			Ref<MeshLoadTaskData> task;
			q.mutex->lock();
			if (!q.queue.is_empty()) {
				// LIFO: process most recently requested first
				task = q.queue[q.queue.size() - 1];
				q.queue.remove_at(q.queue.size() - 1);
			}
			q.mutex->unlock();

			if (task.is_valid()) {
				task->load_mesh();
				task->is_completed = true; // Mark as done safely
			}
		}
	}
};

// ── Background task payload ──────────────────────────────────────────────────

void MeshLoadTaskData::load_mesh() {
	if (!idef)
		return;

	AssetLoader &loader = AssetLoader::get();

	// Skip non-renderable models (flag 0x40 = breakable/hidden)
	if (idef->flags & 0x40) {
		return;
	}

	// Lock the global mutex for the ENTIRE load operation to prevent
	// rendering server crashes (it's not thread-safe for concurrent material/mesh creation).
	loader.cache_mutex->lock();

	// Open the DFF model file
	Ref<FileAccess> dff_file = loader.open_asset(idef->model_name + ".dff");
	if (dff_file.is_null()) {
		loader.cache_mutex->unlock();
		return;
	}

	// Parse the RenderWare clump
	RWClump clump;
	clump.parse(dff_file);

	if (!clump.is_valid) {
		ERR_PRINT("Failed to load Clump for: " + idef->model_name);
		loader.cache_mutex->unlock();
		return;
	}

	// Build mesh from geometry
	for (int gi = 0; gi < (int)clump.geometry_list.geometries.size(); gi++) {
		RWGeometry &geom = clump.geometry_list.geometries.write[gi];
		Ref<ArrayMesh> mesh = geom.build_mesh();
		if (mesh.is_null())
			continue;

		// Apply materials and textures to each surface
		for (int surf = 0; surf < mesh->get_surface_count(); surf++) {
			Ref<StandardMaterial3D> mat = mesh->surface_get_material(surf);
			if (mat.is_null())
				continue;

			// Disable backface culling for GTA models
			mat->set_cull_mode(BaseMaterial3D::CULL_DISABLED);

			bool is_transparent = mat->get_albedo().a < 0.95f;

			// Handle additive blend mode (flag 0x08)
			if (idef->flags & 0x08) {
				mat->set_blend_mode(BaseMaterial3D::BLEND_MODE_ADD);
				mat->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
				mat->set_depth_draw_mode(BaseMaterial3D::DEPTH_DRAW_DISABLED);
			}

			// Apply texture from TXD if this material references one
			if (mat->has_meta("texture_name")) {
				String texture_name = mat->get_meta("texture_name");

				// Open the TXD file for this model
				Ref<FileAccess> txd_file = loader.open_asset(idef->txd_name + ".txd");
				if (txd_file.is_valid()) {
					RWTextureDict txd;
					txd.parse(txd_file);

					// Find the matching raster by name (case-insensitive)
					for (int ri = 0; ri < (int)txd.textures.size(); ri++) {
						RWRaster &raster = txd.textures.write[ri];
						if (texture_name.matchn(raster.name)) {
							Ref<Image> img = raster.load_image();
							if (img.is_valid() && !img->is_empty()) {
								mat->set_texture(BaseMaterial3D::TEXTURE_ALBEDO, ImageTexture::create_from_image(img));

								// Default: opaque rendering
								mat->set_transparency(BaseMaterial3D::TRANSPARENCY_DISABLED);
								mat->set_depth_draw_mode(BaseMaterial3D::DEPTH_DRAW_OPAQUE_ONLY);

								if (is_transparent) {
									// Material color already has alpha < 1
									mat->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
									mat->set_depth_draw_mode(BaseMaterial3D::DEPTH_DRAW_ALWAYS);
								} else {
									// Check image for alpha pixels
									Image::AlphaMode alpha_mode = img->detect_alpha();
									if (alpha_mode != Image::ALPHA_NONE) {
										// Use alpha scissor for better shadow casting
										mat->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA_SCISSOR);
										mat->set_alpha_scissor_threshold(0.5f);
									}
								}
							} else {
								WARN_PRINT("Empty image for: " + texture_name);
							}
							break;
						}
					}
				}
			}

			mesh->surface_set_material(surf, mat);
		}

		// Store the built mesh for the main thread to pick up
		mesh_result = mesh;
	}

	loader.cache_mutex->unlock();
}

// ── StreamedMesh ─────────────────────────────────────────────────────────────

StreamedMesh::StreamedMesh() {}
StreamedMesh::~StreamedMesh() {}

void StreamedMesh::_bind_methods() {
	// Internal use only
}

void StreamedMesh::init(const std::shared_ptr<ItemDef> &p_idef) {
	_idef = p_idef;
}

void StreamedMesh::_exit_tree() {
	if (_state == LOADING && _task_data.is_valid()) {
		// Try to cancel the task if it hasn't started yet.
		// If it has started, the RefCounted payload will survive.
		MeshLoaderQueue::get().cancel_task(_task_data);
	}
	_task_data.unref();
}

void StreamedMesh::_process(double p_delta) {
	if (_idef == nullptr)
		return;

	Viewport *vp = get_viewport();
	if (vp == nullptr)
		return;
	Camera3D *cam = vp->get_camera_3d();
	if (cam == nullptr)
		return;

	float dist = cam->get_global_position().distance_to(get_global_position());
	float range = get_visibility_range_end();

	switch (_state) {
		case IDLE: {
			if (dist < range && get_mesh().is_null()) {
				// Prepare payload
				_task_data.instantiate();
				_task_data->idef = _idef;

				// Submit task to our dedicated queue
				MeshLoaderQueue::get().add_task(_task_data);
				_state = LOADING;
			}
			break;
		}

		case LOADING: {
			// Wait until background task finishes
			if (!_task_data->is_completed) {
				return;
			}

			if (_task_data.is_valid() && _task_data->mesh_result.is_valid()) {
				set_mesh(_task_data->mesh_result);
			}
			_task_data.unref(); // Free memory
			_state = LOADED;
			break;
		}

		case LOADED: {
			// Unload mesh when camera moves away
			if (dist > range && get_mesh().is_valid()) {
				set_mesh(Ref<Mesh>());
				_state = IDLE;
			}
			break;
		}
	}
}
