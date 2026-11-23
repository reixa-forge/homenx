// GameMusic - a game's own music, copied to the SD card to play on HOME.
//
// An installed game's files (its RomFS) are opened read-only, its music
// streams found (Nintendo's formats: .bfstm and .bwav, PCM16 or DSP-ADPCM),
// and each one written to sdmc:/music/<game>/<name>.wav: once through, then
// its loop once more fading out. Done in the background; HOME plays them as
// any other song (Settings > Sound > Your music) and a playlist is made of
// them.
//
// Not yet: music packed in archives (.bars), compressed files (.zs: zstd),
// and other engines' formats (Wwise .wem, FMOD .bank, CRI .hca): counted and
// skipped. How the game's files were opened is logged to
// sdmc:/config/CardsNX/gamemusic.log.
#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace gamemusic {

struct Progress {
    std::atomic<int> found{0};     // music streams found
    std::atomic<int> done{0};      // written (or there already)
    std::atomic<int> failed{0};
    std::atomic<int> skipped{0};   // formats it can't read yet
    std::atomic<bool> finished{false};
    std::atomic<bool> cancel{false};
    std::mutex mu;
    std::string error;             // why nothing came, if nothing did
    std::string folder;            // where the songs went
    std::vector<std::string> files; // the songs written (paths)
};

// The whole job (run it on a thread): open the game, find, convert, close.
void copyGameMusic(std::uint64_t appId, const std::string& gameName, Progress& p);

// --- the decoders, on their own (tests) ---

// A stream: its channels decoded to PCM16 on demand.
struct Stream {
    int channels = 0, sampleRate = 0;
    std::uint32_t samples = 0;             // all of it
    bool loops = false;
    std::uint32_t loopStart = 0, loopEnd = 0;
};

// Reads a .bfstm or .bwav from memory and writes a .wav (16-bit) to `out`.
// False (and why) when the file isn't one it can read.
bool convert(const std::vector<std::uint8_t>& file, const std::string& outPath, std::string& why);

// DSP-ADPCM: one 8-byte frame (14 samples) decoded.
void decodeDspFrame(const std::uint8_t* frame, const std::int16_t* coefs, std::int16_t& hist1, std::int16_t& hist2,
                    std::int16_t* out, int count);

} // namespace gamemusic
