// Zstd - see Zstd.hpp. Follows RFC 8878's names and order throughout.
#include "Zstd.hpp"

#include <algorithm>
#include <array>
#include <cstring>

namespace zstd {

namespace {

// ---------------------------------------------------------------- bits

std::uint32_t le32(const std::uint8_t* p) {
    return std::uint32_t(p[0]) | std::uint32_t(p[1]) << 8 | std::uint32_t(p[2]) << 16 | std::uint32_t(p[3]) << 24;
}

int highBit(std::uint32_t v) { // (v > 0)
    int n = -1;
    while (v) { v >>= 1; ++n; }
    return n;
}

// Forward: FSE table descriptions. Bits from the first byte's lowest up.
struct ForwardBits {
    const std::uint8_t* d;
    std::size_t size;
    std::size_t bit = 0;
    std::uint32_t peek(int n) const {
        std::uint32_t v = 0;
        for (int i = 0; i < n; ++i) {
            const std::size_t b = bit + static_cast<std::size_t>(i);
            if (b / 8 < size && (d[b / 8] >> (b % 8)) & 1) v |= 1u << i;
        }
        return v;
    }
    void skip(int n) { bit += static_cast<std::size_t>(n); }
    std::uint32_t read(int n) { const std::uint32_t v = peek(n); skip(n); return v; }
    std::size_t bytes() const { return (bit + 7) / 8; }
};

// Backward: Huffman and FSE streams. Read from the end; the last byte's
// highest set bit marks where the stream starts. Past the beginning: zeros.
struct BackBits {
    const std::uint8_t* d = nullptr;
    std::size_t size = 0;
    std::int64_t pos = 0; // bits left above the start
    bool init(const std::uint8_t* data, std::size_t n) {
        d = data;
        size = n;
        if (!n || !data[n - 1]) return false;
        pos = static_cast<std::int64_t>(n) * 8 - 8 + highBit(data[n - 1]);
        return true;
    }
    // The n bits below pos (n <= 32), lowest first; past the start: zeros.
    std::uint64_t peek(int n) const {
        if (n <= 0) return 0;
        std::int64_t start = pos - n;
        int pad = 0;
        if (start < 0) { pad = static_cast<int>(-start); start = 0; } // (the missing low bits: zeros)
        const std::size_t byte = static_cast<std::size_t>(start >> 3);
        std::uint64_t v = 0;
        if (byte < size) {
            const std::size_t avail = std::min<std::size_t>(8, size - byte);
            for (std::size_t i = 0; i < avail; ++i) v |= std::uint64_t(d[byte + i]) << (8 * i);
        }
        v >>= (start & 7);
        const int take = n - pad;
        v &= take >= 64 ? ~std::uint64_t(0) : ((std::uint64_t(1) << take) - 1);
        return v << pad;
    }
    std::uint64_t read(int n) {
        const std::uint64_t v = peek(n);
        pos -= n;
        return v;
    }
    bool overflowed() const { return pos < 0; }
    bool done() const { return pos == 0; }
};

// ---------------------------------------------------------------- FSE

struct FseEntry {
    std::uint16_t symbol = 0;
    std::uint8_t bits = 0;
    std::uint16_t base = 0;
};
struct Fse {
    int log = 0;
    std::vector<FseEntry> table;
};

bool buildFse(const std::vector<int>& probs, int log, Fse& out) {
    const std::uint32_t size = 1u << log;
    out.log = log;
    out.table.assign(size, {});
    std::vector<std::uint32_t> next(probs.size());
    std::int64_t high = static_cast<std::int64_t>(size) - 1;
    for (std::size_t s = 0; s < probs.size(); ++s)
        if (probs[s] == -1) {
            if (high < 0) return false;
            out.table[static_cast<std::size_t>(high--)].symbol = static_cast<std::uint16_t>(s);
            next[s] = 1;
        } else {
            next[s] = static_cast<std::uint32_t>(probs[s]);
        }
    const std::uint32_t step = (size >> 1) + (size >> 3) + 3, mask = size - 1;
    std::uint32_t pos = 0;
    for (std::size_t s = 0; s < probs.size(); ++s)
        for (int i = 0; i < probs[s]; ++i) {
            out.table[pos].symbol = static_cast<std::uint16_t>(s);
            do pos = (pos + step) & mask;
            while (static_cast<std::int64_t>(pos) > high);
        }
    if (pos != 0) return false; // (the probabilities didn't add up)
    for (std::uint32_t u = 0; u < size; ++u) {
        FseEntry& e = out.table[u];
        const std::uint32_t n = next[e.symbol]++;
        e.bits = static_cast<std::uint8_t>(log - highBit(n));
        e.base = static_cast<std::uint16_t>((n << e.bits) - size);
    }
    return true;
}

// A table description: its accuracy log and each symbol's probability.
bool readFse(const std::uint8_t* d, std::size_t size, int maxLog, int maxSymbol, Fse& out, std::size_t& used) {
    ForwardBits b{d, size};
    const int log = static_cast<int>(b.read(4)) + 5;
    if (log > maxLog) return false;
    std::vector<int> probs;
    int remaining = (1 << log) + 1, threshold = 1 << log, nbBits = log + 1;
    bool previous0 = false;
    while (remaining > 1 && static_cast<int>(probs.size()) <= maxSymbol) {
        if (previous0) {
            int zeros = 0;
            for (;;) {
                const int r = static_cast<int>(b.read(2));
                zeros += r;
                if (r != 3) break;
            }
            for (int i = 0; i < zeros; ++i) probs.push_back(0);
            if (static_cast<int>(probs.size()) > maxSymbol) break;
        }
        const int max = (2 * threshold - 1) - remaining;
        int count;
        const int low = static_cast<int>(b.peek(nbBits - 1));
        if (low < max) {
            count = low;
            b.skip(nbBits - 1);
        } else {
            count = static_cast<int>(b.peek(nbBits));
            if (count >= threshold) count -= max;
            b.skip(nbBits);
        }
        --count; // (-1: "less than one")
        remaining -= count < 0 ? -count : count;
        probs.push_back(count);
        previous0 = count == 0;
        while (remaining < threshold) {
            --nbBits;
            threshold >>= 1;
        }
    }
    if (remaining != 1 || b.bytes() > size || static_cast<int>(probs.size()) > maxSymbol + 1) return false;
    used = b.bytes();
    return buildFse(probs, log, out);
}

void rleFse(int symbol, Fse& out) {
    out.log = 0;
    out.table.assign(1, {});
    out.table[0].symbol = static_cast<std::uint16_t>(symbol);
}

// ---------------------------------------------------------------- Huffman

struct Huffman {
    int maxBits = 0;
    std::vector<std::uint8_t> symbol, bits; // (1 << maxBits entries)
    bool ready = false;
};

bool buildHuffman(std::vector<std::uint8_t> weights, Huffman& h) {
    if (weights.empty() || weights.size() > 255) return false;
    std::uint32_t sum = 0;
    for (std::uint8_t w : weights) {
        if (w > 11) return false;
        if (w) sum += 1u << (w - 1);
    }
    if (!sum) return false;
    const int maxBits = highBit(sum) + 1;
    const std::uint32_t total = 1u << maxBits, left = total - sum;
    if (left & (left - 1)) return false; // (the last weight's share must be a power of two)
    weights.push_back(static_cast<std::uint8_t>(highBit(left) + 1));
    h.maxBits = maxBits;
    h.symbol.assign(total, 0);
    h.bits.assign(total, 0);
    std::uint32_t pos = 0;
    for (int w = 1; w <= maxBits; ++w)
        for (std::size_t s = 0; s < weights.size(); ++s)
            if (weights[s] == w) {
                const std::uint32_t n = 1u << (w - 1);
                if (pos + n > total) return false;
                for (std::uint32_t i = 0; i < n; ++i) {
                    h.symbol[pos + i] = static_cast<std::uint8_t>(s);
                    h.bits[pos + i] = static_cast<std::uint8_t>(maxBits + 1 - w);
                }
                pos += n;
            }
    h.ready = pos == total;
    return h.ready;
}

// The tree description; `used`: its size.
bool readHuffman(const std::uint8_t* d, std::size_t size, Huffman& h, std::size_t& used) {
    if (!size) return false;
    const std::uint8_t head = d[0];
    std::vector<std::uint8_t> weights;
    if (head >= 128) { // direct: 4 bits each
        const std::size_t n = head - 127u, bytes = (n + 1) / 2;
        if (1 + bytes > size) return false;
        for (std::size_t i = 0; i < n; ++i) weights.push_back(i % 2 == 0 ? d[1 + i / 2] >> 4 : d[1 + i / 2] & 15);
        used = 1 + bytes;
    } else { // FSE-compressed weights
        const std::size_t n = head;
        if (1 + n > size) return false;
        Fse fse;
        std::size_t tableBytes = 0;
        if (!readFse(d + 1, n, 6, 255, fse, tableBytes) || tableBytes >= n) return false;
        BackBits b;
        if (!b.init(d + 1 + tableBytes, n - tableBytes)) return false;
        std::uint32_t s1 = static_cast<std::uint32_t>(b.read(fse.log)), s2 = static_cast<std::uint32_t>(b.read(fse.log));
        for (;;) {
            if (weights.size() >= 255) return false;
            weights.push_back(static_cast<std::uint8_t>(fse.table[s1].symbol));
            s1 = fse.table[s1].base + static_cast<std::uint32_t>(b.read(fse.table[s1].bits));
            if (b.overflowed()) { weights.push_back(static_cast<std::uint8_t>(fse.table[s2].symbol)); break; }
            if (weights.size() >= 255) return false;
            weights.push_back(static_cast<std::uint8_t>(fse.table[s2].symbol));
            s2 = fse.table[s2].base + static_cast<std::uint32_t>(b.read(fse.table[s2].bits));
            if (b.overflowed()) { weights.push_back(static_cast<std::uint8_t>(fse.table[s1].symbol)); break; }
        }
        used = 1 + n;
    }
    return buildHuffman(std::move(weights), h);
}

bool huffmanStream(const Huffman& h, const std::uint8_t* d, std::size_t size, std::uint8_t* out, std::size_t count) {
    BackBits b;
    if (!b.init(d, size)) return false;
    for (std::size_t i = 0; i < count; ++i) {
        const std::size_t at = static_cast<std::size_t>(b.peek(h.maxBits));
        out[i] = h.symbol[at];
        b.pos -= h.bits[at];
    }
    return b.done();
}

// ---------------------------------------------------------------- sequences' codes

constexpr std::uint32_t kLLBase[36] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 18, 20, 22, 24, 28,
                                       32, 40, 48, 64, 128, 256, 512, 1024, 2048, 4096, 8192, 16384, 32768, 65536};
constexpr std::uint8_t kLLBits[36] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2,
                                      3, 3, 4, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
constexpr std::uint32_t kMLBase[53] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20,
                                       21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 37, 39, 41,
                                       43, 47, 51, 59, 67, 83, 99, 131, 259, 515, 1027, 2051, 4099, 8195, 16387, 32771, 65539};
