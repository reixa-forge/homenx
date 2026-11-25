// GameMusicVgm - a game's songs through vgmstream (see GameMusic.hpp): its
// files as vgmstream's streamfiles, .cpk archives opened, songs listed and
// decoded.
#include "GameMusic.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>

extern "C" {
#include "libvgmstream.h"
#include "libvgmstream_streamfile.h"
#include "base/api_internal.h"
#include "util/cri_utf.h"
}

namespace gamemusic {

namespace {

std::string lowerPath(std::string s) {
    for (char& c : s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (c == '\\') c = '/';
    }
    // "a/./b", "a//b": as "a/b" (vgmstream joins its folder and a name)
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '/' && !out.empty() && out.back() == '/') continue;
        if (s[i] == '.' && i + 1 < s.size() && s[i + 1] == '/' && (out.empty() || out.back() == '/')) { ++i; continue; }
        out.push_back(s[i]);
    }
    return out;
}

// ---- vgmstream's IO: one file of the set

struct Sf {
    std::shared_ptr<FileSet> set;
    std::string name;   // its path (vgmstream opens companions beside it)
    VirtualFile file;
};

int sfRead(void* user, uint8_t* dst, int64_t offset, int length) {
    auto* s = static_cast<Sf*>(user);
    if (offset < 0 || length <= 0 || static_cast<std::uint64_t>(offset) >= s->file.size) return 0;
    const std::size_t n = static_cast<std::size_t>(std::min<std::uint64_t>(static_cast<std::uint64_t>(length), s->file.size - static_cast<std::uint64_t>(offset)));
    return s->set->readFile(s->file, static_cast<std::uint64_t>(offset), dst, n) ? static_cast<int>(n) : 0;
}
int64_t sfSize(void* user) { return static_cast<int64_t>(static_cast<Sf*>(user)->file.size); }
const char* sfName(void* user) { return static_cast<Sf*>(user)->name.c_str(); }
libstreamfile_t* sfOpen(void* user, const char* filename);
void sfClose(libstreamfile_t* libsf) {
    if (!libsf) return;
    delete static_cast<Sf*>(libsf->user_data);
    delete libsf;
}

libstreamfile_t* makeSf(std::shared_ptr<FileSet> set, const std::string& path) {
    const VirtualFile* f = set->find(path);
    if (!f) return nullptr;
    auto* s = new Sf{std::move(set), path, *f};
    auto* libsf = new libstreamfile_t{};
    libsf->user_data = s;
    libsf->read = sfRead;
    libsf->get_size = sfSize;
    libsf->get_name = sfName;
    libsf->open = sfOpen;
    libsf->close = sfClose;
    return libsf;
}

libstreamfile_t* sfOpen(void* user, const char* filename) {
    auto* s = static_cast<Sf*>(user);
    return filename ? makeSf(s->set, filename) : nullptr;
}

// Cached (vgmstream seeks about a lot; each read here decrypts).
libstreamfile_t* openCached(std::shared_ptr<FileSet> set, const std::string& path) {
    libstreamfile_t* raw = makeSf(std::move(set), path);
    if (!raw) return nullptr;
    libstreamfile_t* cached = libstreamfile_open_buffered(raw);
    if (!cached) { sfClose(raw); return nullptr; }
    return cached;
}

void quietLog(int, const char*) {}

} // namespace

bool FileSet::readFile(const VirtualFile& f, std::uint64_t at, void* buffer, std::size_t size) {
    if (at + size > f.size) return false;
    std::lock_guard<std::mutex> lk(mu);
    return read && read(f.offset + at, buffer, size);
}

const VirtualFile* FileSet::find(const std::string& path) const {
    const auto it = files.find(lowerPath(path));
    return it == files.end() ? nullptr : &it->second;
}

