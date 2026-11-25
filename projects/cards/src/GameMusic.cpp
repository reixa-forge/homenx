// GameMusic - see GameMusic.hpp.
#include "GameMusic.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <array>
#include <cctype>
#include <map>
#include <memory>
#include <new>
#include <set>

#ifdef __SWITCH__
#include <switch.h>
#endif

namespace gamemusic {

namespace {

// ---------------------------------------------------------------- reading

struct Reader {
    const std::vector<std::uint8_t>& d;
    bool big = false;
    bool ok(size_t off, size_t n) const { return off + n <= d.size() && off + n >= off; }
    std::uint8_t u8(size_t o) const { return ok(o, 1) ? d[o] : 0; }
    std::uint16_t u16(size_t o) const {
        if (!ok(o, 2)) return 0;
        return big ? static_cast<std::uint16_t>(d[o] << 8 | d[o + 1]) : static_cast<std::uint16_t>(d[o] | d[o + 1] << 8);
    }
    std::uint32_t u32(size_t o) const {
        if (!ok(o, 4)) return 0;
        return big ? (std::uint32_t(d[o]) << 24 | std::uint32_t(d[o + 1]) << 16 | std::uint32_t(d[o + 2]) << 8 | d[o + 3])
                   : (std::uint32_t(d[o]) | std::uint32_t(d[o + 1]) << 8 | std::uint32_t(d[o + 2]) << 16 | std::uint32_t(d[o + 3]) << 24);
    }
    std::int16_t s16(size_t o) const { return static_cast<std::int16_t>(u16(o)); }
};

// One channel of a stream, decoded in order (and from its loop again).
struct Channel {
    enum Codec { Pcm8 = 0, Pcm16 = 1, Dsp = 2 } codec = Pcm16;
    // Where its data is: blocks of all channels in turn (BFSTM), or all in one run (BWAV).
    size_t start = 0;
    std::uint32_t blockSize = 0, lastPadded = 0, blockCount = 1, channels = 1, index = 0;
    bool blocked = false;
    std::int16_t coefs[16] = {};
    std::int16_t hist1 = 0, hist2 = 0;  // DSP: the two samples before
    bool big = false;

    // Decoding state.
    std::uint32_t unit = 0;             // the next frame (DSP) or sample (PCM)
    std::int16_t frame[14] = {};
    int have = 0, used = 0;             // samples in `frame`, taken from it
    // The loop's start, as met on the way: its frame and the history then.
    std::uint32_t loopUnit = 0;
    std::int16_t loopHist1 = 0, loopHist2 = 0;
    bool loopSaved = false;

    size_t offset(std::uint32_t u, std::uint32_t unitBytes) const {
        if (!blocked) return start + size_t(u) * unitBytes;
        const std::uint32_t perBlock = blockSize / unitBytes;
        if (!perBlock) return start;
        const std::uint32_t b = u / perBlock;
        const size_t within = size_t(u % perBlock) * unitBytes;
        const std::uint32_t mine = b + 1 >= blockCount ? lastPadded : blockSize;
        return start + size_t(b) * blockSize * channels + size_t(index) * mine + within;
    }

    // The next `n` samples (zeros past the end of the data).
    void read(const std::vector<std::uint8_t>& d, std::int16_t* out, std::uint32_t n) {
        for (std::uint32_t i = 0; i < n; ++i) {
            if (codec == Dsp) {
                if (used >= have) {
                    if (unit == loopUnit && !loopSaved) {
                        loopHist1 = hist1;
                        loopHist2 = hist2;
                        loopSaved = true;
                    }
                    const size_t o = offset(unit, 8);
                    if (o + 8 <= d.size()) decodeDspFrame(&d[o], coefs, hist1, hist2, frame, 14);
                    else std::fill(frame, frame + 14, std::int16_t(0));
                    ++unit;
                    have = 14;
                    used = 0;
                }
                out[i] = frame[used++];
            } else if (codec == Pcm16) {
                const size_t o = offset(unit++, 2);
                out[i] = o + 2 <= d.size()
                             ? static_cast<std::int16_t>(big ? (d[o] << 8 | d[o + 1]) : (d[o] | d[o + 1] << 8))
                             : std::int16_t(0);
            } else {
                const size_t o = offset(unit++, 1);
                out[i] = o < d.size() ? static_cast<std::int16_t>(static_cast<std::int8_t>(d[o]) * 256) : std::int16_t(0);
            }
        }
    }

