// GameMusic - see GameMusic.hpp.
#include "GameMusic.hpp"
#include "Zstd.hpp"

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

// ---------------------------------------------------------------- as a WAV, on demand

struct WavStream::Impl {
    std::vector<std::uint8_t> file;
    Parsed p;
    std::uint32_t first = 0, again = 0, fade = 0, total = 0;
    std::vector<std::uint8_t> head;
    // The decoded samples, a piece at a time: bytes [bufStart, bufStart + buf.size()) of the data.
    std::vector<std::uint8_t> buf;
    std::uint64_t bufStart = 0;
    std::uint32_t frame = 0;          // the next frame to decode (0 .. total)
    std::vector<Channel> fresh;       // the channels as parsed (to start again)
    std::vector<std::int16_t> chan;

    void restart() {
        p.ch = fresh;
        frame = 0;
        buf.clear();
        bufStart = 0;
    }

    // The next piece (up to 4096 frames) into buf.
    bool decodeNext() {
        if (frame >= total) return false;
        const int chans = static_cast<int>(p.ch.size());
        bufStart += buf.size();
        if (frame == first && again) // (on into the loop again)
            for (Channel& ch : p.ch) ch.toLoop(p.info.loopStart);
        const std::uint32_t passEnd = frame < first ? first : total;
        const std::uint32_t n = std::min<std::uint32_t>(4096, passEnd - frame);
        buf.assign(size_t(n) * chans * 2, 0);
        chan.resize(n);
        auto* out = reinterpret_cast<std::int16_t*>(buf.data());
        for (int c = 0; c < chans; ++c) {
            Channel& ch = p.ch[(size_t)c];
            if (ch.pendingSkip) {
                std::int16_t skip[14];
                ch.read(file, skip, ch.pendingSkip);
                ch.pendingSkip = 0;
            }
            ch.read(file, chan.data(), n);
            for (std::uint32_t i = 0; i < n; ++i) {
                int v = chan[i];
                if (frame >= first) { // fading out, the second time
                    const std::uint32_t at = frame + i - first;
                    if (at + fade >= again)
                        v = static_cast<int>(static_cast<float>(v) * static_cast<float>(again - at) /
                                             static_cast<float>(std::max<std::uint32_t>(1, fade)));
                }
                out[size_t(i) * chans + c] = static_cast<std::int16_t>(v);
            }
        }
        frame += n;
        return true;
    }
};

WavStream::WavStream() : m(std::make_unique<Impl>()) {}
WavStream::~WavStream() = default;

std::unique_ptr<WavStream> WavStream::open(std::vector<std::uint8_t> file, std::string& why) {
    std::unique_ptr<WavStream> w(new WavStream());
    Impl& i = *w->m;
    i.file = std::move(file);
    const std::string m = magic(i.file);
    if (m == "FSTM") {
        if (!parseBfstm(i.file, i.p, why)) return nullptr;
    } else if (m == "BWAV") {
        if (!parseBwav(i.file, i.p, why)) return nullptr;
    } else {
        why = "not a stream";
        return nullptr;
    }
    const Stream& s = i.p.info;
    // Once through; then, looping, the loop again fading out (8 s, or the
    // loop's length), all of it at most 6 minutes.
    const std::uint32_t maxFrames = static_cast<std::uint32_t>(s.sampleRate) * 360u;
    i.first = std::min(s.loops ? s.loopEnd : s.samples, maxFrames);
    if (s.loops && s.loopEnd > s.loopStart && i.first < maxFrames)
        i.again = std::min(s.loopEnd - s.loopStart, maxFrames - i.first);
    i.fade = i.again ? std::min<std::uint32_t>(i.again, static_cast<std::uint32_t>(s.sampleRate) * 8u) : 0;
    i.total = i.first + i.again;
    i.head = wavHeader(static_cast<int>(i.p.ch.size()), s.sampleRate, i.total);
    i.fresh = i.p.ch;
    return w;
}

std::uint64_t WavStream::size() const {
    return m->head.size() + std::uint64_t(m->total) * m->p.ch.size() * 2;
}

double WavStream::seconds() const {
    return m->p.info.sampleRate ? static_cast<double>(m->total) / m->p.info.sampleRate : 0.0;
}

