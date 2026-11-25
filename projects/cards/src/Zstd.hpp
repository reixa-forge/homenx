// Zstd - a zstd decompressor (RFC 8878), for games' .zs files.
//
// Frames (one or more, skippable ones skipped), raw / RLE / compressed
// blocks, Huffman literals (one or four streams), FSE sequences, and
// dictionaries: raw content, or zstd's own format (its entropy tables, repeat
// offsets and content). Decoding only; no checksum check.
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace zstd {

struct Dictionary {
    struct Tables;
    std::uint32_t id = 0;                // 0: raw content
    std::vector<std::uint8_t> content;
    std::shared_ptr<Tables> tables;      // (a formatted one's entropy tables and repeat offsets)
    ~Dictionary();
};

// A dictionary from its bytes (formatted: magic 0xEC30A437; otherwise raw content).
bool loadDictionary(const std::uint8_t* data, std::size_t size, Dictionary& out, std::string& why);

// Decompresses everything in `src` (all its frames) onto the end of `out`.
// `dicts`: the ones a frame may name by its ID; a frame that names none gets
// the dictionary given when there's just one (else none).
// `limit`: the most it may come to.
bool decompress(const std::uint8_t* src, std::size_t size, std::vector<std::uint8_t>& out,
                const std::vector<const Dictionary*>& dicts, std::string& why, std::size_t limit = 256u << 20);

// The dictionary ID a frame names (0: none, or not a zstd frame).
std::uint32_t frameDictionary(const std::uint8_t* src, std::size_t size);

} // namespace zstd
