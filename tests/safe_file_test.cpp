// replaceFileSafely: every step that can fail leaves a complete copy at
// path, path.tmp or path.bak, in the order safeFileCandidates() reads them.
#include <switchu/safe_file.hpp>

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <unistd.h>

namespace fs = std::filesystem;

static std::string read(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

static void write(const fs::path& p, const std::string& text) {
    std::ofstream(p, std::ios::binary | std::ios::trunc) << text;
}

// The first candidate that exists and is "complete" (here: starts with '{' and ends with '}').
static std::string firstComplete(const std::string& path) {
    for (const std::string& c : switchu::safeFileCandidates(path)) {
        const std::string t = read(c);
        if (!t.empty() && t.front() == '{' && t.back() == '}') return t;
    }
    return "";
}

int main() {
    const fs::path dir = fs::temp_directory_path() / ("safe-file-test-" + std::to_string(::getpid()));
    fs::remove_all(dir);
    fs::create_directories(dir);
    const std::string path = (dir / "discord.json").string();

    int commits = 0;
    auto ok = [&](const char*) { ++commits; return true; };

    // First save: no previous file.
    assert(switchu::replaceFileSafely(path, "{\"v\":1}", ok));
    assert(read(path) == "{\"v\":1}");
    assert(!fs::exists(path + ".tmp") && !fs::exists(path + ".bak"));
    assert(commits == 3);

    // Replace an existing file.
    assert(switchu::replaceFileSafely(path, "{\"v\":2}", ok));
    assert(read(path) == "{\"v\":2}");
    assert(!fs::exists(path + ".tmp") && !fs::exists(path + ".bak"));

    // The staging commit fails: the old file is untouched and still read first.
    auto failFirst = [](const char* why) { return std::string(why) != "safe file staged"; };
    assert(!switchu::replaceFileSafely(path, "{\"v\":3}", failFirst));
    assert(read(path) == "{\"v\":2}");
    assert(firstComplete(path) == "{\"v\":2}");
    fs::remove(path + ".tmp");

    // The commit after the renames fails: the new copy is in place; the backup stays.
    auto failSecond = [](const char* why) { return std::string(why) != "safe file replaced"; };
    assert(!switchu::replaceFileSafely(path, "{\"v\":4}", failSecond));
    assert(read(path) == "{\"v\":4}");
    assert(read(path + ".bak") == "{\"v\":2}");
    assert(firstComplete(path) == "{\"v\":4}");
    // A later good save clears the stale backup.
    assert(switchu::replaceFileSafely(path, "{\"v\":5}", ok));
    assert(!fs::exists(path + ".bak"));

    // Crash between the two renames: path is gone, .tmp is new and complete, .bak is old.
    fs::rename(path, path + ".bak");
    write(path + ".tmp", "{\"v\":6}");
    assert(firstComplete(path) == "{\"v\":6}");
    // The next save recovers to a single file.
    assert(switchu::replaceFileSafely(path, "{\"v\":7}", ok));
    assert(read(path) == "{\"v\":7}");
    assert(!fs::exists(path + ".tmp") && !fs::exists(path + ".bak"));

    // A torn write of path itself (old firmware, or another writer): the backup is used.
    write(path, "{\"v\":");
    write(path + ".bak", "{\"v\":7}");
    assert(firstComplete(path) == "{\"v\":7}");

    // An unwritable location fails cleanly without touching anything.
    assert(!switchu::replaceFileSafely((dir / "missing" / "x.json").string(), "{}", ok));

    fs::remove_all(dir);
    std::puts("Safe file replace tests passed.");
    return 0;
}
