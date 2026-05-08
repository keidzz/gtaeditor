#ifndef GTA_PATH_RESOLVER_H
#define GTA_PATH_RESOLVER_H

#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/templates/hash_map.hpp>

using namespace godot;

// =============================================================================
// GtaPathResolver — Resolves GTA's Windows-style paths to actual filesystem
// paths with case-insensitive matching. Critical on Linux where the filesystem
// is case-sensitive but GTA uses mixed-case paths like "DATA\MAPS\LA\LAn.IDE".
// =============================================================================

class GtaPathResolver {
public:
	// Set the root directory for all GTA file lookups.
	void set_root(const String &p_root);
	String get_root() const;

	// Resolve a GTA-style path (e.g. "DATA\\MAPS\\LA\\LAn.IDE") to an
	// absolute filesystem path with correct casing. Returns empty string
	// if the file cannot be found.
	String resolve(const String &p_relative_path) const;

	// Clear the resolution cache (if paths change).
	void clear_cache();

private:
	String root;
	mutable HashMap<String, String> cache;

	// Walk each component of the path, doing case-insensitive directory listing.
	String resolve_uncached(const String &p_relative_path) const;

	// Find a file/directory in parent_dir matching name case-insensitively.
	String find_case_insensitive(const String &p_parent_dir, const String &p_name) const;
};

#endif // GTA_PATH_RESOLVER_H
