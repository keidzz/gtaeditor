#ifndef GTAEDITOR_ASSET_LOADER_H
#define GTAEDITOR_ASSET_LOADER_H

#include <godot_cpp/classes/file_access.hpp>
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

/// Singleton that manages GTA game path resolution and IMG archive asset loading.
/// Combines the functionality of the GDScript GameManager and AssetLoader.
///
/// Thread safety:
///   - The `assets` HashMap is read-only after initialization (populated in _ready).
///   - `open()` and `open_asset()` each create new FileAccess instances, so they
///     are safe to call from any thread without a mutex.
///   - If you need to cache shared mutable state (e.g., texture cache), use
///     the provided mutex.
class AssetLoader {
public:
	/// Get the global singleton instance.
	static AssetLoader &get();

	/// Initialize the game path. Must be called before any file operations.
	/// Resolves path differently in editor vs. exported builds.
	void initialize();

	/// Load all directory entries from a GTA SA IMG v2 archive.
	void load_cd_image(const String &path);

	/// Open a file from the GTA directory using case-insensitive path traversal.
	/// Returns a new FileAccess instance (safe for use from any thread).
	Ref<FileAccess> open(const String &path);

	/// Open an asset from the IMG archive by name, or fall back to models/ directory.
	/// Returns a FileAccess seeked to the asset's offset within the IMG file.
	Ref<FileAccess> open_asset(const String &name);

	/// Get the resolved GTA installation path.
	const String &get_gta_path() const { return gta_path; }

	/// Check if an asset exists in the loaded IMG archives.
	bool has_asset(const String &name) const;

	/// Get a DirEntry for an asset. Returns nullptr if not found.
	const DirEntry *get_asset_entry(const String &name) const;

	/// Mutex for protecting shared mutable state (e.g., texture cache).
	/// Not needed for open()/open_asset() which create independent file handles.
	Ref<Mutex> cache_mutex;

private:
	AssetLoader();

	HashMap<String, String> resolved_paths;
	~AssetLoader() = default;

	String gta_path;
	HashMap<String, DirEntry> assets; // asset_name (lowercase) → DirEntry
	bool initialized = false;
};

#endif // GTAEDITOR_ASSET_LOADER_H
