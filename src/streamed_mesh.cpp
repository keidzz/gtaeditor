#include "streamed_mesh.h"
#include "asset_loader.h"
#include "rw/rw_clump.h"
#include "rw/rw_texture_dict.h"

#include <godot_cpp/classes/base_material3d.hpp>
#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>
#include <godot_cpp/classes/viewport.hpp>
#include <godot_cpp/classes/thread.hpp>
#include <godot_cpp/classes/semaphore.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;

// ── Dedicated Background Queue with Multiple Workers ─────────────────────────

static constexpr int NUM_WORKERS = 3;

class MeshLoaderQueue {
public:
	static MeshLoaderQueue &get() {
		static MeshLoaderQueue instance;
		return instance;
	}

	MeshLoaderQueue() {
		mutex.instantiate();
		semaphore.instantiate();
		running = true;
		for (int i = 0; i < NUM_WORKERS; i++) {
			threads[i].instantiate();
			threads[i]->start(callable_mp_static(&MeshLoaderQueue::_thread_func));
		}
	}

	~MeshLoaderQueue() {
		running = false;
		for (int i = 0; i < NUM_WORKERS; i++) {
			semaphore->post();
		}
		for (int i = 0; i < NUM_WORKERS; i++) {
			threads[i]->wait_to_finish();
		}
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
	Ref<Thread> threads[NUM_WORKERS];
	Ref<Semaphore> semaphore;
	Ref<Mutex> mutex;
	Vector<Ref<MeshLoadTaskData>> queue;
	bool running = false;

	static void _thread_func() {
		MeshLoaderQueue &q = get();
		while (q.running) {
			q.semaphore->wait();
			if (!q.running) break;

			Ref<MeshLoadTaskData> task;
			q.mutex->lock();
			if (!q.queue.is_empty()) {
				// LIFO: most recently requested first (nearest to camera)
				task = q.queue[q.queue.size() - 1];
				q.queue.remove_at(q.queue.size() - 1);
			}
			q.mutex->unlock();

			if (task.is_valid()) {
				task->load_mesh();
				task->is_completed = true;
			}
		}
	}
};

// ── Helper: apply transparency settings to a material ────────────────────────

static void _apply_transparency(Ref<StandardMaterial3D> mat, bool is_transparent, Image::AlphaMode alpha_mode) {
	mat->set_transparency(BaseMaterial3D::TRANSPARENCY_DISABLED);
	mat->set_depth_draw_mode(BaseMaterial3D::DEPTH_DRAW_OPAQUE_ONLY);

	if (is_transparent) {
		// Material color has alpha < 1
		mat->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
		mat->set_depth_draw_mode(BaseMaterial3D::DEPTH_DRAW_ALWAYS);
	} else if (alpha_mode != Image::ALPHA_NONE) {
		// Texture has alpha (trees, fences, etc.)
		mat->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA_SCISSOR);
		mat->set_alpha_scissor_threshold(0.5f);
	}
}

// ── Background task payload ──────────────────────────────────────────────────