// ---- .cpk: "CPK " + a @UTF header (TocOffset, ContentOffset, Files), then
// "TOC " + a @UTF table: DirName, FileName, FileSize, ExtractSize, FileOffset
// (from the lower of TocOffset / ContentOffset).
int addCpkFiles(FileSet& set, const std::string& cpkPath, std::string& why) {
    const VirtualFile* cpk = set.find(cpkPath);
    if (!cpk) { why = "not found"; return 0; }
    const VirtualFile base = *cpk;
    // (vgmstream's table reader on this file; the set isn't kept past here)
    std::shared_ptr<FileSet> alias(&set, [](FileSet*) {});
    libstreamfile_t* libsf = openCached(alias, cpkPath);
    if (!libsf) { why = "couldn't be opened"; return 0; }
    STREAMFILE* sf = open_api_streamfile(libsf);
    if (!sf) {
        libstreamfile_close(libsf);
        why = "couldn't be opened";
        return 0;
    }
    int added = 0;
    auto close = [&]() {
        close_streamfile(sf);         // (an outside one: it leaves ours open)
        libstreamfile_close(libsf);
    };
    uint8_t magic[4] = {};
    if (read_streamfile(magic, 0, 4, sf) != 4 || std::memcmp(magic, "CPK ", 4) != 0) { close(); why = "not a CPK"; return 0; }
    int rows = 0;
    const char* tableName = nullptr;
    utf_context* head = utf_open(sf, 0x10, &rows, &tableName);
    if (!head || rows < 1) { if (head) utf_close(head); close(); why = "no CPK header"; return 0; }
    uint64_t tocOffset = 0, contentOffset = 0;
    utf_query_u64(head, 0, "TocOffset", &tocOffset);
    utf_query_u64(head, 0, "ContentOffset", &contentOffset);
    utf_close(head);
    if (!tocOffset) { close(); why = "no file names (ID-only CPK)"; return 0; }
    utf_context* toc = utf_open(sf, static_cast<uint32_t>(tocOffset + 0x10), &rows, &tableName);
    if (!toc) { close(); why = "no TOC"; return 0; }
    const int cDir = utf_get_column(toc, "DirName"), cName = utf_get_column(toc, "FileName"),
              cSize = utf_get_column(toc, "FileSize"), cExtract = utf_get_column(toc, "ExtractSize"),
              cOffset = utf_get_column(toc, "FileOffset");
    struct Entry { std::string path; uint64_t offset; uint32_t size; };
    std::vector<Entry> entries;
    int compressed = 0;
    for (int r = 0; r < rows; ++r) {
        const char* dir = nullptr;
        const char* name = nullptr;
        uint32_t size = 0, extract = 0;
        uint64_t offset = 0;
        if (cDir >= 0) utf_query_col_string(toc, r, cDir, &dir);
        utf_query_col_string(toc, r, cName, &name);
        utf_query_col_u32(toc, r, cSize, &size);
        if (cExtract >= 0) utf_query_col_u32(toc, r, cExtract, &extract);
        utf_query_col_u64(toc, r, cOffset, &offset);
        if (!name) continue;
        if (extract && extract != size) { ++compressed; continue; } // (CRILAYLA: not audio, as a rule)
        entries.push_back({cpkPath + "/" + (dir && *dir ? std::string(dir) + "/" : std::string()) + name, offset, size});
    }
    // Where FileOffset counts from: CRI's tools take the lower of TocOffset and
    // ContentOffset. Checked here on files whose start is known (CRI's own
    // kinds), and another base taken if it fits better.
    auto fits = [&](uint64_t addBase) {
        int good = 0, tried = 0;
        for (const Entry& e : entries) {
            if (tried >= 12) break;
            std::string ext = e.path.substr(e.path.find_last_of('.') + 1);
            for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            const char* want = ext == "acb" || ext == "acf" ? "@UTF" : ext == "awb" ? "AFS2" : ext == "cpk" ? "CPK "
                             : ext == "usm" ? "CRID" : ext == "hca" ? "HCA" : nullptr;
            if (!want) continue;
            ++tried;
            uint8_t m4[4] = {};
            if (addBase + e.offset + 4 > base.size || read_streamfile(m4, static_cast<offv_t>(addBase + e.offset), 4, sf) != 4) continue;
            for (uint8_t& b : m4) b &= 0x7F; // (HCA hides its letters' top bits when encrypted)
            if (std::memcmp(m4, want, std::strlen(want)) == 0) ++good;
        }
        return good;
    };
    uint64_t addBase = contentOffset && contentOffset < tocOffset ? contentOffset : tocOffset;
    int best = fits(addBase);
    for (uint64_t other : {tocOffset, contentOffset, uint64_t(0)}) {
        const int f = fits(other);
        if (f > best) { best = f; addBase = other; }
    }
    for (const Entry& e : entries) {
        const uint64_t at = addBase + e.offset;
        if (at + e.size > base.size) continue;
        set.files[lowerPath(e.path)] = VirtualFile{base.offset + at, e.size};
        ++added;
    }
    utf_close(toc);
    close();
    if (compressed) why = std::to_string(compressed) + " compressed left out";
    return added;
}