constexpr std::uint8_t kMLBits[53] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                      0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 3, 3, 4, 4, 5, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
const std::vector<int> kLLDefault = {4, 3, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2,
                                     2, 3, 2, 1, 1, 1, 1, 1, -1, -1, -1, -1};
const std::vector<int> kMLDefault = {1, 4, 3, 2, 2, 2, 2, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
                                     1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, -1, -1, -1, -1, -1, -1, -1};
const std::vector<int> kOFDefault = {1, 1, 1, 1, 1, 1, 2, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, -1, -1, -1, -1, -1};

// What carries from block to block (and comes from a dictionary).
struct State {
    Huffman huffman;
    Fse ll, of, ml;
    bool haveLL = false, haveOF = false, haveML = false;
    std::uint32_t rep[3] = {1, 4, 8};
};

// A sequence's table: mode 0 predefined, 1 RLE, 2 described, 3 the last one.
bool sequenceTable(int mode, const std::uint8_t* d, std::size_t size, std::size_t& at, const std::vector<int>& def, int defLog,
                   int maxLog, int maxSymbol, Fse& table, bool& have) {
    switch (mode) {
        case 0: have = buildFse(def, defLog, table); return have;
        case 1:
            if (at >= size || d[at] > maxSymbol) return false;
            rleFse(d[at++], table);
            have = true;
            return true;
        case 2: {
            std::size_t used = 0;
            if (!readFse(d + at, size - at, maxLog, maxSymbol, table, used)) return false;
            at += used;
            have = true;
            return true;
        }
        default: return have;
    }
}

} // namespace