    // Back to the loop's start (after the first time through).
    void toLoop(std::uint32_t loopStart) {
        if (codec == Dsp) {
            unit = loopStart / 14;
            hist1 = loopHist1;
            hist2 = loopHist2;
            have = used = 0;
            pendingSkip = loopStart % 14; // (the frame decoded again, its first samples dropped)
        } else {
            unit = loopStart;
        }
    }
    std::uint32_t pendingSkip = 0;
};

struct Parsed {
    Stream info;
    std::vector<Channel> ch;
};

std::string magic(const std::vector<std::uint8_t>& d) {
    return d.size() >= 4 ? std::string(reinterpret_cast<const char*>(d.data()), 4) : std::string();
}

// .bfstm (Wii U big-endian, Switch little-endian).
bool parseBfstm(const std::vector<std::uint8_t>& d, Parsed& out, std::string& why) {
    Reader r{d};
    r.big = d.size() > 6 && d[4] == 0xFE && d[5] == 0xFF;
    const std::uint16_t sections = r.u16(0x10);
    size_t info = 0, data = 0;
    for (std::uint16_t i = 0; i < sections && i < 8; ++i) {
        const size_t ref = 0x14 + size_t(i) * 12;
        const std::uint16_t type = r.u16(ref);
        const std::uint32_t off = r.u32(ref + 4);
        if (type == 0x4000) info = off;
        if (type == 0x4002) data = off;
    }
    if (!info || !data || (r.u32(info) != 0x4F464E49u && r.u32(info) != 0x494E464Fu)) {
        why = "no INFO or DATA";
        return false;
    }
    const size_t ib = info + 8; // (references inside INFO are from here)
    const size_t streamInfo = ib + r.u32(ib + 4);
    const size_t chanTable = ib + r.u32(ib + 0x14);
    const std::uint8_t codec = r.u8(streamInfo + 0x00);
    if (codec > 2) {
        why = codec == 3 ? "IMA-ADPCM" : codec == 4 ? "Opus" : "codec " + std::to_string(codec);
        return false;
    }
    out.info.loops = r.u8(streamInfo + 0x01) != 0;
    const int chans = r.u8(streamInfo + 0x02);
    out.info.channels = chans;
    out.info.sampleRate = static_cast<int>(r.u32(streamInfo + 0x04));
    out.info.loopStart = r.u32(streamInfo + 0x08);
    out.info.samples = r.u32(streamInfo + 0x0C);
    out.info.loopEnd = out.info.samples;
    const std::uint32_t blockCount = r.u32(streamInfo + 0x10), blockSize = r.u32(streamInfo + 0x14);
    const std::uint32_t lastPadded = r.u32(streamInfo + 0x24);
    const std::uint32_t dataRef = r.u32(streamInfo + 0x34); // (sample data: from DATA + 8)
    if (chans < 1 || chans > 8 || !out.info.sampleRate || !out.info.samples || !blockSize) {
        why = "odd stream info";
        return false;
    }
    const std::uint32_t tableCount = r.u32(chanTable);
    for (int c = 0; c < chans; ++c) {
        Channel ch;
        ch.codec = static_cast<Channel::Codec>(codec);
        ch.big = r.big;
        ch.blocked = true;
        ch.start = data + 8 + dataRef;
        ch.blockSize = blockSize;
        ch.lastPadded = lastPadded ? lastPadded : blockSize;
        ch.blockCount = blockCount ? blockCount : 1;
        ch.channels = static_cast<std::uint32_t>(chans);
        ch.index = static_cast<std::uint32_t>(c);
        if (codec == 2) {
            if (static_cast<std::uint32_t>(c) >= tableCount) {
                why = "no DSP info";
                return false;
            }
            const size_t chInfo = chanTable + r.u32(chanTable + 4 + size_t(c) * 8 + 4);
            const size_t dsp = chInfo + r.u32(chInfo + 4);
            for (int k = 0; k < 16; ++k) ch.coefs[k] = r.s16(dsp + size_t(k) * 2);
            ch.hist1 = r.s16(dsp + 0x22);
            ch.hist2 = r.s16(dsp + 0x24);
        }
        ch.loopUnit = codec == 2 ? out.info.loopStart / 14 : out.info.loopStart;
        out.ch.push_back(ch);
    }
    return true;
}

// .bwav (the newer games': each channel's data in one run).
bool parseBwav(const std::vector<std::uint8_t>& d, Parsed& out, std::string& why) {
    Reader r{d};
    r.big = d.size() > 6 && d[4] == 0xFE && d[5] == 0xFF;
    if (r.u16(0x0C) != 0) {
        why = "prefetch only (the rest is streamed from elsewhere)";
        return false;
    }
    const int chans = r.u16(0x0E);
    if (chans < 1 || chans > 8) {
        why = "odd channel count";
        return false;
    }
    out.info.channels = chans;
    for (int c = 0; c < chans; ++c) {
        const size_t h = 0x10 + size_t(c) * 0x4C;
        const std::uint16_t codec = r.u16(h + 0x00);
        if (codec > 1) {
            why = codec == 2 ? "Opus" : "codec " + std::to_string(codec);
            return false;
        }
        Channel ch;
        ch.codec = codec == 1 ? Channel::Dsp : Channel::Pcm16;
        ch.big = r.big;
        ch.blocked = false;
        ch.start = r.u32(h + 0x30);
        for (int k = 0; k < 16; ++k) ch.coefs[k] = r.s16(h + 0x10 + size_t(k) * 2);
        ch.hist1 = r.s16(h + 0x46);
        ch.hist2 = r.s16(h + 0x48);
        if (c == 0) {
            out.info.sampleRate = static_cast<int>(r.u32(h + 0x04));
            out.info.samples = r.u32(h + 0x08);
            out.info.loops = r.u32(h + 0x38) != 0;
            out.info.loopEnd = r.u32(h + 0x3C);
            out.info.loopStart = r.u32(h + 0x40);
        }
        out.ch.push_back(ch);
    }
    if (!out.info.sampleRate || !out.info.samples) {
        why = "odd channel info";
        return false;
    }
    if (!out.info.loopEnd || out.info.loopEnd > out.info.samples) out.info.loopEnd = out.info.samples;
    for (Channel& ch : out.ch) ch.loopUnit = ch.codec == Channel::Dsp ? out.info.loopStart / 14 : out.info.loopStart;
    return true;
}

void put16(std::vector<std::uint8_t>& b, std::uint16_t v) { b.push_back(v & 0xFF); b.push_back(v >> 8); }
void put32(std::vector<std::uint8_t>& b, std::uint32_t v) { for (int i = 0; i < 4; ++i) b.push_back((v >> (8 * i)) & 0xFF); }

std::vector<std::uint8_t> wavHeader(int channels, int rate, std::uint32_t frames) {
    std::vector<std::uint8_t> h;
    const std::uint32_t bytes = frames * std::uint32_t(channels) * 2;
    h.insert(h.end(), {'R', 'I', 'F', 'F'});
    put32(h, 36 + bytes);
    h.insert(h.end(), {'W', 'A', 'V', 'E', 'f', 'm', 't', ' '});
    put32(h, 16);
    put16(h, 1);
    put16(h, static_cast<std::uint16_t>(channels));
    put32(h, static_cast<std::uint32_t>(rate));
    put32(h, static_cast<std::uint32_t>(rate * channels * 2));
    put16(h, static_cast<std::uint16_t>(channels * 2));
    put16(h, 16);
    h.insert(h.end(), {'d', 'a', 't', 'a'});
    put32(h, bytes);
    return h;
}

} // namespace

void decodeDspFrame(const std::uint8_t* f, const std::int16_t* coefs, std::int16_t& hist1, std::int16_t& hist2,
                    std::int16_t* out, int count) {
    const int header = f[0];
    const int scale = 1 << (header & 0xF);
    const int pred = (header >> 4) & 0x7;
    const int c1 = coefs[pred * 2], c2 = coefs[pred * 2 + 1];
    for (int i = 0; i < count && i < 14; ++i) {
        int nib = (i & 1) ? (f[1 + i / 2] & 0xF) : (f[1 + i / 2] >> 4);
        if (nib >= 8) nib -= 16;
        int s = ((nib * scale) << 11) + 1024 + c1 * hist1 + c2 * hist2;
        s >>= 11;
        s = std::clamp(s, -32768, 32767);
        hist2 = hist1;
        hist1 = static_cast<std::int16_t>(s);
        out[i] = static_cast<std::int16_t>(s);
    }
}

bool convert(const std::vector<std::uint8_t>& file, const std::string& outPath, std::string& why) {
    Parsed p;
    const std::string m = magic(file);
    if (m == "FSTM") {
        if (!parseBfstm(file, p, why)) return false;
    } else if (m == "BWAV") {
        if (!parseBwav(file, p, why)) return false;
    } else {
        why = "not a stream";
        return false;
    }
    const Stream& s = p.info;
    const int chans = static_cast<int>(p.ch.size());
    // Once through; then, looping, the loop again fading out (8 s, or the
    // loop's length), all of it at most 6 minutes.
    const std::uint32_t maxFrames = static_cast<std::uint32_t>(s.sampleRate) * 360u;
    const std::uint32_t first = std::min(s.loops ? s.loopEnd : s.samples, maxFrames);
    std::uint32_t again = 0;
    if (s.loops && s.loopEnd > s.loopStart && first < maxFrames)
        again = std::min(s.loopEnd - s.loopStart, maxFrames - first);
    const std::uint32_t fade = again ? std::min<std::uint32_t>(again, static_cast<std::uint32_t>(s.sampleRate) * 8u) : 0;
    const std::uint32_t total = first + again;

    const std::string tmp = outPath + ".part";
    std::FILE* f = std::fopen(tmp.c_str(), "wb");
    if (!f) {
        why = "can't write to the SD card";
        return false;
    }
    std::vector<std::uint8_t> head = wavHeader(chans, s.sampleRate, total);
    bool ok = std::fwrite(head.data(), 1, head.size(), f) == head.size();
    constexpr std::uint32_t kChunk = 14 * 512;
    std::vector<std::int16_t> chan(kChunk), mix(size_t(kChunk) * chans);
    auto pass = [&](std::uint32_t frames, bool fading) {
        std::uint32_t doneFrames = 0;
        while (ok && doneFrames < frames) {
            const std::uint32_t n = std::min(kChunk, frames - doneFrames);
            for (int c = 0; c < chans; ++c) {
                Channel& ch = p.ch[(size_t)c];
                if (ch.pendingSkip) { // (back at the loop mid-frame: its first samples dropped)
                    std::int16_t skip[14];
                    ch.read(file, skip, ch.pendingSkip);
                    ch.pendingSkip = 0;
                }
                ch.read(file, chan.data(), n);
                for (std::uint32_t i = 0; i < n; ++i) {
                    int v = chan[i];
                    if (fading) {
                        const std::uint32_t at = doneFrames + i;
                        if (at + fade >= frames) {
                            const float left = static_cast<float>(frames - at) / static_cast<float>(std::max<std::uint32_t>(1, fade));
                            v = static_cast<int>(static_cast<float>(v) * left);
                        }
                    }
                    mix[size_t(i) * chans + c] = static_cast<std::int16_t>(v);
                }
            }
            // (little-endian on the Switch, as WAV is)
            ok = std::fwrite(mix.data(), sizeof(std::int16_t), size_t(n) * chans, f) == size_t(n) * chans;
            doneFrames += n;
        }
    };
    pass(first, false);
    if (again) {
        for (Channel& ch : p.ch) ch.toLoop(s.loopStart);
        pass(again, true);
    }
    ok = std::fclose(f) == 0 && ok;
    std::error_code ec;
    if (!ok) {
        std::filesystem::remove(tmp, ec);
        why = "the SD card is full, or couldn't be written";
        return false;
    }
    std::filesystem::remove(outPath, ec);
    std::filesystem::rename(tmp, outPath, ec);
    return !ec;
}

// ---------------------------------------------------------------- RomFS

namespace {
std::uint32_t le32(const std::uint8_t* p) { return std::uint32_t(p[0]) | std::uint32_t(p[1]) << 8 | std::uint32_t(p[2]) << 16 | std::uint32_t(p[3]) << 24; }
std::uint64_t le64(const std::uint8_t* p) { return std::uint64_t(le32(p)) | std::uint64_t(le32(p + 4)) << 32; }
} // namespace

// Header: ten u64s (its size 0x50, then directory hash / directory entries /
// file hash / file entries as offset+size pairs, then where the files' data
// starts). Directory entry: parent, sibling, first child directory, first
// file, hash chain, name length (u32s), name. File entry: parent, sibling
// (u32), data offset, size (u64), hash chain, name length (u32), name.
// "None" is 0xFFFFFFFF.
bool listRomfs(const ReadAt& read, std::vector<RomfsFile>& out, std::string& why) {
    std::uint8_t h[0x50];
    if (!read(0, h, sizeof(h))) { why = "its files couldn't be read"; return false; }
    if (le64(h) != 0x50) { why = "its files couldn't be read (not a RomFS: wrong key?)"; return false; }
    const std::uint64_t dirOff = le64(h + 0x18), dirSize = le64(h + 0x20);
    const std::uint64_t fileOff = le64(h + 0x38), fileSize = le64(h + 0x40), dataOff = le64(h + 0x48);
    if (dirSize > 64u << 20 || fileSize > 64u << 20 || dirSize < 0x18) { why = "its file list looks wrong"; return false; }
    std::vector<std::uint8_t> dirs(static_cast<size_t>(dirSize)), files(static_cast<size_t>(fileSize));
    if (!read(dirOff, dirs.data(), dirs.size()) || (!files.empty() && !read(fileOff, files.data(), files.size()))) {
        why = "its file list couldn't be read";
        return false;
    }
    constexpr std::uint32_t kNone = 0xFFFFFFFFu;
    struct Todo { std::uint32_t dir; std::string path; };
    std::vector<Todo> todo{{0, ""}};
    size_t steps = 0;
    while (!todo.empty()) {
        Todo t = std::move(todo.back());
        todo.pop_back();
        if (++steps > 200000 || size_t(t.dir) + 0x18 > dirs.size()) continue; // (a loop, or out of the table)
        const std::uint8_t* d = dirs.data() + t.dir;
        // its files
        for (std::uint32_t f = le32(d + 0xC); f != kNone && size_t(f) + 0x20 <= files.size() && ++steps < 2000000;) {
            const std::uint8_t* e = files.data() + f;
            const std::uint32_t len = le32(e + 0x1C);
            if (size_t(f) + 0x20 + len > files.size()) break;
            out.push_back({t.path + "/" + std::string(reinterpret_cast<const char*>(e + 0x20), len), dataOff + le64(e + 8), le64(e + 0x10)});
            f = le32(e + 4);
        }
        // its directories (each with its own path)
        for (std::uint32_t c = le32(d + 8); c != kNone && size_t(c) + 0x18 <= dirs.size() && ++steps < 2000000;) {
            const std::uint8_t* e = dirs.data() + c;
            const std::uint32_t len = le32(e + 0x14);
            if (size_t(c) + 0x18 + len > dirs.size()) break;
            todo.push_back({c, t.path + "/" + std::string(reinterpret_cast<const char*>(e + 0x18), len)});
            c = le32(e + 4);
        }
    }
    return true;
}

#ifdef __SWITCH__

namespace {

void logLine(const std::string& line) {
    std::FILE* f = std::fopen("sdmc:/config/CardsNX/gamemusic.log", "a");
    if (!f) return;
    const std::time_t t = std::time(nullptr);
    char when[32];
    std::strftime(when, sizeof(when), "%Y-%m-%d %H:%M:%S", std::localtime(&t));
    std::fprintf(f, "%s %s\n", when, line.c_str());
    std::fclose(f);
}

std::string hex(std::uint64_t v) {
    char b[24];
    std::snprintf(b, sizeof(b), "%016llX", static_cast<unsigned long long>(v));
    return b;
}

// ---- the console's keys (Lockpick_RCM's prod.keys and title.keys on the SD card)

using Key = std::array<std::uint8_t, 16>;

struct Keys {
    std::uint8_t header[32] = {};
    bool hasHeader = false;
    std::map<int, Key> areaKey[3]; // key_area_key_{application, ocean, system}_XX
    std::map<int, Key> titlekek;   // titlekek_XX
    std::map<std::string, Key> title; // rights ID (lower-case hex) -> title key
};

int hexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool fromHex(const std::string& s, std::uint8_t* out, size_t n) {
    if (s.size() < n * 2) return false;
    for (size_t i = 0; i < n; ++i) {
        const int a = hexDigit(s[2 * i]), b = hexDigit(s[2 * i + 1]);
        if (a < 0 || b < 0) return false;
        out[i] = static_cast<std::uint8_t>(a << 4 | b);
    }
    return true;
}

std::string trim(std::string s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
    size_t i = 0;
    while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
    return s.substr(i);
}

// "name = value" lines.
// "0a" -> 10; -1 for anything that isn't a hex number ("..._source").
int keyNumber(const std::string& s) {
    if (s.empty() || s.size() > 2) return -1;
    int n = 0;
    for (char c : s) {
        const int d = hexDigit(c);
        if (d < 0) return -1;
        n = n * 16 + d;
    }
    return n;
}

void readKeyFile(const char* path, const std::function<void(const std::string&, const std::string&)>& each) {
    std::ifstream in(path);
    std::string line;
    while (std::getline(in, line)) {
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string name = trim(line.substr(0, eq)), value = trim(line.substr(eq + 1));
        for (char& c : name) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        each(name, value);
    }
}

bool loadKeys(Keys& k, std::string& why) {
    const char* prod = nullptr;
    for (const char* p : {"sdmc:/switch/prod.keys", "sdmc:/prod.keys", "sdmc:/switch/keys.txt"})
        if (std::filesystem::exists(p)) { prod = p; break; }
    if (!prod) {
        why = "no keys: put Lockpick_RCM's prod.keys in the SD card's switch folder";
        return false;
    }
    readKeyFile(prod, [&k](const std::string& name, const std::string& value) {
        if (name == "header_key") { k.hasHeader = fromHex(value, k.header, 32); return; }
        static const char* kArea[3] = {"key_area_key_application_", "key_area_key_ocean_", "key_area_key_system_"};
        for (int i = 0; i < 3; ++i)
            if (name.rfind(kArea[i], 0) == 0) {
                Key key;
                const int n = keyNumber(name.substr(std::strlen(kArea[i])));
                if (n >= 0 && fromHex(value, key.data(), 16)) k.areaKey[i][n] = key;
                return;
            }
        if (name.rfind("titlekek_", 0) == 0) {
            Key key;
            const int n = keyNumber(name.substr(9));
            if (n >= 0 && fromHex(value, key.data(), 16)) k.titlekek[n] = key;
        }
    });
    for (const char* p : {"sdmc:/switch/title.keys", "sdmc:/title.keys"})
        if (std::filesystem::exists(p)) {
            readKeyFile(p, [&k](const std::string& name, const std::string& value) {
                Key key;
                if (name.size() == 32 && fromHex(value, key.data(), 16)) k.title[name] = key;
            });
            break;
        }
    logLine(std::string("keys: ") + prod + (k.hasHeader ? "" : " (no header_key)") + ", " +
            std::to_string(k.areaKey[0].size()) + " key area keys, " + std::to_string(k.titlekek.size()) + " titlekeks, " +
            std::to_string(k.title.size()) + " title keys");
    if (!k.hasHeader) {
        why = "prod.keys has no header_key";
        return false;
    }
    return true;
}

// ---- the game's program content (an NCA), read through ncm and decrypted here

struct Nca {
    NcmContentStorage cs{};
    NcmContentId cid{};
    bool open = false;
    std::uint64_t sectionStart = 0;  // the RomFS section, in the NCA
    std::uint64_t romfsStart = 0;    // the RomFS, in the section (IVFC's data level)
    int encryption = 1;              // 1 none, 3 AES-CTR
    Key key{};
    std::uint8_t ctrHigh[8] = {};
    std::vector<std::uint8_t> raw;   // (a buffer, kept)

