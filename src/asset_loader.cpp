#include "asset_loader.h"

#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;

// ── Singleton ────────────────────────────────────────────────────────────────

AssetLoader::AssetLoader() {
	cache_mutex.instantiate();
}

AssetLoader &AssetLoader::get() {
	static AssetLoader instance;
	return instance;
}

// ── Initialization ───────────────────────────────────────────────────────────

void AssetLoader::initialize() {
	if (initialized) return;

	// Resolve GTA path based on whether we're in the editor or an export
	if (OS::get_singleton()->has_feature("editor")) {
		gta_path = ProjectSettings::get_singleton()->globalize_path("res://gta/");
	} else {
		gta_path = OS::get_singleton()->get_executable_path().get_base_dir() + "/";
	}

	UtilityFunctions::print("GTA path: " + gta_path);

	// Load the main IMG archive
	load_cd_image("models/gta3.img");

	initialized = true;
}

// ── IMG v2 archive loader ────────────────────────────────────────────────────

void AssetLoader::load_cd_image(const String &path) {
	// Pre-resolve the image path so we don't have to scan directories repeatedly
	String resolved_img_path = path;
	Ref<FileAccess> file = open(path);
	if (file.is_valid()) {
		resolved_img_path = file->get_path_absolute();
	}

	ERR_FAIL_COND_MSG(file.is_null(), "Failed to open GTA IMG file: " + path);

	// Verify this is a GTA SA IMG v2 file
	String version = file->get_buffer(4).get_string_from_ascii();
	ERR_FAIL_COND_MSG(version != "VER2", "Not a valid GTA SA IMG file (version 2): " + path);

	uint32_t entry_count = file->get_32();

	// Pre-size the hash map for fewer rehashes
	assets.reserve(entry_count);

	// Read all directory entries
	for (uint32_t i = 0; i < entry_count; i++) {
		DirEntry entry;
		entry.img = resolved_img_path;

		// Offsets and sizes are stored in 2048-byte sectors
		entry.offset         = static_cast<uint64_t>(file->get_32()) * 2048;
		entry.streaming_size = static_cast<uint64_t>(file->get_16()) * 2048;
		entry.archive_size   = static_cast<uint64_t>(file->get_16()) * 2048;

		// Use streaming_size if available, otherwise fall back to archive_size
		entry.size = entry.streaming_size > 0 ? entry.streaming_size : entry.archive_size;

		// Asset names are 24 bytes, null-padded, stored lowercase for lookup
		String name = file->get_buffer(24).get_string_from_ascii().to_lower();
		assets.insert(name, entry);
	}

	file->close();
	UtilityFunctions::print("Loaded " + String::num_int64(entry_count) + " assets from " + path);
}

// ── File access ──────────────────────────────────────────────────────────────

Ref<FileAccess> AssetLoader::open(const String &path) {
	if (resolved_paths.has(path)) {
		return FileAccess::open(resolved_paths[path], FileAccess::READ);
	}

	// Navigate through directories using case-insensitive matching.
	// This handles the fact that GTA file references may not match actual
	// filesystem casing (common on Linux).
	Ref<DirAccess> dir = DirAccess::open(gta_path);
	ERR_FAIL_COND_V_MSG(dir.is_null(), Ref<FileAccess>(), "Cannot open GTA directory: " + gta_path);

	// Normalize path separators and split into components
	String normalized = path.replace("\\", "/");
	PackedStringArray parts = normalized.split("/", false);

	for (int i = 0; i < parts.size(); i++) {
		const String &part = parts[i];
		bool is_last = (i == parts.size() - 1);

		if (is_last) {
			// Last component = file name. Find a case-insensitive match.
			PackedStringArray files = dir->get_files();
			for (int f = 0; f < files.size(); f++) {
				if (files[f].matchn(part)) {
					String full_path = dir->get_current_dir() + "/" + files[f];
					resolved_paths[path] = full_path;
					return FileAccess::open(full_path, FileAccess::READ);
				}
			}
			// File not found
			return Ref<FileAccess>();
		} else {
			// Intermediate component = directory name. Find and enter it.
			PackedStringArray dirs = dir->get_directories();
			bool found = false;
			for (int d = 0; d < dirs.size(); d++) {
				if (dirs[d].matchn(part)) {
					dir->change_dir(dirs[d]);
					found = true;
					break;
				}
			}
			if (!found) {
				return Ref<FileAccess>();
			}
		}
	}

	return Ref<FileAccess>();
}

Ref<FileAccess> AssetLoader::open_asset(const String &name) {
	String lower = name.to_lower();

	if (assets.has(lower)) {
		const DirEntry &entry = assets[lower];
		// Use FileAccess::open directly since entry.img is an absolute resolved path
		Ref<FileAccess> file = FileAccess::open(entry.img, FileAccess::READ);
		if (file.is_valid()) {
			file->seek(entry.offset);
			return file;
		}
	}

	// Fall back to loading directly from the models directory
	return open("models/" + name);
}

bool AssetLoader::has_asset(const String &name) const {
	return assets.has(name.to_lower());
}

const DirEntry *AssetLoader::get_asset_entry(const String &name) const {
	String lower = name.to_lower();
	if (assets.has(lower)) {
		return &assets[lower];
	}
	return nullptr;
}
