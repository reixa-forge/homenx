// GameMusic - a game's own music, copied to the SD card to play on HOME.
//
// An installed game's files (its RomFS) are read through ncm and decrypted
// with the console's own keys (Lockpick_RCM's prod.keys / title.keys on the
// SD card: HOME isn't let open them otherwise), its music
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
#include <functional>
#include <map>
#include <memory>
#include <cstdint>
#include <cstddef>
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

// A game's songs: its files opened (on a thread: it takes a second or two),
// the songs listed; any one read (to hear it); the chosen ones copied.
struct Song {
    std::string path;              // in the game
    std::uint64_t offset = 0, size = 0;
    std::string name;              // the file's name on the SD card (he can change it)
    std::string ext;               // ".bfstm" / ".bwav" (as it's copied: a .zs one decompressed)
    bool zs = false;               // compressed in the game (zstd)
    bool chosen = true;
};
class GameFiles {
public:
    static std::unique_ptr<GameFiles> open(std::uint64_t appId, const std::string& gameName, std::string& why);
    ~GameFiles();
    bool read(const Song& song, std::vector<std::uint8_t>& out, std::string& why);
    std::vector<Song> songs;
    std::map<std::string, int> skipped; // files in formats not read yet, by kind
    int fileCount = 0;
private:
    GameFiles();
    struct Impl;
    std::unique_ptr<Impl> m;
};
std::string songFolder(const std::string& gameName);               // sdmc:/music/<game>
void copySongs(GameFiles& files, const std::string& gameName, Progress& p); // (a thread)

// --- the game's files (RomFS), walked through a reader: read(offset, buffer,
// size) from the RomFS's start. On the Switch the reader decrypts the game's
// program content as it goes; the tests give it a plain image. ---
struct RomfsFile {
    std::string path;          // "/Sound/Stream/x.bfstm"
    std::uint64_t offset = 0;  // from the RomFS's start
    std::uint64_t size = 0;
};
using ReadAt = std::function<bool(std::uint64_t offset, void* buffer, std::size_t size)>;
bool listRomfs(const ReadAt& read, std::vector<RomfsFile>& out, std::string& why);

// A compressed section (newer games): the table of pieces (bucket tree,
// 0x18-byte entries: virtual offset, physical offset, kind 0 stored / 1 zeros
// / 3 LZ4, physical size), read through `phys` (the section, decrypted).
struct CompressedPiece {
    std::uint64_t virt = 0, phys = 0;
    std::uint32_t physSize = 0;
    std::uint8_t kind = 0;
};
bool parseCompressionTable(const std::vector<std::uint8_t>& table, std::uint32_t entries,
                           std::vector<CompressedPiece>& out, std::string& why);
class CompressedStorage {
public:
    CompressedStorage(std::vector<CompressedPiece> pieces, std::uint64_t end, ReadAt phys);
    bool read(std::uint64_t offset, void* buffer, std::size_t size);
private:
    std::vector<CompressedPiece> m_pieces;
    std::uint64_t m_end;
    ReadAt m_phys;
    std::size_t m_cached = SIZE_MAX;            // the piece last unpacked
    std::vector<std::uint8_t> m_cache, m_packed;
};
// LZ4 block (no frame): false if it doesn't come out exactly dstSize.
bool lz4Block(const std::uint8_t* src, std::size_t srcSize, std::uint8_t* dst, std::size_t dstSize);

// A SARC archive's files (Nintendo's: little- or big-endian).
struct SarcFile {
    std::string name;
    std::size_t offset = 0, size = 0;
};
bool sarcFiles(const std::vector<std::uint8_t>& data, std::vector<SarcFile>& out);

// --- the decoders, on their own (tests) ---

// A stream: its channels decoded to PCM16 on demand.
struct Stream {
    int channels = 0, sampleRate = 0;
    std::uint32_t samples = 0;             // all of it
    bool loops = false;
    std::uint32_t loopStart = 0, loopEnd = 0;
};

// A .bfstm / .bwav seen as a .wav (16-bit), decoded as it's read: once
// through, then (a looping one) the loop again fading out, at most 6 minutes.
// HOME plays the game's own files with it (they're 3-4x smaller than WAVs).
class WavStream {
public:
    static std::unique_ptr<WavStream> open(std::vector<std::uint8_t> file, std::string& why);
    ~WavStream();
    std::uint64_t size() const;                              // the whole .wav, its header too
    std::size_t read(std::uint64_t offset, void* buffer, std::size_t size); // (any order: back means from the start)
    double seconds() const;
private:
    WavStream();
    struct Impl;
    std::unique_ptr<Impl> m;
};
bool isStream(const std::vector<std::uint8_t>& file); // a .bfstm / .bwav it can read

// Reads a .bfstm or .bwav from memory and writes a .wav (16-bit) to `out`.
// False (and why) when the file isn't one it can read.
bool convert(const std::vector<std::uint8_t>& file, const std::string& outPath, std::string& why);

// DSP-ADPCM: one 8-byte frame (14 samples) decoded.
void decodeDspFrame(const std::uint8_t* frame, const std::int16_t* coefs, std::int16_t& hist1, std::int16_t& hist2,
                    std::int16_t* out, int count);

} // namespace gamemusic
