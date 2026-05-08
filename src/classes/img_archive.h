#ifndef IMG_ARCHIVE_H
#define IMG_ARCHIVE_H

#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/templates/hash_map.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/string.hpp>

using namespace godot;

// =============================================================================
// ImgArchive — Reads GTA SA IMG v2 archives (e.g. models/gta3.img).
//
// IMG v2 format:
//   Header: "VER2" (4 bytes) + entry_count (int32)
//   Each entry (32 bytes): offset_sectors(int32) + size_sectors(int16)
//     + archive_size(int16) + name(char[24])
//   Offsets and sizes are in 2048-byte sectors.
//
// Reference: https://gtamods.com/wiki/IMG_archive
// =============================================================================

class ImgArchive {
public:
	struct Entry {
		int64_t offset = 0;   // Byte offset into the IMG file
		int64_t size = 0;     // Size in bytes
		String name;          // Filename (lowercase)
	};

	// Load the IMG archive from an absolute filesystem path.
	// Returns true on success.
	bool load(const String &p_path);

	// Check if an entry exists (case-insensitive).
	bool has_entry(const String &p_name) const;

	// Read the raw bytes of a named entry from the archive.
	PackedByteArray read_entry(const String &p_name) const;

	// Get all entry names matching a file extension (e.g. ".dff", ".txd", ".ipl").
	Vector<String> get_entries_with_extension(const String &p_ext) const;

	// Total number of entries.
	int get_entry_count() const;

private:
	String archive_path;
	HashMap<String, Entry> entries;
	Ref<FileAccess> file_handle;
};

#endif // IMG_ARCHIVE_H