// ---- a song

struct VgmSong::Impl {
    libvgmstream_t* lib = nullptr;
    ~Impl() { if (lib) libvgmstream_free(lib); }
};

VgmSong::VgmSong() : m(std::make_unique<Impl>()) {}
VgmSong::~VgmSong() = default;

std::unique_ptr<VgmSong> VgmSong::open(std::shared_ptr<FileSet> set, const std::string& path, int subsong, std::string& why) {
    static bool quiet = (libvgmstream_set_log(LIBVGMSTREAM_LOG_LEVEL_NONE, quietLog), true);
    (void)quiet;
    std::unique_ptr<VgmSong> s(new VgmSong());
    s->m->lib = libvgmstream_init();
    if (!s->m->lib) { why = "vgmstream couldn't start"; return nullptr; }
    libvgmstream_config_t cfg{};
    cfg.loop_count = 1;
    cfg.fade_time = 8.0;
    cfg.auto_downmix_channels = 2;
    cfg.force_sfmt = LIBVGMSTREAM_SFMT_PCM16;
    libvgmstream_setup(s->m->lib, &cfg);
    libstreamfile_t* libsf = openCached(std::move(set), path);
    if (!libsf) { why = "its file wasn't found"; return nullptr; }
    const int rc = libvgmstream_open_stream(s->m->lib, libsf, subsong);
    libstreamfile_close(libsf); // (vgmstream keeps what it needs)
    if (rc < 0) { why = "not a format vgmstream reads"; return nullptr; }
    const libvgmstream_format_t* f = s->m->lib->format;
    if (f->channels < 1 || f->sample_rate < 1 || f->play_samples < 1) { why = "odd song"; return nullptr; }
    return s;
}

int VgmSong::channels() const { return m->lib->format->channels; }
int VgmSong::rate() const { return m->lib->format->sample_rate; }
std::uint64_t VgmSong::frames() const { return static_cast<std::uint64_t>(m->lib->format->play_samples); }
int VgmSong::subsongs() const { return m->lib->format->subsong_count; }
std::string VgmSong::name() const { return m->lib->format->stream_name; }

std::size_t VgmSong::fill(std::int16_t* out, std::size_t frames) {
    if (m->lib->decoder->done) return 0;
    const int want = static_cast<int>(std::min<std::size_t>(frames, 4096));
    if (libvgmstream_fill(m->lib, out, want) < 0) return 0;
    return static_cast<std::size_t>(std::max(0, m->lib->decoder->buf_samples));
}

void VgmSong::restart() { libvgmstream_reset(m->lib); }

// ---- listing

void listVgmSongs(std::shared_ptr<FileSet> set, const std::vector<std::string>& paths, double minSeconds,
                  std::vector<Song>& out, const std::function<bool()>& stop) {
    for (const std::string& path : paths) {
        if (stop && stop()) return;
        std::string why;
        std::unique_ptr<VgmSong> first = VgmSong::open(set, path, 0, why);
        if (!first) continue;
        const int count = std::max(1, first->subsongs());
        std::string stem = path.substr(path.find_last_of('/') + 1);
        if (const size_t dot = stem.find_last_of('.'); dot != std::string::npos) stem.resize(dot);
        for (int k = 1; k <= count; ++k) {
            if (stop && stop()) return;
            std::unique_ptr<VgmSong> one = k == 1 ? std::move(first) : VgmSong::open(set, path, k, why);
            if (!one) continue;
            const double seconds = static_cast<double>(one->frames()) / one->rate();
            if (seconds < minSeconds) continue;
            Song s;
            std::string name = one->name();
            if (name.empty()) name = count > 1 ? stem + " " + std::to_string(k) : stem;
            s.name = name;
            s.ext = ".wav";
            s.path = path;
            s.sub = count > 1 ? k : 0;
            s.seconds = seconds;
            s.size = static_cast<std::uint64_t>(seconds * one->rate() * 2 * 2 / 4) + 64; // (as IMA ADPCM, about)
            out.push_back(std::move(s));
        }
    }
}

} // namespace gamemusic