    ~Nca() { if (open) ncmContentStorageClose(&cs); }

    bool readRaw(std::uint64_t off, void* buf, size_t n) {
        auto* out = static_cast<std::uint8_t*>(buf);
        while (n) {
            const size_t part = std::min<size_t>(n, 1u << 20);
            if (R_FAILED(ncmContentStorageReadContentIdFile(&cs, out, part, &cid, static_cast<s64>(off)))) return false;
            out += part;
            off += part;
            n -= part;
        }
        return true;
    }

    // From the RomFS's start, decrypted.
    bool read(std::uint64_t off, void* buf, size_t n) {
        std::uint64_t at = sectionStart + romfsStart + off;
        if (encryption == 1) return readRaw(at, buf, n);
        auto* out = static_cast<std::uint8_t*>(buf);
        while (n) {
            const std::uint64_t aligned = at & ~0xFull;
            const size_t skip = static_cast<size_t>(at - aligned);
            const size_t part = std::min<size_t>(n, (1u << 20) - skip);
            const size_t whole = (skip + part + 15) & ~size_t(15);
            raw.resize(whole);
            if (!readRaw(aligned, raw.data(), whole)) return false;
            std::uint8_t ctr[16];
            for (int i = 0; i < 8; ++i) ctr[i] = ctrHigh[7 - i];
            std::uint64_t block = aligned >> 4;
            for (int i = 15; i >= 8; --i) { ctr[i] = static_cast<std::uint8_t>(block); block >>= 8; }
            Aes128CtrContext c;
            aes128CtrContextCreate(&c, key.data(), ctr);
            aes128CtrCrypt(&c, raw.data(), raw.data(), whole);
            std::memcpy(out, raw.data() + skip, part);
            out += part;
            n -= part;
            at += part;
        }
        return true;
    }
};

bool looksLikeRomfs(Nca& nca) {
    std::uint8_t h[0x50];
    return nca.read(0, h, sizeof(h)) && le64(h) == 0x50 && le64(h + 0x48) >= 0x50;
}

// Finds the game's program content, decrypts its header, picks its RomFS
// section and that section's key. Each step logged.
bool openGame(std::uint64_t appId, const Keys& keys, Nca& nca, std::string& why) {
    if (R_FAILED(ncmInitialize())) { why = "ncm"; return false; }
    bool found = false;
    for (const NcmStorageId sid : {NcmStorageId_SdCard, NcmStorageId_BuiltInUser, NcmStorageId_GameCard}) {
        NcmContentMetaDatabase db;
        if (R_FAILED(ncmOpenContentMetaDatabase(&db, sid))) continue;
        NcmContentMetaKey metaKey;
        NcmContentId cid;
        Result rc = ncmContentMetaDatabaseGetLatestContentMetaKey(&db, &metaKey, appId);
        if (R_SUCCEEDED(rc)) rc = ncmContentMetaDatabaseGetContentIdByType(&db, &cid, &metaKey, NcmContentType_Program);
        ncmContentMetaDatabaseClose(&db);
        if (R_FAILED(rc)) continue;
        if (R_FAILED(ncmOpenContentStorage(&nca.cs, sid))) continue;
        nca.cid = cid;
        nca.open = true;
        found = true;
        logLine("program content on storage " + std::to_string(static_cast<int>(sid)));
        break;
    }
    ncmExit();
    if (!found) { why = "its files weren't found"; return false; }

    std::vector<std::uint8_t> h(0xC00);
    if (!nca.readRaw(0, h.data(), h.size())) { why = "its files couldn't be read"; return false; }
    Aes128XtsContext xts;
    aes128XtsContextCreate(&xts, keys.header, keys.header + 16, false);
    for (int sector = 0; sector < 6; ++sector) {
        aes128XtsContextResetSector(&xts, static_cast<std::uint64_t>(sector), true);
        aes128XtsDecrypt(&xts, h.data() + sector * 0x200, h.data() + sector * 0x200, 0x200);
    }
    if (std::memcmp(h.data() + 0x200, "NCA3", 4) != 0) {
        why = "its files couldn't be read (the header_key doesn't fit)";
        logLine("header: not NCA3");
        return false;
    }
    const int gen = std::max<int>(h[0x206], h[0x220]);
    const int keyIndex = gen ? gen - 1 : 0;
    const int areaKind = std::min<int>(h[0x207], 2);
    bool rights = false;
    for (int i = 0; i < 16; ++i) rights |= h[0x230 + i] != 0;
    logLine("header: key generation " + std::to_string(gen) + (rights ? ", needs a title key" : ""));

    // The RomFS section: RomFS kind, with an integrity tree.
    int section = -1;
    for (int i = 0; i < 4; ++i) {
        const std::uint8_t* fs = h.data() + 0x400 + i * 0x200;
        if (le32(h.data() + 0x240 + i * 0x10) == 0) continue;
        if (fs[2] == 0 && fs[3] == 3 && std::memcmp(fs + 8, "IVFC", 4) == 0) { section = i; break; }
    }
    if (section < 0) { why = "it has no files (no RomFS)"; return false; }
    const std::uint8_t* fs = h.data() + 0x400 + section * 0x200;
    nca.sectionStart = std::uint64_t(le32(h.data() + 0x240 + section * 0x10)) * 0x200;
    nca.encryption = fs[4];
    const std::uint32_t layers = le32(fs + 8 + 0xC);
    if (layers < 2 || layers > 7) { why = "its files look wrong"; return false; }
    nca.romfsStart = le64(fs + 8 + 0x10 + (layers - 2) * 0x18);
    std::memcpy(nca.ctrHigh, fs + 0x140, 8);
    if (nca.encryption != 1 && nca.encryption != 3) {
        why = "its files are encrypted a way not read yet (" + std::to_string(nca.encryption) + ")";
        return false;
    }

    // Its key: from the key area, or (bought games) the title key.
    std::vector<Key> tries;
    if (!rights) {
        const auto k = keys.areaKey[areaKind].find(keyIndex);
        if (k == keys.areaKey[areaKind].end()) {
            why = "prod.keys is missing key_area_key " + std::to_string(keyIndex) + ": run Lockpick_RCM again";
            return false;
        }
        Aes128Context ecb;
        aes128ContextCreate(&ecb, k->second.data(), false);
        Key key;
        aes128DecryptBlock(&ecb, key.data(), h.data() + 0x300 + 2 * 0x10);
        tries.push_back(key);
    } else {
        char rid[33];
        for (int i = 0; i < 16; ++i) std::snprintf(rid + 2 * i, 3, "%02x", h[0x230 + i]);
        const auto t = keys.title.find(rid);
        if (t == keys.title.end()) {
            why = "its title key isn't in title.keys: run Lockpick_RCM again";
            logLine(std::string("no title key for rights ID ") + rid);
            return false;
        }
        tries.push_back(t->second); // (as it is: already decrypted)
        const auto kek = keys.titlekek.find(keyIndex);
        if (kek != keys.titlekek.end()) { // (or still encrypted with the titlekek)
            Aes128Context ecb;
            aes128ContextCreate(&ecb, kek->second.data(), false);
            Key key;
            aes128DecryptBlock(&ecb, key.data(), t->second.data());
            tries.push_back(key);
        }
    }
    for (const Key& k : tries) {
        nca.key = k;
        if (nca.encryption == 1 || looksLikeRomfs(nca)) {
            logLine("RomFS opened (section " + std::to_string(section) + ")");
            return true;
        }
    }
    why = "its files couldn't be decrypted (keys too old? run Lockpick_RCM again)";
    logLine("RomFS: no key gave a RomFS");
    return false;
}

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string safeName(std::string s) {
    for (char& c : s)
        if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') c = ' ';
    while (!s.empty() && (s.back() == ' ' || s.back() == '.')) s.pop_back();
    if (s.size() > 60) s.resize(60);
    return s.empty() ? std::string("Game") : s;
}

} // namespace

void copyGameMusic(std::uint64_t appId, const std::string& gameName, Progress& p) {
    std::string why;
    logLine("---- " + gameName + " (" + hex(appId) + ")");
    const auto fail = [&p, &why]() {
        logLine("stopped: " + why);
        std::lock_guard<std::mutex> lk(p.mu);
        p.error = why;
        p.finished = true;
    };
    auto keys = std::make_unique<Keys>();
    if (!loadKeys(*keys, why)) return fail();
    auto nca = std::make_unique<Nca>();
    if (!openGame(appId, *keys, *nca, why)) return fail();
    keys.reset();
    std::vector<RomfsFile> all;
    if (!listRomfs([&nca](std::uint64_t off, void* buf, size_t n) { return nca->read(off, buf, n); }, all, why)) return fail();

    // The streams (the whole RomFS: games keep them in different places).
    std::vector<RomfsFile> streams;
    for (RomfsFile& f : all) {
        const std::string name = lower(f.path);
        const auto ends = [&name](const char* e) {
            const size_t n = std::strlen(e);
            return name.size() >= n && name.compare(name.size() - n, n, e) == 0;
        };
        if (ends(".bfstm") || ends(".bwav")) streams.push_back(std::move(f));
        else if (ends(".bwav.zs") || ends(".bfstm.zs") || ends(".bars") || ends(".bars.zs") || ends(".wem") ||
                 ends(".bnk") || ends(".bank") || ends(".acb") || ends(".awb") || ends(".hca") || ends(".opus") || ends(".lopus"))
            ++p.skipped;
    }
    logLine("files: " + std::to_string(all.size()) + ", streams " + std::to_string(streams.size()) + ", " +
            std::to_string(p.skipped.load()) + " in other formats");
    all.clear();
    all.shrink_to_fit();
    std::sort(streams.begin(), streams.end(), [](const RomfsFile& a, const RomfsFile& b) { return a.path < b.path; });
    p.found = static_cast<int>(streams.size());
    const std::string folder = "sdmc:/music/" + safeName(gameName);
    {
        std::lock_guard<std::mutex> lk(p.mu);
        p.folder = folder;
    }
    std::error_code ec;
    if (!streams.empty()) std::filesystem::create_directories(folder, ec);
    std::set<std::string> used;
    for (const RomfsFile& f : streams) {
        if (p.cancel) break;
        std::string stem = safeName(std::filesystem::path(f.path).stem().string());
        for (int n = 2; !used.insert(lower(stem)).second; ++n) // (same name in two folders)
            stem = safeName(std::filesystem::path(f.path).stem().string()) + " " + std::to_string(n);
        const std::string out = folder + "/" + stem + ".wav";
        if (std::filesystem::exists(out, ec)) { // (copied before)
            ++p.done;
            std::lock_guard<std::mutex> lk(p.mu);
            p.files.push_back(out);
            continue;
        }
        if (f.size > 32u * 1024u * 1024u) { // (too big to read in at once in HOME's memory)
            ++p.failed;
            logLine("  " + f.path + ": too big (" + std::to_string(f.size / 1048576u) + " MB)");
            continue;
        }
        std::vector<std::uint8_t> data;
        try {
            data.resize(static_cast<size_t>(f.size));
        } catch (const std::bad_alloc&) { // (HOME is short of memory just now)
            ++p.failed;
            logLine("  " + f.path + ": no memory for it");
            continue;
        }
        if (!nca->read(f.offset, data.data(), data.size())) {
            ++p.failed;
            logLine("  " + f.path + ": couldn't be read");
            continue;
        }
        std::string bad;
        if (convert(data, out, bad)) {
            ++p.done;
            std::lock_guard<std::mutex> lk(p.mu);
            p.files.push_back(out);
        } else {
            ++p.failed;
            logLine("  " + f.path + ": " + bad);
            if (bad.rfind("the SD card", 0) == 0) {
                std::lock_guard<std::mutex> lk(p.mu);
                p.error = bad;
                break;
            }
        }
    }
    logLine("done: " + std::to_string(p.done.load()) + " written, " + std::to_string(p.failed.load()) + " failed");
    p.finished = true;
}

#else

void copyGameMusic(std::uint64_t, const std::string&, Progress& p) {
    std::lock_guard<std::mutex> lk(p.mu);
    p.error = "only on the Switch";
    p.finished = true;
}

#endif

} // namespace gamemusic
