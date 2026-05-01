#ifndef GTAEDITOR_ASSET_LOADER_H
#define GTAEDITOR_ASSET_LOADER_H

#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/mutex.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/templates/hash_map.hpp>

using namespace godot;

/// Directory entry within a GTA IMG v2 archive.
/// Stores the byte offset and size for seeking directly to an asset.
struct DirEntry {
	String img;              // Path to the IMG file containing this asset
	uint64_t offset = 0;     // Byte offset within the IMG file
	uint64_t size = 0;       // Actual data size in bytes
	uint64_t streaming_size = 0;
	uint64_t archive_size = 0;
};

/// Cached texture entry: stores both the GPU texture and its detected alpha mode.
/// This is critical for trees/fences which have opaque material color but alpha
/// in the texture image — without storing alpha_mode, cache hits lose transparency.
struct CachedTexture {
	Ref<ImageTexture> texture;
	Image::AlphaMode alpha_mode = Image::ALPHA_NONE;
};

/// Singleton that manages GTA game path resolution and IMG archive asset loading.
/// Thread safety:
///   - The `assets` HashMap is read-only after initialization.
///   - `open()` and `open_asset()` create new FileAccess instances (thread-safe).
///   - Mesh and texture caches use `cache_mutex` for thread-safe access.
class AssetLoader {
public:
	static AssetLoader &get();

	void initialize();
	void load_cd_image(const String &path);
	Ref<FileAccess> open(const String &path);
	Ref<FileAccess> open_asset(const String &name);

	const String &get_gta_path() const { return gta_path; }
	bool has_asset(const String &name) const;
	const DirEntry *get_asset_entry(const String &name) const;

	// ── Cache access (use these when NOT already holding cache_mutex) ─────
	Ref<ArrayMesh> get_cached_mesh(const String &model_name);
	void cache_mesh(const String &model_name, Ref<ArrayMesh> mesh);

	CachedTexture get_cached_texture(const String &key);
	void cache_texture(const String &key, const CachedTexture &entry);

	/// Mutex for protecting caches and Godot resource creation.
	Ref<Mutex> cache_mutex;

	// ── Direct cache access (use when ALREADY holding cache_mutex) ────────
	HashMap<String, Ref<ArrayMesh>> mesh_cache;
	HashMap<String, CachedTexture> texture_cache;

private:
	AssetLoader();
	~AssetLoader() = default;

	HashMap<String, String> resolved_paths;
	String gta_path;
	HashMap<String, DirEntry> assets;
	bool initialized = false;
};

#endif // GTAEDITOR_ASSET_LOADER_H
