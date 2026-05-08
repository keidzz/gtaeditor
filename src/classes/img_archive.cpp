#include "img_archive.h"

#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

// =============================================================================
// ImgArchive implementation
// =============================================================================

bool ImgArchive::load(const String &p_path) {
	archive_path = p_path;
	entries.clear();

	file_handle = FileAccess::open(p_path, FileAccess::READ);
	if (file_handle.is_null()) {
		UtilityFunctions::printerr("[ImgArchive] Failed to open: ", p_path);
		return false;
	}

	// Read and verify "VER2" magic header.
	PackedByteArray magic_bytes = file_handle->get_buffer(4);
	String magic = String::utf8(reinterpret_cast<const char *>(magic_bytes.ptr()), 4);
	if (magic != "VER2") {
		UtilityFunctions::printerr("[ImgArchive] Not a valid GTA SA IMG v2 file: ", p_path);
		return false;
	}

	// Read entry count.
	int32_t entry_count = file_handle->get_32();

	// Read all directory entries (32 bytes each).
	for (int32_t i = 0; i < entry_count; i++) {
		Entry entry;

		// Offset in 2048-byte sectors.
		int32_t offset_sectors = file_handle->get_32();
		entry.offset = static_cast<int64_t>(offset_sectors) * 2048;

		// Streaming size in sectors (int16), then archive size in sectors (int16).
		int16_t streaming_size = file_handle->get_16();
		int16_t archive_size = file_handle->get_16();
		int64_t size = (streaming_size > 0) ? streaming_size : archive_size;
		entry.size = size * 2048;

		// Name: 24 bytes, null-terminated.
		PackedByteArray name_bytes = file_handle->get_buffer(24);
		const char *name_ptr = reinterpret_cast<const char *>(name_bytes.ptr());
		int name_len = 0;
		while (name_len < 24 && name_ptr[name_len] != '\0') {
			name_len++;
		}
		entry.name = String::utf8(name_ptr, name_len).to_lower();

		// Add to map (skip duplicates).
		if (!entries.has(entry.name)) {
			entries[entry.name] = entry;
		}
	}

	UtilityFunctions::print("[ImgArchive] Loaded ", entries.size(),
			" entries from ", p_path);
	return true;
}

bool ImgArchive::has_entry(const String &p_name) const {
	return entries.has(p_name.to_lower());
}

PackedByteArray ImgArchive::read_entry(const String &p_name) const {
	String key = p_name.to_lower();
	if (!entries.has(key)) {
		UtilityFunctions::printerr("[ImgArchive] Entry not found: ", p_name);
		return PackedByteArray();
	}

	const Entry &entry = entries[key];

	if (file_handle.is_null()) {
		UtilityFunctions::printerr("[ImgArchive] Archive file handle is null: ", archive_path);
		return PackedByteArray();
	}

	file_handle->seek(entry.offset);
	return file_handle->get_buffer(entry.size);
}

Vector<String> ImgArchive::get_entries_with_extension(const String &p_ext) const {
	Vector<String> result;
	String ext_lower = p_ext.to_lower();

	for (const KeyValue<String, Entry> &kv : entries) {
		if (kv.key.ends_with(ext_lower)) {
			result.push_back(kv.key);
		}
	}

	return result;
}

int ImgArchive::get_entry_count() const {
	return entries.size();
}