void MeshLoadTaskData::load_mesh() {
	if (!idef) return;

	AssetLoader &loader = AssetLoader::get();

	if (idef->flags & 0x40) {
		return;
	}

	// ── Check mesh cache first (no lock needed for the wrapper) ───────────
	Ref<ArrayMesh> cached_mesh = loader.get_cached_mesh(idef->model_name);
	if (cached_mesh.is_valid()) {
		mesh_result = cached_mesh;
		cache_hit = true;
		return;
	}

	// ── Phase 1: File I/O and parsing (NO mutex) ─────────────────────────
	Ref<FileAccess> dff_file = loader.open_asset(idef->model_name + ".dff");
	if (dff_file.is_null()) {
		return;
	}

	RWClump clump;
	clump.parse(dff_file);

	if (!clump.is_valid) {
		return;
	}

	// Pre-load TXD outside the mutex
	RWTextureDict txd;
	bool txd_loaded = false;
	{
		Ref<FileAccess> txd_file = loader.open_asset(idef->txd_name + ".txd");
		if (txd_file.is_valid()) {
			txd.parse(txd_file);
			txd_loaded = true;
		}
	}

	// ── Phase 2: Godot resource creation (mutex required) ────────────────
	loader.cache_mutex->lock();

	// Double-check cache (another worker may have built it while we parsed)
	if (loader.mesh_cache.has(idef->model_name)) {
		cached_mesh = loader.mesh_cache[idef->model_name];
		loader.cache_mutex->unlock();
		mesh_result = cached_mesh;
		cache_hit = true;
		return;
	}

	for (int gi = 0; gi < (int)clump.geometry_list.geometries.size(); gi++) {
		RWGeometry &geom = clump.geometry_list.geometries.write[gi];
		Ref<ArrayMesh> mesh = geom.build_mesh();
		if (mesh.is_null()) continue;

		for (int surf = 0; surf < mesh->get_surface_count(); surf++) {
			Ref<StandardMaterial3D> mat = mesh->surface_get_material(surf);
			if (mat.is_null()) continue;

			mat->set_cull_mode(BaseMaterial3D::CULL_DISABLED);

			bool is_transparent = mat->get_albedo().a < 0.95f;

			if (idef->flags & 0x08) {
				mat->set_blend_mode(BaseMaterial3D::BLEND_MODE_ADD);
				mat->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
				mat->set_depth_draw_mode(BaseMaterial3D::DEPTH_DRAW_DISABLED);
			}

			if (mat->has_meta("texture_name") && txd_loaded) {
				String texture_name = mat->get_meta("texture_name");
				String cache_key = idef->txd_name + "/" + texture_name;

				// Check texture cache (direct access, already hold mutex)
				if (loader.texture_cache.has(cache_key)) {
					const CachedTexture &ct = loader.texture_cache[cache_key];
					mat->set_texture(BaseMaterial3D::TEXTURE_ALBEDO, ct.texture);
					// Use the STORED alpha_mode — this is the fix for trees
					_apply_transparency(mat, is_transparent, ct.alpha_mode);
				} else {
					// Decode texture and cache it
					for (int ri = 0; ri < (int)txd.textures.size(); ri++) {
						RWRaster &raster = txd.textures.write[ri];
						if (texture_name.matchn(raster.name)) {
							Ref<Image> img = raster.load_image();
							if (img.is_valid() && !img->is_empty()) {
								Ref<ImageTexture> tex = ImageTexture::create_from_image(img);
								mat->set_texture(BaseMaterial3D::TEXTURE_ALBEDO, tex);

								Image::AlphaMode alpha_mode = img->detect_alpha();

								// Cache with alpha mode
								CachedTexture ct;
								ct.texture = tex;
								ct.alpha_mode = alpha_mode;
								loader.texture_cache.insert(cache_key, ct);

								_apply_transparency(mat, is_transparent, alpha_mode);
							}
							break;
						}
					}
				}
			}

			mesh->surface_set_material(surf, mat);
		}

		// Cache the built mesh
		if (!loader.mesh_cache.has(idef->model_name)) {
			loader.mesh_cache.insert(idef->model_name, mesh);
		}

		mesh_result = mesh;
	}

	loader.cache_mutex->unlock();
}

// ── StreamedMesh ─────────────────────────────────────────────────────────────

StreamedMesh::StreamedMesh() {}
StreamedMesh::~StreamedMesh() {}

void StreamedMesh::_bind_methods() {}

void StreamedMesh::init(const std::shared_ptr<ItemDef> &p_idef) {
	_idef = p_idef;
}

void StreamedMesh::_exit_tree() {
	if (_state == LOADING && _task_data.is_valid()) {
		MeshLoaderQueue::get().cancel_task(_task_data);
	}
	_task_data.unref();
}

void StreamedMesh::_process(double p_delta) {
	if (_idef == nullptr) return;

	Viewport *vp = get_viewport();
	if (vp == nullptr) return;
	Camera3D *cam = vp->get_camera_3d();
	if (cam == nullptr) return;

	float dist = cam->get_global_position().distance_to(get_global_position());
	float range = get_visibility_range_end();

	switch (_state) {
		case IDLE: {
			if (dist < range && get_mesh().is_null()) {
				// Try cache first — instant, no background task
				Ref<ArrayMesh> cached = AssetLoader::get().get_cached_mesh(_idef->model_name);
				if (cached.is_valid()) {
					set_mesh(cached);
					_state = LOADED;
					break;
				}

				_task_data.instantiate();
				_task_data->idef = _idef;
				MeshLoaderQueue::get().add_task(_task_data);
				_state = LOADING;
			}
			break;
		}

		case LOADING: {
			if (!_task_data->is_completed) {
				return;
			}
			
			if (_task_data.is_valid() && _task_data->mesh_result.is_valid()) {
				set_mesh(_task_data->mesh_result);
			}
			_task_data.unref();
			_state = LOADED;
			break;
		}

		case LOADED: {
			if (dist > range && get_mesh().is_valid()) {
				set_mesh(Ref<Mesh>());
				_state = IDLE;
			}
			break;
		}
	}
}
