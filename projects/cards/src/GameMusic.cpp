// GameMusic - see GameMusic.hpp.
#include "GameMusic.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <new>

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

constexpr const char* kMount = "gamemusic";
std::uint64_t g_fakePid = 0; // the registration made for the copy (unregistered when done)

// The game registered with FS's program registry (as pm does when it starts
// one), under a process ID no process has: FS opens a program's files by ID
// only for programs it has registered ("not found", 2002-6605, otherwise).
// Access as the menu's own: all of it (read-only use).
bool registerGame(std::uint64_t appId, NcmStorageId sid) {
    const Result init = fsprInitialize();
    if (R_FAILED(init)) {
        logLine("fs program registry (fsp-pr): rc=0x" + hex(init));
        return false;
    }
    struct __attribute__((packed)) Fah { // the ACI's FS access header
        u8 version = 1;
        u8 pad[3] = {};
        u64 permissions = ~0ull;
        u32 contentOwnerOffset = 0x1C, contentOwnerSize = 0, saveOwnerOffset = 0x1C, saveOwnerSize = 0;
    } fah;
    struct __attribute__((packed)) Fac { // the ACID's FS access control
        u8 version = 1;
        u8 contentOwnerCount = 0, saveOwnerCount = 0, pad = 0;
        u64 permissions = ~0ull;
        u64 contentOwnerMin = 0, contentOwnerMax = 0, saveOwnerMin = 0, saveOwnerMax = 0;
    } fac;
    static_assert(sizeof(Fah) == 0x1C && sizeof(Fac) == 0x2C);
    const std::uint64_t pid = 0x7E570000ull + (armGetSystemTick() & 0xFFFF); // (no real process has one this big)
    const Result rc = fsprRegisterProgram(pid, appId, sid, &fah, sizeof(fah), &fac, sizeof(fac), 0);
    logLine("registered " + hex(appId) + " (storage " + std::to_string(static_cast<int>(sid)) + "): rc=0x" + hex(rc));
    if (R_FAILED(rc)) {
        fsprExit();
        return false;
    }
    g_fakePid = pid;
    return true;
}

void unregisterGame() {
    if (!g_fakePid) return;
    fsprUnregisterProgram(g_fakePid);
    fsprExit();
    g_fakePid = 0;
}

