#ifndef TEXTURE_COLLECTION_H
#define TEXTURE_COLLECTION_H

#include "../rw/txd_parser.h"
#include "img_archive.h"

#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/templates/hash_map.hpp>
#include <godot_cpp/variant/string.hpp>

using namespace godot;

// =============================================================================
// TextureCollection — Manages all TXD files and provides texture lookups.
// TXD data is loaded lazily (parsed on first access).
// Supports texture parent fallback (TXDP from IDE files).
// =============================================================================

class TextureCollection {
public:
	// Register a TXD entry from the IMG archive (stores raw bytes reference).
	void register_txd(const String &p_txd_name, const ImgArchive *p_archive);

	// Register a texture parent link (child TXD falls back to parent TXD).
	void add_parent(const String &p_child, const String &p_parent);

	// Get a texture by TXD name and texture name.
	// Tries the TXD first, then its parent if not found.
	// r_has_alpha_content is true if the texture has actual non-opaque pixels.
	bool get_texture(const String &p_txd_name, const String &p_texture_name, Ref<ImageTexture> &r_tex, bool &r_has_alpha_content);
	bool get_texture_image(const String &p_txd_name, const String &p_texture_name, Ref<Image> &r_image, bool &r_has_alpha_content);

	// Stats.
	int get_txd_count() const;

	// Free all textures.
	void clear();

private:
	// Lazy-loaded TXD data.
	struct TxdEntry {
		String txd_name;
		const ImgArchive *archive = nullptr;
		bool loaded = false;
		HashMap<String, TxdTexture> textures;
	};

	HashMap<String, TxdEntry> txd_entries;
	HashMap<String, String> parent_links; // child_txd → parent_txd

	// Ensure a TXD is loaded (parse if needed).
	void ensure_loaded(TxdEntry &entry);
};

#endif // TEXTURE_COLLECTION_H