size_t WavStream::read(std::uint64_t off, void* buffer, size_t n) {
    Impl& i = *m;
    auto* out = static_cast<std::uint8_t*>(buffer);
    size_t done = 0;
    while (done < n && off < size()) {
        if (off < i.head.size()) {
            const size_t k = std::min<size_t>(n - done, static_cast<size_t>(i.head.size() - off));
            std::memcpy(out + done, i.head.data() + off, k);
            done += k;
            off += k;
            continue;
        }
        const std::uint64_t d = off - i.head.size();
        if (d < i.bufStart) i.restart(); // (back: decoded again from the start)
        while (d >= i.bufStart + i.buf.size())
            if (!i.decodeNext()) return done;
        const size_t at = static_cast<size_t>(d - i.bufStart);
        const size_t k = std::min(n - done, i.buf.size() - at);
        std::memcpy(out + done, i.buf.data() + at, k);
        done += k;
        off += k;
    }
    return done;
}

bool isStream(const std::vector<std::uint8_t>& file) {
    const std::string m = magic(file);
    return m == "FSTM" || m == "BWAV";
}

bool convert(const std::vector<std::uint8_t>& file, const std::string& outPath, std::string& why) {
    std::unique_ptr<WavStream> w = WavStream::open(file, why);
    if (!w) return false;
    const std::string tmp = outPath + ".part";
    std::FILE* f = std::fopen(tmp.c_str(), "wb");
    if (!f) {
        why = "can't write to the SD card";
        return false;
    }
    std::vector<std::uint8_t> piece(64 * 1024);
    bool ok = true;
    for (std::uint64_t off = 0; ok && off < w->size();) {
        const size_t got = w->read(off, piece.data(), piece.size());
        if (!got) break;
        ok = std::fwrite(piece.data(), 1, got, f) == got;
        off += got;
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

// ---------------------------------------------------------------- SARC

bool sarcFiles(const std::vector<std::uint8_t>& d, std::vector<SarcFile>& out) {
    if (d.size() < 0x20 || std::memcmp(d.data(), "SARC", 4) != 0) return false;
    const bool big = d[6] == 0xFE && d[7] == 0xFF;
    auto u16 = [&](size_t o) -> std::uint32_t { return o + 2 > d.size() ? 0 : big ? (d[o] << 8 | d[o + 1]) : (d[o] | d[o + 1] << 8); };
    auto u32 = [&](size_t o) -> std::uint32_t {
        if (o + 4 > d.size()) return 0;
        return big ? (std::uint32_t(d[o]) << 24 | std::uint32_t(d[o + 1]) << 16 | std::uint32_t(d[o + 2]) << 8 | d[o + 3]) : le32(&d[o]);
    };
    const std::uint32_t dataStart = u32(0xC);
    const size_t sfat = u16(4);
    if (sfat + 0xC > d.size() || std::memcmp(&d[sfat], "SFAT", 4) != 0) return false;
    const std::uint32_t count = u16(sfat + 6);
    const size_t nodes = sfat + u16(sfat + 4), sfnt = nodes + size_t(count) * 16;
    if (sfnt + 8 > d.size() || std::memcmp(&d[sfnt], "SFNT", 4) != 0) return false;
    const size_t names = sfnt + u16(sfnt + 4);
    for (std::uint32_t i = 0; i < count; ++i) {
        const size_t n = nodes + size_t(i) * 16;
        const std::uint32_t attr = u32(n + 4), start = u32(n + 8), end = u32(n + 12);
        SarcFile f;
        if (attr >> 24) {
            const size_t at = names + size_t(attr & 0xFFFF) * 4;
            for (size_t k = at; k < d.size() && d[k]; ++k) f.name.push_back(static_cast<char>(d[k]));
        }
        f.offset = size_t(dataStart) + start;
        f.size = end >= start ? end - start : 0;
        if (f.offset + f.size > d.size()) return false;
        out.push_back(std::move(f));
    }
    return true;
}

// ---------------------------------------------------------------- compressed sections

bool lz4Block(const std::uint8_t* src, size_t srcSize, std::uint8_t* dst, size_t dstSize) {
    size_t i = 0, o = 0;
    while (i < srcSize) {
        const std::uint8_t token = src[i++];
        size_t lit = token >> 4;
        if (lit == 15)
            for (std::uint8_t b = 255; b == 255 && i < srcSize;) { b = src[i++]; lit += b; }
        if (lit > srcSize - i || lit > dstSize - o) return false;
        std::memcpy(dst + o, src + i, lit);
        i += lit;
        o += lit;
        if (i >= srcSize) break; // (the last sequence: literals only)
        if (srcSize - i < 2) return false;
        const size_t back = size_t(src[i]) | size_t(src[i + 1]) << 8;
        i += 2;
        if (!back || back > o) return false;
        size_t len = (token & 0xF) + 4;
        if ((token & 0xF) == 15)
            for (std::uint8_t b = 255; b == 255 && i < srcSize;) { b = src[i++]; len += b; }
        if (len > dstSize - o) return false;
        for (size_t k = 0; k < len; ++k, ++o) dst[o] = dst[o - back]; // (may overlap: byte by byte)
    }
    return o == dstSize;
}

// The bucket tree: a 0x4000 node (header: index, count, end; then each set's
// start), then the entry sets, 0x4000 each (header: index, count, end; then
// the entries). With more sets than one node holds, a second level of nodes
// comes first: they're skipped (the sets are where they always are, last).
bool parseCompressionTable(const std::vector<std::uint8_t>& t, std::uint32_t entries, std::vector<CompressedPiece>& out,
                           std::string& why) {
    constexpr size_t kNode = 0x4000, kEntry = 0x18;
    constexpr size_t perSet = (kNode - 0x10) / kEntry, perNode = (kNode - 0x10) / 8;
    const size_t sets = (entries + perSet - 1) / perSet;
    const size_t l2 = sets > perNode ? (sets - perNode + perNode - 1) / perNode : 0;
    const size_t first = (1 + l2) * kNode;
    if (t.size() < first + sets * kNode) { why = "its compression table is short"; return false; }
    out.clear();
    out.reserve(entries);
    for (size_t s = 0; s < sets; ++s) {
        const std::uint8_t* set = t.data() + first + s * kNode;
        const std::uint32_t count = le32(set + 4);
        if (count > perSet) { why = "its compression table looks wrong"; return false; }
        for (std::uint32_t e = 0; e < count; ++e) {
            const std::uint8_t* p = set + 0x10 + e * kEntry;
            CompressedPiece c;
            c.virt = le64(p);
            c.phys = le64(p + 8);
            c.kind = p[0x10];
            c.physSize = le32(p + 0x14);
            out.push_back(c);
        }
    }
    for (size_t i = 1; i < out.size(); ++i)
        if (out[i].virt < out[i - 1].virt) { why = "its compression table is out of order"; return false; }
    return !out.empty();
}

CompressedStorage::CompressedStorage(std::vector<CompressedPiece> pieces, std::uint64_t end, ReadAt phys)
    : m_pieces(std::move(pieces)), m_end(end), m_phys(std::move(phys)) {}

bool CompressedStorage::read(std::uint64_t off, void* buffer, size_t n) {
    auto* out = static_cast<std::uint8_t*>(buffer);
    while (n) {
        // The piece holding `off`: the last one starting at or before it.
        auto it = std::upper_bound(m_pieces.begin(), m_pieces.end(), off,
                                   [](std::uint64_t v, const CompressedPiece& c) { return v < c.virt; });
        if (it == m_pieces.begin()) return false;
        --it;
        const size_t index = static_cast<size_t>(it - m_pieces.begin());
        const std::uint64_t pieceEnd = index + 1 < m_pieces.size() ? m_pieces[index + 1].virt : m_end;
        if (off >= pieceEnd) return false;
        const std::uint64_t within = off - it->virt;
        const size_t k = static_cast<size_t>(std::min<std::uint64_t>(n, pieceEnd - off));
        if (it->kind == 0) {
            if (!m_phys(it->phys + within, out, k)) return false;
        } else if (it->kind == 1) {
            std::memset(out, 0, k);
        } else if (it->kind == 3) {
            if (m_cached != index) {
                const std::uint64_t size = pieceEnd - it->virt;
                if (size > (64u << 20) || it->physSize > (64u << 20)) return false;
                m_packed.resize(it->physSize);
                m_cache.resize(static_cast<size_t>(size));
                if (!m_phys(it->phys, m_packed.data(), m_packed.size())) return false;
                if (!lz4Block(m_packed.data(), m_packed.size(), m_cache.data(), m_cache.size())) return false;
                m_cached = index;
            }
            std::memcpy(out, m_cache.data() + within, k);
        } else {
            return false;
        }
        out += k;
        off += k;
        n -= k;
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

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

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
    // Every title*.keys (title.keys, and e.g. title_emummc.keys beside it: a
    // dump from each system), in /switch and the SD card's top.
    int titleFiles = 0;
    for (const char* dir : {"sdmc:/switch", "sdmc:/"}) {
        std::error_code ec;
        for (const auto& e : std::filesystem::directory_iterator(dir, ec)) {
            const std::string name = lower(e.path().filename().string());
            if (name.rfind("title", 0) != 0 || name.size() < 5 || name.compare(name.size() - 5, 5, ".keys") != 0) continue;
            ++titleFiles;
            readKeyFile(e.path().string().c_str(), [&k](const std::string& n, const std::string& value) {
                Key key;
                if (n.size() == 32 && fromHex(value, key.data(), 16)) k.title[n] = key;
            });
        }
    }
    logLine(std::string("keys: ") + prod + (k.hasHeader ? "" : " (no header_key)") + ", " +
            std::to_string(k.areaKey[0].size()) + " key area keys, " + std::to_string(k.titlekek.size()) + " titlekeks, " +
            std::to_string(k.title.size()) + " title keys (" + std::to_string(titleFiles) + " files)");
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

    std::unique_ptr<CompressedStorage> compressed; // (newer games: the section's pieces packed)

    // From the RomFS's start: decrypted, and unpacked where it's compressed.
    bool read(std::uint64_t off, void* buf, size_t n) {
        if (compressed) return compressed->read(romfsStart + off, buf, n);
        return readSection(romfsStart + off, buf, n);
    }

    // From the section's start, decrypted.
    bool readSection(std::uint64_t off, void* buf, size_t n) {
        std::uint64_t at = sectionStart + off;
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

// The console's common ticket for a rights ID (es: GetCommonTicketData), its
// title key still sealed. Personalized tickets (bought on this console) hold
// it RSA-encrypted: not read here (title.keys has those).
bool commonTicketKey(const std::uint8_t* rightsId, Key& sealed) {
    Service es;
    if (R_FAILED(smGetService(&es, "es"))) return false;
    struct { std::uint8_t id[16]; } in;
    std::memcpy(in.id, rightsId, 16);
    std::vector<std::uint8_t> ticket(0x800);
    std::uint64_t size = 0;
    const Result rc = serviceDispatchInOut(&es, 16, in, size,
        .buffer_attrs = {SfBufferAttr_HipcMapAlias | SfBufferAttr_Out},
        .buffers = {{ticket.data(), ticket.size()}});
    serviceClose(&es);
    if (R_FAILED(rc)) {
        logLine("no common ticket on the console (rc=0x" + hex(rc) + ")");
        return false;
    }
    size_t body;
    switch (le32(ticket.data())) {
        case 0x10000: case 0x10001: body = 0x240; break;   // RSA-4096
        case 0x10003: case 0x10004: body = 0x140; break;   // RSA-2048
        case 0x10002: case 0x10005: body = 0x80; break;    // ECDSA
        default: return false;
    }
    if (body + 0x180 > ticket.size() || ticket[body + 0x141] != 0) return false; // (0: common)
    std::memcpy(sealed.data(), ticket.data() + body + 0x40, 16);
    return true;
}

bool looksLikeRomfs(Nca& nca) {
    std::uint8_t h[0x50];
    return nca.read(0, h, sizeof(h)) && le64(h) == 0x50 && le64(h + 0x48) >= 0x50;
}

// Finds the game's program content, decrypts its header, picks its RomFS
// section and that section's key. Each step logged.
bool openGame(std::uint64_t appId, const Keys& keys, Nca& nca, std::string& why) {
    if (R_FAILED(ncmInitialize())) { why = "ncm"; return false; }
    bool found = false;
    std::vector<std::uint8_t> h(0xC00);
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
        if (!nca.readRaw(0, h.data(), h.size())) { // (listed, but not readable there: the next place)
            logLine("program content on storage " + std::to_string(static_cast<int>(sid)) + ": couldn't be read");
            ncmContentStorageClose(&nca.cs);
            nca.open = false;
            continue;
        }
        found = true;
        logLine("program content on storage " + std::to_string(static_cast<int>(sid)));
        break;
    }
    ncmExit();
    if (!found) { why = "its files weren't found, or couldn't be read"; return false; }
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
    const std::uint64_t romfsEnd = nca.romfsStart + le64(fs + 8 + 0x10 + (layers - 2) * 0x18 + 8);
    std::memcpy(nca.ctrHigh, fs + 0x140, 8);
    // Newer games: the section packed in pieces (LZ4), a table says where.
    const std::uint64_t packOff = le64(fs + 0x178), packSize = le64(fs + 0x180);
    const bool packed = packSize != 0 && std::memcmp(fs + 0x188, "BKTR", 4) == 0;
    const std::uint32_t packEntries = packed ? le32(fs + 0x188 + 8) : 0;
    if (le64(fs + 0x148 + 8) != 0) logLine("section is sparse (not read yet)");
    if (packed) logLine("section is compressed: " + std::to_string(packEntries) + " pieces");
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
        const auto kek = keys.titlekek.find(keyIndex);
        auto unwrap = [&](const Key& sealed) { // (a title key as tickets keep it: sealed with the titlekek)
            if (kek == keys.titlekek.end()) return;
            Aes128Context ecb;
            aes128ContextCreate(&ecb, kek->second.data(), false);
            Key key;
            aes128DecryptBlock(&ecb, key.data(), sealed.data());
            tries.push_back(key);
        };
        const auto t = keys.title.find(rid);
        if (t != keys.title.end()) {
            tries.push_back(t->second); // (title.keys: already unsealed)
            unwrap(t->second);
        }
        // The console's own ticket for it (installed games: a common ticket).
        Key sealed;
        if (commonTicketKey(h.data() + 0x230, sealed)) {
            logLine("title key from the console's ticket");
            unwrap(sealed);
        }
        if (tries.empty()) {
            why = "its title key isn't in title.keys (run Lockpick_RCM on the system the game is on: sysMMC or emuMMC)";
            logLine(std::string("no title key for rights ID ") + rid);
            return false;
        }
    }
    std::string packWhy;
    std::vector<std::uint8_t> table;
    for (const Key& k : tries) {
        nca.key = k;
        nca.compressed.reset();
        if (packed) {
            std::vector<CompressedPiece> pieces;
            table.resize(static_cast<size_t>(std::min<std::uint64_t>(packSize, 64u << 20)));
            if (!nca.readSection(packOff, table.data(), table.size()) ||
                !parseCompressionTable(table, packEntries, pieces, packWhy))
                continue; // (the wrong key reads garbage)
            Nca* self = &nca;
            nca.compressed = std::make_unique<CompressedStorage>(
                std::move(pieces), romfsEnd,
                [self](std::uint64_t off, void* buf, size_t n) { return self->readSection(off, buf, n); });
        }
        if (nca.encryption == 1 || looksLikeRomfs(nca)) {
            logLine("RomFS opened (section " + std::to_string(section) + ")");
            return true;
        }
    }
    if (packed && !packWhy.empty()) logLine("compression: " + packWhy);
    why = "its files couldn't be decrypted (keys too old? run Lockpick_RCM again)";
    logLine("RomFS: no key gave a RomFS");
    return false;
}

std::string safeName(std::string s) {
    for (char& c : s)
        if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') c = ' ';
    while (!s.empty() && (s.back() == ' ' || s.back() == '.')) s.pop_back();
    if (s.size() > 60) s.resize(60);
    return s.empty() ? std::string("Game") : s;
}

} // namespace

struct GameFiles::Impl {
    std::unique_ptr<Nca> nca;
    std::mutex mu; // (one read at a time: a song heard while others copy)
    std::vector<std::unique_ptr<zstd::Dictionary>> dicts; // (the game's own, for its .zs files)
};

GameFiles::GameFiles() : m(std::make_unique<Impl>()) {}
GameFiles::~GameFiles() = default;

std::unique_ptr<GameFiles> GameFiles::open(std::uint64_t appId, const std::string& gameName, std::string& why) {
    try {
        logLine("---- " + gameName + " (" + hex(appId) + ")");
        auto keys = std::make_unique<Keys>();
        if (!loadKeys(*keys, why)) { logLine("stopped: " + why); return nullptr; }
        std::unique_ptr<GameFiles> g(new GameFiles());
        g->m->nca = std::make_unique<Nca>();
        if (!openGame(appId, *keys, *g->m->nca, why)) { logLine("stopped: " + why); return nullptr; }
        keys.reset();
        std::vector<RomfsFile> all;
        Nca* nca = g->m->nca.get();
        if (!listRomfs([nca](std::uint64_t off, void* buf, size_t n) { return nca->read(off, buf, n); }, all, why)) {
            logLine("stopped: " + why);
            return nullptr;
        }
        g->fileCount = static_cast<int>(all.size());
        for (RomfsFile& f : all) {
            const std::string name = lower(f.path);
            const auto ends = [&name](const char* e) {
                const size_t n = std::strlen(e);
                return name.size() >= n && name.compare(name.size() - n, n, e) == 0;
            };
            const bool zs = ends(".bfstm.zs") || ends(".bwav.zs");
            if (zs || ends(".bfstm") || ends(".bwav")) {
                Song s;
                std::filesystem::path stem = std::filesystem::path(f.path).stem(); // (x.bwav.zs: x.bwav)
                if (zs) stem = stem.stem();
                s.name = safeName(stem.string());
                s.ext = (ends(".bfstm") || ends(".bfstm.zs")) ? ".bfstm" : ".bwav";
                s.path = f.path;
                s.offset = f.offset;
                s.size = f.size;
                s.zs = zs;
                g->songs.push_back(std::move(s));
            } else {
                for (const char* e : {".bars", ".bars.zs", ".wem", ".bnk", ".bank", ".acb", ".awb", ".hca",
                                      ".opus", ".lopus", ".pak", ".cpk", ".utoc", ".ucas"})
                    if (ends(e)) {
                        ++g->skipped[e];
                        break;
                    }
            }
        }
        // Compressed songs (.zs): the game's zstd dictionaries, from
        // Pack/ZsDic.pack.zs (itself compressed without one) where it has them.
        if (std::any_of(g->songs.begin(), g->songs.end(), [](const Song& x) { return x.zs; })) {
            for (const RomfsFile& f : all) {
                if (lower(std::filesystem::path(f.path).filename().string()) != "zsdic.pack.zs" || f.size > (64u << 20)) continue;
                std::vector<std::uint8_t> packed(static_cast<size_t>(f.size)), pack;
                std::string zwhy;
                if (!nca->read(f.offset, packed.data(), packed.size()) || !zstd::decompress(packed.data(), packed.size(), pack, {}, zwhy)) {
                    logLine("zstd dictionaries: " + f.path + " couldn't be read (" + zwhy + ")");
                    break;
                }
                std::vector<SarcFile> files;
                if (!sarcFiles(pack, files)) { logLine("zstd dictionaries: not a SARC"); break; }
                for (const SarcFile& sf : files) {
                    auto dict = std::make_unique<zstd::Dictionary>();
                    if (zstd::loadDictionary(pack.data() + sf.offset, sf.size, *dict, zwhy)) {
                        logLine("zstd dictionary " + sf.name + " (ID " + std::to_string(dict->id) + ")");
                        g->m->dicts.push_back(std::move(dict));
                    }
                }
                break;
            }
        }
        // A game with hundreds: its sound effects among them. Under ~10 seconds
        // (512 KB of DSP-ADPCM stereo, a quarter of that compressed) left out.
        size_t shortOnes = 0;
        if (g->songs.size() > 300) {
            const size_t before = g->songs.size();
            g->songs.erase(std::remove_if(g->songs.begin(), g->songs.end(),
                                          [](const Song& x) { return x.size < (x.zs ? 128u : 512u) * 1024u; }),
                           g->songs.end());
            shortOnes = before - g->songs.size();
        }
        std::sort(g->songs.begin(), g->songs.end(), [](const Song& a, const Song& b) { return a.path < b.path; });
        if (shortOnes) logLine("left out " + std::to_string(shortOnes) + " short sounds");
        if (g->songs.empty()) { // (what it does have: the next formats to read)
            std::map<std::string, std::pair<int, std::uint64_t>> kinds;
            for (const RomfsFile& f : all) {
                std::string ext = lower(std::filesystem::path(f.path).extension().string());
                auto& k = kinds[ext.empty() ? "(none)" : ext];
                ++k.first;
                k.second += f.size;
            }
            std::vector<std::pair<std::string, std::pair<int, std::uint64_t>>> big(kinds.begin(), kinds.end());
            std::sort(big.begin(), big.end(), [](const auto& a, const auto& b) { return a.second.second > b.second.second; });
            std::string line;
            for (size_t i = 0; i < big.size() && i < 8; ++i)
                line += " " + big[i].first + " x" + std::to_string(big[i].second.first) + " (" +
                        std::to_string(big[i].second.second >> 20) + " MB)";
            logLine("its biggest kinds of file:" + line);
        }
        // Same names in two folders: numbered.
        std::set<std::string> used;
        for (Song& s : g->songs) {
            const std::string base = s.name;
            for (int n = 2; !used.insert(lower(s.name)).second; ++n) s.name = base + " " + std::to_string(n);
        }
        std::string other;
        for (const auto& [ext, n] : g->skipped) other += " " + ext + " x" + std::to_string(n);
        logLine("files: " + std::to_string(g->fileCount) + ", songs " + std::to_string(g->songs.size()) +
                (other.empty() ? "" : ", not read yet:" + other));
        return g;
    } catch (const std::exception& e) {
        why = std::string("it ran out of memory (") + e.what() + ")";
        logLine("stopped: " + why);
        return nullptr;
    }
}

bool GameFiles::read(const Song& s, std::vector<std::uint8_t>& out, std::string& why) {
    if (s.size > 48u * 1024u * 1024u) { why = "too big"; return false; }
    try {
        out.resize(static_cast<size_t>(s.size));
    } catch (const std::bad_alloc&) {
        why = "no memory for it just now";
        return false;
    }
    std::lock_guard<std::mutex> lk(m->mu);
    if (!m->nca->read(s.offset, out.data(), out.size())) { why = "it couldn't be read"; return false; }
    if (!s.zs) return true;
    // Compressed: with the dictionary its frame names, else (naming none) on
    // its own, then each of the game's.
    std::vector<std::uint8_t> plain;
    std::vector<const zstd::Dictionary*> all;
    for (const auto& d : m->dicts) all.push_back(d.get());
    const std::uint32_t id = zstd::frameDictionary(out.data(), out.size());
    bool ok = false;
    try {
        if (id) {
            ok = zstd::decompress(out.data(), out.size(), plain, all, why, 96u << 20);
        } else {
            ok = zstd::decompress(out.data(), out.size(), plain, {}, why, 96u << 20);
            for (size_t i = 0; !ok && i < all.size(); ++i) {
                plain.clear();
                ok = zstd::decompress(out.data(), out.size(), plain, {all[i]}, why, 96u << 20);
            }
        }
    } catch (const std::bad_alloc&) {
        why = "no memory for it just now";
        return false;
    }
    if (!ok) { why = "it couldn't be decompressed (" + why + ")"; return false; }
    out = std::move(plain);
    return true;
}

namespace {

// Written with FS itself (not stdio): sdmc:/music/... -> /music/...
bool writeSd(const std::string& path, const std::vector<std::uint8_t>& data, std::string& why) {
    FsFileSystem* fs = fsdevGetDeviceFileSystem("sdmc");
    if (!fs) { why = "the SD card isn't there"; return false; }
    const std::string p = path.rfind("sdmc:", 0) == 0 ? path.substr(5) : path;
    const std::string tmp = p + ".part";
    char name[FS_MAX_PATH];
    std::snprintf(name, sizeof(name), "%s", tmp.c_str());
    fsFsDeleteFile(fs, name);
    if (R_FAILED(fsFsCreateFile(fs, name, static_cast<s64>(data.size()), 0))) { why = "the SD card is full, or couldn't be written"; return false; }
    FsFile f;
    if (R_FAILED(fsFsOpenFile(fs, name, FsOpenMode_Write, &f))) { why = "the SD card couldn't be written"; return false; }
    bool ok = true;
    for (size_t at = 0; ok && at < data.size();) {
        const size_t n = std::min<size_t>(data.size() - at, 1u << 20);
        ok = R_SUCCEEDED(fsFileWrite(&f, static_cast<s64>(at), data.data() + at, n, FsWriteOption_None));
        at += n;
    }
    if (ok) ok = R_SUCCEEDED(fsFileFlush(&f));
    fsFileClose(&f);
    if (!ok) {
        fsFsDeleteFile(fs, name);
        why = "the SD card is full, or couldn't be written";
        return false;
    }
    char final[FS_MAX_PATH];
    std::snprintf(final, sizeof(final), "%s", p.c_str());
    fsFsDeleteFile(fs, final);
    if (R_FAILED(fsFsRenameFile(fs, name, final))) { why = "the SD card couldn't be written"; return false; }
    return true;
}

void makeDir(const std::string& path) {
    FsFileSystem* fs = fsdevGetDeviceFileSystem("sdmc");
    if (!fs) return;
    const std::string p = path.rfind("sdmc:", 0) == 0 ? path.substr(5) : path;
    for (size_t i = 1; i <= p.size(); ++i)
        if (i == p.size() || p[i] == '/') {
            char d[FS_MAX_PATH];
            std::snprintf(d, sizeof(d), "%s", p.substr(0, i).c_str());
            fsFsCreateDirectory(fs, d); // (there already: fine)
        }
}

} // namespace

std::string songFolder(const std::string& gameName) { return "sdmc:/music/" + safeName(gameName); }

// The chosen songs, as the game has them (.bfstm / .bwav: HOME plays them as
// they are), under the names chosen.
void copySongs(GameFiles& g, const std::string& gameName, Progress& p) {
    try {
        const std::string folder = songFolder(gameName);
        {
            std::lock_guard<std::mutex> lk(p.mu);
            p.folder = folder;
        }
        std::uint64_t need = 0;
        for (const Song& s : g.songs)
            if (s.chosen) {
                ++p.found;
                need += s.size;
            }
        if (FsFileSystem* fs = fsdevGetDeviceFileSystem("sdmc")) {
            s64 free = 0;
            if (R_SUCCEEDED(fsFsGetFreeSpace(fs, "/", &free)) && static_cast<std::uint64_t>(free) < need + (64u << 20)) {
                std::lock_guard<std::mutex> lk(p.mu);
                p.error = "the SD card hasn't room (" + std::to_string(need >> 20) + " MB needed, " +
                          std::to_string(static_cast<std::uint64_t>(free) >> 20) + " MB free)";
                logLine("stopped: " + p.error);
                p.finished = true;
                return;
            }
        }
        makeDir(folder);
        std::error_code ec;
        for (const Song& s : g.songs) {
            if (p.cancel) break;
            if (!s.chosen) continue;
            const std::string out = folder + "/" + safeName(s.name) + s.ext;
            if (std::filesystem::exists(out, ec)) { // (copied before)
                ++p.done;
                std::lock_guard<std::mutex> lk(p.mu);
                p.files.push_back(out);
                continue;
            }
            std::vector<std::uint8_t> data;
            std::string why;
            if (!g.read(s, data, why)) {
                ++p.failed;
                logLine("  " + s.path + ": " + why);
                continue;
            }
            if (!isStream(data)) {
                ++p.failed;
                logLine("  " + s.path + ": not a stream it can play");
                continue;
            }
            if (!writeSd(out, data, why)) {
                ++p.failed;
                logLine("  " + s.path + ": " + why);
                std::lock_guard<std::mutex> lk(p.mu);
                p.error = why;
                break;
            }
            ++p.done;
            std::lock_guard<std::mutex> lk(p.mu);
            p.files.push_back(out);
        }
        logLine("done: " + std::to_string(p.done.load()) + " copied, " + std::to_string(p.failed.load()) + " failed");
    } catch (const std::exception& e) {
        std::lock_guard<std::mutex> lk(p.mu);
        p.error = std::string("it ran out of memory (") + e.what() + ")";
        logLine("stopped: " + p.error);
    }
    p.finished = true;
}

#else

struct GameFiles::Impl {};
GameFiles::GameFiles() : m(std::make_unique<Impl>()) {}
GameFiles::~GameFiles() = default;
std::unique_ptr<GameFiles> GameFiles::open(std::uint64_t, const std::string&, std::string& why) {
    why = "only on the Switch";
    return nullptr;
}
bool GameFiles::read(const Song&, std::vector<std::uint8_t>&, std::string& why) {
    why = "only on the Switch";
    return false;
}
std::string songFolder(const std::string& gameName) { return "sdmc:/music/" + gameName; }
void copySongs(GameFiles&, const std::string&, Progress& p) {
    std::lock_guard<std::mutex> lk(p.mu);
    p.error = "only on the Switch";
    p.finished = true;
}

#endif

} // namespace gamemusic