// The game's files (RomFS), read-only, as "gamemusic:/". Two ways: by its
// program ID (FS finds it), else its program content found with ncm and
// opened directly. Each try is logged.
bool mountGame(std::uint64_t appId, bool& asRomfs, std::string& why) {
    FsStorage storage;
    Result rc = fsOpenDataStorageByProgramId(&storage, appId);
    if (R_SUCCEEDED(rc)) {
        rc = romfsMountFromStorage(storage, 0, kMount);
        if (R_SUCCEEDED(rc)) {
            logLine("opened " + hex(appId) + " by program ID");
            asRomfs = true;
            return true;
        }
        fsStorageClose(&storage);
    }
    logLine("by program ID: rc=0x" + hex(rc));
    // Not running (so FS doesn't know it): registered for the copy, then by ID again.
    for (const NcmStorageId sid : {NcmStorageId_SdCard, NcmStorageId_BuiltInUser, NcmStorageId_GameCard}) {
        if (!registerGame(appId, sid)) continue;
        rc = fsOpenDataStorageByProgramId(&storage, appId);
        logLine("  then by program ID: rc=0x" + hex(rc));
        if (R_SUCCEEDED(rc)) {
            rc = romfsMountFromStorage(storage, 0, kMount);
            logLine("  mounted: rc=0x" + hex(rc));
            if (R_SUCCEEDED(rc)) {
                asRomfs = true;
                return true;
            }
            fsStorageClose(&storage);
        }
        unregisterGame();
    }
    if (R_FAILED(ncmInitialize())) {
        why = "ncm";
        return false;
    }
    bool ok = false;
    for (const NcmStorageId sid : {NcmStorageId_SdCard, NcmStorageId_BuiltInUser, NcmStorageId_GameCard}) {
        NcmContentMetaDatabase db;
        NcmContentStorage cs;
        if (R_FAILED(ncmOpenContentMetaDatabase(&db, sid))) continue;
        if (R_FAILED(ncmOpenContentStorage(&cs, sid))) {
            ncmContentMetaDatabaseClose(&db);
            continue;
        }
        NcmContentMetaKey key;
        NcmContentId cid;
        char path[FS_MAX_PATH] = {};
        Result r1 = ncmContentMetaDatabaseGetLatestContentMetaKey(&db, &key, appId);
        Result r2 = R_SUCCEEDED(r1) ? ncmContentMetaDatabaseGetContentIdByType(&db, &cid, &key, NcmContentType_Program) : r1;
        Result r3 = R_SUCCEEDED(r2) ? ncmContentStorageGetPath(&cs, path, sizeof(path), &cid) : r2;
        if (R_SUCCEEDED(r3)) {
            FsFileSystem fs;
            Result r4 = fsOpenFileSystemWithId(&fs, appId, FsFileSystemType_ContentData, path, FsContentAttributes_All);
            logLine("storage " + std::to_string(static_cast<int>(sid)) + ": " + path + " rc=0x" + hex(r4));
            if (R_FAILED(r4)) { // the program content's own RomFS (FS's "Rom" kind, 1: not named in libnx)
                r4 = fsOpenFileSystemWithId(&fs, appId, static_cast<FsFileSystemType>(1), path, FsContentAttributes_All);
                logLine("  as its RomFS: rc=0x" + hex(r4));
            }
            if (R_SUCCEEDED(r4)) {
                if (fsdevMountDevice(kMount, fs) >= 0) {
                    ok = true;
                    asRomfs = false;
                } else {
                    fsFsClose(&fs);
                }
            }
        } else {
            logLine("storage " + std::to_string(static_cast<int>(sid)) + ": not there (rc=0x" + hex(r3) + ")");
        }
        ncmContentStorageClose(&cs);
        ncmContentMetaDatabaseClose(&db);
        if (ok) break;
    }
    ncmExit();
    if (!ok) why = "the game's files couldn't be opened";
    return ok;
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
    bool asRomfs = false;
    std::string why;
    logLine("---- " + gameName + " (" + hex(appId) + ")");
    if (!mountGame(appId, asRomfs, why)) {
        std::lock_guard<std::mutex> lk(p.mu);
        p.error = why;
        p.finished = true;
        return;
    }
    // Find the streams (the whole RomFS: games keep them in different places).
    std::vector<std::string> streams;
    {
        std::error_code ec;
        std::filesystem::recursive_directory_iterator it(std::string(kMount) + ":/", ec), end;
        for (; !ec && it != end && !p.cancel; it.increment(ec)) {
            if (!it->is_regular_file(ec)) continue;
            const std::string name = lower(it->path().filename().string());
            const auto ends = [&name](const char* e) {
                const size_t n = std::strlen(e);
                return name.size() >= n && name.compare(name.size() - n, n, e) == 0;
            };
            if (ends(".bfstm") || ends(".bwav")) streams.push_back(it->path().string());
            else if (ends(".bwav.zs") || ends(".bfstm.zs") || ends(".bars") || ends(".bars.zs") || ends(".wem") ||
                     ends(".bnk") || ends(".bank") || ends(".acb") || ends(".awb") || ends(".hca") || ends(".opus") || ends(".lopus"))
                ++p.skipped;
        }
    }
    std::sort(streams.begin(), streams.end());
    p.found = static_cast<int>(streams.size());
    logLine("found " + std::to_string(streams.size()) + " streams, " + std::to_string(p.skipped.load()) + " in other formats");
    const std::string folder = "sdmc:/music/" + safeName(gameName);
    {
        std::lock_guard<std::mutex> lk(p.mu);
        p.folder = folder;
    }
    std::error_code ec;
    if (!streams.empty()) std::filesystem::create_directories(folder, ec);
    for (const std::string& path : streams) {
        if (p.cancel) break;
        std::string stem = std::filesystem::path(path).stem().string();
        const std::string out = folder + "/" + safeName(stem) + ".wav";
        if (std::filesystem::exists(out, ec)) { // (copied before)
            ++p.done;
            std::lock_guard<std::mutex> lk(p.mu);
            p.files.push_back(out);
            continue;
        }
        const auto size = std::filesystem::file_size(path, ec);
        if (ec || size > 32u * 1024u * 1024u) { // (too big to read in at once in HOME's memory)
            ++p.failed;
            logLine("  " + path + ": too big (" + std::to_string(size / 1048576u) + " MB)");
            continue;
        }
        std::vector<std::uint8_t> data;
        try {
            data.resize(static_cast<size_t>(size));
        } catch (const std::bad_alloc&) { // (HOME is short of memory just now)
            ++p.failed;
            logLine("  " + path + ": no memory for it");
            continue;
        }
        {
            std::ifstream in(path, std::ios::binary);
            in.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));
            if (!in) {
                ++p.failed;
                continue;
            }
        }
        std::string fail;
        if (convert(data, out, fail)) {
            ++p.done;
            std::lock_guard<std::mutex> lk(p.mu);
            p.files.push_back(out);
        } else {
            ++p.failed;
            logLine("  " + path + ": " + fail);
            if (fail.rfind("the SD card", 0) == 0) {
                std::lock_guard<std::mutex> lk(p.mu);
                p.error = fail;
                break;
            }
        }
    }
    if (asRomfs) romfsUnmount(kMount);
    else fsdevUnmountDevice(kMount);
    unregisterGame();
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