struct Dictionary::Tables {
    State state;
};
Dictionary::~Dictionary() = default;

namespace {

// One compressed block onto `out` (`frameStart`: where this frame's output
// starts; before it, the dictionary's content).
bool compressedBlock(const std::uint8_t* d, std::size_t size, std::vector<std::uint8_t>& out, std::size_t frameStart,
                     const Dictionary* dict, State& st, std::string& why) {
    // ---- literals
    if (!size) { why = "empty block"; return false; }
    const int litType = d[0] & 3, sizeFormat = (d[0] >> 2) & 3;
    std::size_t at = 0, regen = 0, comp = 0;
    int streams = 1;
    if (litType <= 1) {
        if (sizeFormat == 0 || sizeFormat == 2) { regen = d[0] >> 3; at = 1; }
        else if (sizeFormat == 1) { if (size < 2) return false; regen = (d[0] >> 4) + (std::size_t(d[1]) << 4); at = 2; }
        else { if (size < 3) return false; regen = (d[0] >> 4) + (std::size_t(d[1]) << 4) + (std::size_t(d[2]) << 12); at = 3; }
    } else {
        if (sizeFormat <= 1) {
            if (size < 3) return false;
            regen = (d[0] >> 4) | (std::size_t(d[1] & 0x3F) << 4);
            comp = (d[1] >> 6) | (std::size_t(d[2]) << 2);
            at = 3;
            streams = sizeFormat == 0 ? 1 : 4;
        } else if (sizeFormat == 2) {
            if (size < 4) return false;
            regen = (d[0] >> 4) | (std::size_t(d[1]) << 4) | (std::size_t(d[2] & 3) << 12);
            comp = (d[2] >> 2) | (std::size_t(d[3]) << 6);
            at = 4;
            streams = 4;
        } else {
            if (size < 5) return false;
            regen = (d[0] >> 4) | (std::size_t(d[1]) << 4) | (std::size_t(d[2] & 0x3F) << 12);
            comp = (d[2] >> 6) | (std::size_t(d[3]) << 2) | (std::size_t(d[4]) << 10);
            at = 5;
            streams = 4;
        }
    }
    if (regen > (128u << 10)) { why = "literals too big"; return false; }
    std::vector<std::uint8_t> lit(regen);
    if (litType == 0) {
        if (at + regen > size) { why = "raw literals short"; return false; }
        std::memcpy(lit.data(), d + at, regen);
        at += regen;
    } else if (litType == 1) {
        if (at >= size) return false;
        std::fill(lit.begin(), lit.end(), d[at]);
        at += 1;
    } else {
        if (at + comp > size) { why = "literals short"; return false; }
        const std::uint8_t* c = d + at;
        std::size_t treeBytes = 0;
        if (litType == 2) {
            if (!readHuffman(c, comp, st.huffman, treeBytes)) { why = "bad Huffman tree"; return false; }
        } else if (!st.huffman.ready) {
            why = "no Huffman tree to repeat";
            return false;
        }
        const std::uint8_t* s = c + treeBytes;
        const std::size_t sLen = comp - treeBytes;
        if (streams == 1) {
            if (!huffmanStream(st.huffman, s, sLen, lit.data(), regen)) { why = "bad literals stream"; return false; }
        } else {
            if (sLen < 6) return false;
            const std::size_t s1 = s[0] | std::size_t(s[1]) << 8, s2 = s[2] | std::size_t(s[3]) << 8, s3 = s[4] | std::size_t(s[5]) << 8;
            if (6 + s1 + s2 + s3 > sLen) return false;
            const std::size_t s4 = sLen - 6 - s1 - s2 - s3, part = (regen + 3) / 4;
            if (part * 3 > regen) return false;
            const std::uint8_t* p = s + 6;
            std::size_t o = 0;
            const std::size_t sizes[4] = {s1, s2, s3, s4};
            for (int k = 0; k < 4; ++k) {
                const std::size_t n = k < 3 ? part : regen - 3 * part;
                if (!huffmanStream(st.huffman, p, sizes[k], lit.data() + o, n)) { why = "bad literals stream"; return false; }
                p += sizes[k];
                o += n;
            }
        }
        at += comp;
    }

    // ---- sequences
    if (at >= size) { why = "no sequences section"; return false; }
    std::size_t nbSeq = d[at++];
    if (nbSeq >= 128) {
        if (nbSeq < 255) {
            if (at >= size) return false;
            nbSeq = ((nbSeq - 128) << 8) + d[at++];
        } else {
            if (at + 2 > size) return false;
            nbSeq = d[at] + (std::size_t(d[at + 1]) << 8) + 0x7F00;
            at += 2;
        }
    }
    std::size_t litAt = 0;
    // A match: `length` bytes from `offset` back (into the dictionary's
    // content when it reaches before this frame's start; may overlap itself).
    auto copyMatch = [&](std::size_t offset, std::size_t length) -> bool {
        const std::size_t have = out.size() - frameStart;
        const std::size_t dictSize = dict ? dict->content.size() : 0;
        if (offset == 0 || offset > have + dictSize) return false;
        out.reserve(out.size() + length);
        std::size_t left = length;
        if (offset > have) {
            std::size_t from = dictSize - (offset - have);
            const std::size_t n = std::min(left, offset - have);
            out.insert(out.end(), dict->content.begin() + static_cast<std::ptrdiff_t>(from),
                       dict->content.begin() + static_cast<std::ptrdiff_t>(from + n));
            left -= n;
        }
        for (; left; --left) {
            const std::uint8_t v = out[out.size() - offset];
            out.push_back(v);
        }
        return true;
    };
    if (nbSeq == 0) {
        out.insert(out.end(), lit.begin(), lit.end());
        return true;
    }
    if (at >= size) return false;
    const std::uint8_t modes = d[at++];
    if (modes & 3) { why = "reserved bits set"; return false; }
    if (!sequenceTable(modes >> 6, d, size, at, kLLDefault, 6, 9, 35, st.ll, st.haveLL) ||
        !sequenceTable((modes >> 4) & 3, d, size, at, kOFDefault, 5, 8, 31, st.of, st.haveOF) ||
        !sequenceTable((modes >> 2) & 3, d, size, at, kMLDefault, 6, 9, 52, st.ml, st.haveML)) {
        why = "bad sequence tables";
        return false;
    }
    BackBits b;
    if (!b.init(d + at, size - at)) { why = "bad sequences stream"; return false; }
    std::uint32_t sLL = static_cast<std::uint32_t>(b.read(st.ll.log));
    std::uint32_t sOF = static_cast<std::uint32_t>(b.read(st.of.log));
    std::uint32_t sML = static_cast<std::uint32_t>(b.read(st.ml.log));
    for (std::size_t i = 0; i < nbSeq; ++i) {
        const int ofCode = st.of.table[sOF].symbol, mlCode = st.ml.table[sML].symbol, llCode = st.ll.table[sLL].symbol;
        if (ofCode > 31 || mlCode > 52 || llCode > 35) { why = "bad sequence code"; return false; }
        const std::uint32_t ofValue = (1u << ofCode) + static_cast<std::uint32_t>(b.read(ofCode));
        const std::uint32_t ml = kMLBase[mlCode] + static_cast<std::uint32_t>(b.read(kMLBits[mlCode]));
        const std::uint32_t ll = kLLBase[llCode] + static_cast<std::uint32_t>(b.read(kLLBits[llCode]));
        // The offset: a new one, or one of the three repeated.
        std::uint32_t offset;
        if (ofValue > 3) {
            offset = ofValue - 3;
            st.rep[2] = st.rep[1];
            st.rep[1] = st.rep[0];
            st.rep[0] = offset;
        } else {
            std::uint32_t idx = ofValue - 1 + (ll == 0 ? 1 : 0);
            if (idx == 0) {
                offset = st.rep[0];
            } else {
                offset = idx == 3 ? st.rep[0] - 1 : st.rep[idx];
                if (idx > 1) st.rep[2] = st.rep[1];
                st.rep[1] = st.rep[0];
                st.rep[0] = offset;
            }
        }
        if (litAt + ll > lit.size()) { why = "literals overrun"; return false; }
        out.insert(out.end(), lit.begin() + static_cast<std::ptrdiff_t>(litAt), lit.begin() + static_cast<std::ptrdiff_t>(litAt + ll));
        litAt += ll;
        if (!copyMatch(offset, ml)) { why = "bad match offset"; return false; }
        if (i + 1 < nbSeq) { // the states, in this order
            sLL = st.ll.table[sLL].base + static_cast<std::uint32_t>(b.read(st.ll.table[sLL].bits));
            sML = st.ml.table[sML].base + static_cast<std::uint32_t>(b.read(st.ml.table[sML].bits));
            sOF = st.of.table[sOF].base + static_cast<std::uint32_t>(b.read(st.of.table[sOF].bits));
        }
    }
    if (!b.done()) { why = "sequences stream not used up"; return false; }
    out.insert(out.end(), lit.begin() + static_cast<std::ptrdiff_t>(litAt), lit.end());
    return true;
}

} // namespace

