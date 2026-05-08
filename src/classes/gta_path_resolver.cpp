#include "gta_path_resolver.h"

#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

// =============================================================================
// GtaPathResolver implementation
// =============================================================================

void GtaPathResolver::set_root(const String &p_root) {
	root = p_root;
	// Ensure trailing slash.
	if (!root.ends_with("/")) {
		root += "/";
	}
	clear_cache();
}

String GtaPathResolver::get_root() const {
	return root;
}

void GtaPathResolver::clear_cache() {
	cache.clear();
}

String GtaPathResolver::resolve(const String &p_relative_path) const {
	// Normalize: replace backslashes with forward slashes and lowercase the key.
	String normalized = p_relative_path.replace("\\", "/");
	String cache_key = normalized.to_lower();

	// Check cache first.
	if (cache.has(cache_key)) {
		return cache[cache_key];
	}

	String result = resolve_uncached(normalized);
	cache[cache_key] = result;
	return result;
}

String GtaPathResolver::resolve_uncached(const String &p_relative_path) const {
	// Split the path into components and resolve each one case-insensitively.
	PackedStringArray parts = p_relative_path.split("/", false);
	String current_dir = root;

	for (int i = 0; i < parts.size(); i++) {
		String found = find_case_insensitive(current_dir, parts[i]);
		if (found.is_empty()) {
			UtilityFunctions::printerr("[GtaPathResolver] Could not find '",
					parts[i], "' in '", current_dir, "'");
			return String();
		}
		current_dir = current_dir + found;
		// Add trailing slash for directories (all but last component).
		if (i < parts.size() - 1) {
			current_dir += "/";
		}
	}

	return current_dir;
}

String GtaPathResolver::find_case_insensitive(const String &p_parent_dir, const String &p_name) const {
	Ref<DirAccess> dir = DirAccess::open(p_parent_dir);
	if (dir.is_null()) {
		return String();
	}

	String lower_name = p_name.to_lower();

	// Check files.
	PackedStringArray files = dir->get_files();
	for (int i = 0; i < files.size(); i++) {
		if (files[i].to_lower() == lower_name) {
			return files[i];
		}
	}

	// Check directories.
	PackedStringArray dirs = dir->get_directories();
	for (int i = 0; i < dirs.size(); i++) {
		if (dirs[i].to_lower() == lower_name) {
			return dirs[i];
		}
	}

	return String();
}
