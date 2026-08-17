#include "gta_img_archive.h"

#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <cstring>

using namespace godot;

std::string GTAImgArchive::_lower(const String &s) {
	CharString utf8 = s.utf8();
	std::string out(utf8.get_data());
	for (char &c : out) {
		if (c >= 'A' && c <= 'Z') {
			c = (char)(c - 'A' + 'a');
		}
	}
	return out;
}

GTAImgArchive::GTAImgArchive() {
}

GTAImgArchive::~GTAImgArchive() {
	close();
}

void GTAImgArchive::_bind_methods() {
	ClassDB::bind_method(D_METHOD("open", "path"), &GTAImgArchive::open);
	ClassDB::bind_method(D_METHOD("close"), &GTAImgArchive::close);
	ClassDB::bind_method(D_METHOD("is_archive_open"), &GTAImgArchive::is_archive_open);
	ClassDB::bind_method(D_METHOD("get_file_count"), &GTAImgArchive::get_file_count);
	ClassDB::bind_method(D_METHOD("list_files"), &GTAImgArchive::list_files);
	ClassDB::bind_method(D_METHOD("has_file", "name"), &GTAImgArchive::has_file);
	ClassDB::bind_method(D_METHOD("get_file_info", "name"), &GTAImgArchive::get_file_info);
	ClassDB::bind_method(D_METHOD("extract_file", "name"), &GTAImgArchive::extract_file);
}

bool GTAImgArchive::open(const String &path) {
	close();

	// std::ifstream works on real filesystem paths only; res:// is a virtual
	// path, so globalize it the same way the rest of the project does.
	String abs_path = path;
	if (abs_path.begins_with("res://") || abs_path.begins_with("user://")) {
		abs_path = ProjectSettings::get_singleton()->globalize_path(abs_path);
	}

	CharString utf8 = abs_path.utf8();
	file_stream.open(utf8.get_data(), std::ios::binary | std::ios::in);
	if (!file_stream.is_open()) {
		UtilityFunctions::push_error("GTAImgArchive: could not open '", path, "'");
		return false;
	}

	// [SOURCED] Streaming.cpp::LoadCdDirectory reads+asserts this magic;
	// Directory.cpp::ReadDirFile reads the same 4 bytes but treats them as
	// "unused" (no assert) — we validate it here regardless, since a mismatch
	// means this isn't a format this class understands.
	char magic[4];
	file_stream.read(magic, 4);
	if (!file_stream || std::memcmp(magic, "VER2", 4) != 0) {
		UtilityFunctions::push_error("GTAImgArchive: '", path, "' is not a VER2 archive (unsupported format or corrupt file)");
		file_stream.close();
		return false;
	}

	uint32_t entry_count = 0;
	file_stream.read(reinterpret_cast<char *>(&entry_count), sizeof(uint32_t));
	if (!file_stream) {
		UtilityFunctions::push_error("GTAImgArchive: truncated header in '", path, "'");
		file_stream.close();
		return false;
	}

	entries.clear();
	entries.reserve(entry_count);
	name_to_index.clear();

	// [SOURCED] CDirectory::DirectoryInfo, 32 bytes/entry: 4 (Pos) + 2 (Size)
	// + 2 (SizeInArchive) + 24 (Name).
	for (uint32_t i = 0; i < entry_count; ++i) {
		uint32_t raw_pos = 0;
		uint16_t size_sectors = 0;
		uint16_t size_in_archive = 0; // unused, present on disk, must still be consumed
		char raw_name[24];

		file_stream.read(reinterpret_cast<char *>(&raw_pos), sizeof(uint32_t));
		file_stream.read(reinterpret_cast<char *>(&size_sectors), sizeof(uint16_t));
		file_stream.read(reinterpret_cast<char *>(&size_in_archive), sizeof(uint16_t));
		file_stream.read(raw_name, sizeof(raw_name));

		if (!file_stream) {
			UtilityFunctions::push_error("GTAImgArchive: truncated entry table in '", path, "' (entry ", (int)i, " of ", (int)entry_count, ")");
			entries.clear();
			name_to_index.clear();
			file_stream.close();
			return false;
		}

		raw_name[sizeof(raw_name) - 1] = '\0'; // guard against a non-terminated 24-byte name

		Entry entry;
		entry.offset_sectors = raw_pos & OFFSET_MASK; // low 24 bits; high 8 (FileID) is meaningless for a single archive
		entry.size_sectors = size_sectors;
		entry.name = String::utf8(raw_name);

		name_to_index[_lower(entry.name)] = entries.size();
		entries.push_back(entry);
	}

	archive_path = path;
	is_open = true;
	return true;
}

void GTAImgArchive::close() {
	if (file_stream.is_open()) {
		file_stream.close();
	}
	entries.clear();
	name_to_index.clear();
	archive_path = String();
	is_open = false;
}

bool GTAImgArchive::is_archive_open() const {
	return is_open;
}

int GTAImgArchive::get_file_count() const {
	return (int)entries.size();
}

PackedStringArray GTAImgArchive::list_files() const {
	PackedStringArray out;
	out.resize((int)entries.size());
	for (size_t i = 0; i < entries.size(); ++i) {
		out[(int)i] = entries[i].name;
	}
	return out;
}

bool GTAImgArchive::has_file(const String &name) const {
	return name_to_index.find(_lower(name)) != name_to_index.end();
}

Dictionary GTAImgArchive::get_file_info(const String &name) const {
	Dictionary out;
	auto it = name_to_index.find(_lower(name));
	if (it == name_to_index.end()) {
		return out;
	}
	const Entry &entry = entries[it->second];
	out["offset_bytes"] = (int64_t)entry.offset_sectors * SECTOR_SIZE;
	out["size_bytes"] = (int64_t)entry.size_sectors * SECTOR_SIZE;
	return out;
}

PackedByteArray GTAImgArchive::extract_file(const String &name) const {
	PackedByteArray out;

	auto it = name_to_index.find(_lower(name));
	if (it == name_to_index.end()) {
		UtilityFunctions::push_error("GTAImgArchive: '", name, "' not found in '", archive_path, "'");
		return out;
	}
	if (!is_open) {
		UtilityFunctions::push_error("GTAImgArchive: archive is not open");
		return out;
	}

	const Entry &entry = entries[it->second];
	const int64_t offset_bytes = (int64_t)entry.offset_sectors * SECTOR_SIZE;
	const int64_t size_bytes = (int64_t)entry.size_sectors * SECTOR_SIZE;

	file_stream.clear(); // reset any eof/fail bits from prior reads
	file_stream.seekg(offset_bytes, std::ios::beg);
	if (!file_stream) {
		UtilityFunctions::push_error("GTAImgArchive: seek failed for '", name, "'");
		return out;
	}

	out.resize((int)size_bytes);
	file_stream.read(reinterpret_cast<char *>(out.ptrw()), size_bytes);
	if (!file_stream) {
		UtilityFunctions::push_error("GTAImgArchive: read failed for '", name, "' (archive may be truncated)");
		out.resize(0);
		return out;
	}

	return out;
}