bool loadDictionary(const std::uint8_t* d, std::size_t size, Dictionary& out, std::string& why) {
    out = Dictionary();
    if (size < 8 || le32(d) != 0xEC30A437u) { // raw content
        out.content.assign(d, d + size);
        return true;
    }
    out.id = le32(d + 4);
    auto t = std::make_shared<Dictionary::Tables>();
    std::size_t at = 8, used = 0;
    if (!readHuffman(d + at, size - at, t->state.huffman, used)) { why = "dictionary: bad Huffman tree"; return false; }
    at += used;
    if (!readFse(d + at, size - at, 8, 31, t->state.of, used)) { why = "dictionary: bad offsets table"; return false; }
    at += used;
    if (!readFse(d + at, size - at, 9, 52, t->state.ml, used)) { why = "dictionary: bad match lengths table"; return false; }
    at += used;
    if (!readFse(d + at, size - at, 9, 35, t->state.ll, used)) { why = "dictionary: bad literal lengths table"; return false; }
    at += used;
    t->state.haveLL = t->state.haveOF = t->state.haveML = true;
    if (at + 12 > size) { why = "dictionary: short"; return false; }
    for (int i = 0; i < 3; ++i) t->state.rep[i] = le32(d + at + 4 * i);
    at += 12;
    out.content.assign(d + at, d + size);
    out.tables = std::move(t);
    return true;
}

