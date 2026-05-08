#include "texture_collection.h"

#include <godot_cpp/variant/utility_functions.hpp>

// =============================================================================
// TextureCollection implementation
// =============================================================================

void TextureCollection::register_txd(const String &p_txd_name, const ImgArchive *p_archive) {
	String key = p_txd_name.to_lower();
	// Strip .txd extension if present.
	if (key.ends_with(".txd")) {
		key = key.substr(0, key.length() - 4);
	}

	TxdEntry entry;
	entry.txd_name = key;
	entry.archive = p_archive;
	entry.loaded = false;
	txd_entries[key] = entry;
}

void TextureCollection::add_parent(const String &p_child, const String &p_parent) {
	parent_links[p_child.to_lower()] = p_parent.to_lower();
}

bool TextureCollection::get_texture(const String &p_txd_name, const String &p_texture_name, Ref<ImageTexture> &r_tex, bool &r_has_alpha) {
	String txd_key = p_txd_name.to_lower();
	String tex_key = p_texture_name.to_lower();

	// Try the primary TXD.
	if (txd_entries.has(txd_key)) {
		TxdEntry &entry = txd_entries[txd_key];
		ensure_loaded(entry);

		if (entry.textures.has(tex_key)) {
			r_tex = entry.textures[tex_key].texture;
			r_has_alpha = entry.textures[tex_key].has_alpha;
			return true;
		}
	}

	// Try the parent TXD (TXDP fallback).
	if (parent_links.has(txd_key)) {
		String parent_key = parent_links[txd_key];
		if (txd_entries.has(parent_key)) {
			TxdEntry &parent_entry = txd_entries[parent_key];
			ensure_loaded(parent_entry);

			if (parent_entry.textures.has(tex_key)) {
				r_tex = parent_entry.textures[tex_key].texture;
				r_has_alpha = parent_entry.textures[tex_key].has_alpha;
				return true;
			}
		}
	}

	return false;
}

int TextureCollection::get_txd_count() const {
	return txd_entries.size();
}

void TextureCollection::clear() {
	txd_entries.clear();
	parent_links.clear();
}

void TextureCollection::ensure_loaded(TxdEntry &entry) {
	if (entry.loaded) {
		return;
	}
	entry.loaded = true;

	if (entry.archive == nullptr) {
		return;
	}

	// Read TXD data from IMG archive and parse it.
	String txd_filename = entry.txd_name + ".txd";
	if (!entry.archive->has_entry(txd_filename)) {
		return;
	}

	PackedByteArray data = entry.archive->read_entry(txd_filename);
	if (data.is_empty()) {
		return;
	}

	entry.textures = TxdParser::parse(data);
}
