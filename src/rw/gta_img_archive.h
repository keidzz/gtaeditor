/*
    GTAImgArchive — reads GTA:SA's "VER2" .img archive format.

    SOURCED from gta-reversed (both call sites confirmed genuinely reversed,
    not RenderWare/exe thunks — plain byte-level file I/O):

      - source/game_sa/Streaming.cpp
            CStreaming::LoadCdDirectory (0x5B6170)
            Used for gta3.img, gta_int.img, etc.
      - source/game_sa/Directory.cpp
            CDirectory::ReadDirFile (0x532350)
            Called as `playerImg.ReadDirFile("MODELS\\PLAYER.IMG")` from
            ClothesBuilder.cpp:54 — confirms player.img uses the SAME
            VER2 embedded-header format, not a separate legacy .dir file.
      - source/game_sa/Directory.h
            CDirectory::DirectoryInfo struct (VALIDATE_SIZE'd to 0x20 = 32 bytes)
      - source/game_sa/CdStreamInfo.h
            CdStreamPos bitfield (Offset:24, FileID:8)
      - source/game_sa/StreamingInfo.h
            STREAMING_SECTOR_SIZE = 2048u

    On-disk format (all of this is one file, no separate .dir needed):
        char     magic[4];       // "VER2"
        uint32_t entry_count;
        struct {                // repeated entry_count times, 32 bytes each
            uint32_t pos;        // low 24 bits = offset in 2048-byte sectors,
                                  // high 8 bits = FileID (multi-archive index,
                                  // meaningless for a single opened file — masked off)
            uint16_t size;        // size in 2048-byte sectors
            uint16_t size_in_archive; // unused when reading
            char     name[24];    // null-terminated filename with extension
        } entries[entry_count];

    NOT covered here: the actual .dff/.txd file *contents* — those are opaque
    blobs as far as this class is concerned. Parsing them is a separate,
    NOT-yet-sourced task (see the research notes from the player-controller
    step: RenderWare stream reading in gta-reversed thunks into the original
    exe, it isn't reimplemented).
*/
#ifndef GTA_IMG_ARCHIVE_H
#define GTA_IMG_ARCHIVE_H

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/string.hpp>

#include <fstream>
#include <unordered_map>
#include <vector>

namespace godot {

class GTAImgArchive : public RefCounted {
	GDCLASS(GTAImgArchive, RefCounted)

public:
	struct Entry {
		uint32_t offset_sectors = 0;
		uint16_t size_sectors = 0;
		String name;
	};

private:
	static constexpr uint32_t SECTOR_SIZE = 2048; // [SOURCED] StreamingInfo.h STREAMING_SECTOR_SIZE
	static constexpr uint32_t OFFSET_MASK = 0x00FFFFFF; // low 24 bits, per CdStreamPos

	String archive_path;
	std::vector<Entry> entries;
	std::unordered_map<std::string, size_t> name_to_index; // lowercase name -> entries[] index
	mutable std::ifstream file_stream;
	bool is_open = false;

	static std::string _lower(const String &s);

protected:
	static void _bind_methods();

public:
	GTAImgArchive();
	~GTAImgArchive() override;

	// Opens the .img file and parses its embedded VER2 directory. Returns
	// false (and prints an error) if the file can't be opened or the magic
	// doesn't match "VER2" — every archive gta-reversed touches uses this
	// format, so a mismatch means either a corrupt file or a pre-SA (III/VC,
	// separate .dir) archive, which this class doesn't support.
	bool open(const String &path);
	void close();

	bool is_archive_open() const;
	int get_file_count() const;
	PackedStringArray list_files() const;
	bool has_file(const String &name) const;

	// {"offset_bytes": int, "size_bytes": int} or an empty Dictionary if not found.
	Dictionary get_file_info(const String &name) const;

	// Case-insensitive lookup + seek + read. Returns an empty PackedByteArray
	// (and prints an error) if the entry isn't found or the read fails.
	PackedByteArray extract_file(const String &name) const;
};

} // namespace godot

#endif // GTA_IMG_ARCHIVE_H