std::uint32_t frameDictionary(const std::uint8_t* d, std::size_t size) {
    if (size < 6 || le32(d) != 0xFD2FB528u) return 0;
    const std::uint8_t fhd = d[4];
    const int idFlag = fhd & 3;
    const bool single = (fhd >> 5) & 1;
    std::size_t at = 5 + (single ? 0 : 1);
    static const int kIdBytes[4] = {0, 1, 2, 4};
    std::uint32_t id = 0;
    for (int i = 0; i < kIdBytes[idFlag] && at + i < size; ++i) id |= std::uint32_t(d[at + i]) << (8 * i);
    return id;
}

bool decompress(const std::uint8_t* src, std::size_t size, std::vector<std::uint8_t>& out,
                const std::vector<const Dictionary*>& dicts, std::string& why, std::size_t limit) {
    std::size_t at = 0;
    bool any = false;
    while (at + 4 <= size) {
        const std::uint32_t magic = le32(src + at);
        if ((magic & 0xFFFFFFF0u) == 0x184D2A50u) { // skippable
            if (at + 8 > size) break;
            at += 8 + le32(src + at + 4);
            continue;
        }
        if (magic != 0xFD2FB528u) { why = any ? std::string() : "not zstd"; return any; }
        at += 4;
        if (at >= size) { why = "short frame"; return false; }
        const std::uint8_t fhd = src[at++];
        const int fcsFlag = fhd >> 6, idFlag = fhd & 3;
        const bool single = (fhd >> 5) & 1, checksum = (fhd >> 2) & 1;
        if (fhd & 8) { why = "reserved bit"; return false; }
        if (!single) ++at; // (the window descriptor)
        static const int kIdBytes[4] = {0, 1, 2, 4};
        std::uint32_t id = 0;
        for (int i = 0; i < kIdBytes[idFlag]; ++i) id |= std::uint32_t(src[at + i]) << (8 * i);
        at += kIdBytes[idFlag];
        const int fcsBytes = fcsFlag == 0 ? (single ? 1 : 0) : fcsFlag == 1 ? 2 : fcsFlag == 2 ? 4 : 8;
        std::uint64_t fcs = 0;
        for (int i = 0; i < fcsBytes && at + i < size; ++i) fcs |= std::uint64_t(src[at + i]) << (8 * i);
        if (fcsBytes == 2) fcs += 256;
        at += fcsBytes;
        if (at > size) { why = "short frame"; return false; }
        const Dictionary* dict = nullptr;
        if (id) {
            for (const Dictionary* x : dicts)
                if (x && x->id == id) dict = x;
            if (!dict) { why = "needs dictionary " + std::to_string(id); return false; }
        } else if (dicts.size() == 1) { // (a frame that doesn't name one: the one given)
            dict = dicts[0];
        }
        if (fcsBytes && fcs <= limit) out.reserve(out.size() + static_cast<std::size_t>(fcs));
        State st;
        if (dict && dict->tables) st = dict->tables->state;
        const std::size_t frameStart = out.size();
        for (bool last = false; !last;) {
            if (at + 3 > size) { why = "short block"; return false; }
            const std::uint32_t h = src[at] | std::uint32_t(src[at + 1]) << 8 | std::uint32_t(src[at + 2]) << 16;
            at += 3;
            last = h & 1;
            const int type = (h >> 1) & 3;
            const std::size_t bsize = h >> 3;
            if (type == 0) {
                if (at + bsize > size) { why = "short raw block"; return false; }
                out.insert(out.end(), src + at, src + at + bsize);
                at += bsize;
            } else if (type == 1) {
                if (at >= size) return false;
                out.insert(out.end(), bsize, src[at]);
                at += 1;
            } else if (type == 2) {
                if (at + bsize > size) { why = "short block"; return false; }
                if (!compressedBlock(src + at, bsize, out, frameStart, dict, st, why)) return false;
                at += bsize;
            } else {
                why = "reserved block type";
                return false;
            }
            if (out.size() - frameStart > limit) { why = "too big"; return false; }
        }
        if (checksum) at += 4;
        any = true;
    }
    return any;
}

} // namespace zstd
