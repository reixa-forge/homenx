#pragma once

// Replacing a small settings file so that a crash or power loss at any point
// leaves one complete copy behind. FAT (the SD card) can't rename over an
// existing file, so the replace is: write path.tmp, commit, move path to
// path.bak, move path.tmp to path, commit, drop path.bak.
//
// Readers try safeFileCandidates(path) in order and take the first copy that
// parses: path, then path.tmp (complete and committed whenever path is
// missing because of a crash between the two renames), then path.bak.
//
// `commit` flushes the filesystem (switchu::commitSdCard on the console); it
// gets a short reason and returns false when the flush failed.

#include <array>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

namespace switchu {

inline std::array<std::string, 3> safeFileCandidates(const std::string& path) {
    return {path, path + ".tmp", path + ".bak"};
}

template <typename Commit>
bool replaceFileSafely(const std::string& path, const std::string& text, Commit&& commit) {
    const std::string temp = path + ".tmp", backup = path + ".bak";
    {
        std::ofstream file(temp, std::ios::binary | std::ios::trunc);
        file << text;
        file.close();
        if (!file) return false;
    }
    if (!commit("safe file staged")) return false;

    std::error_code ec;
    if (std::filesystem::exists(path, ec)) {
        std::filesystem::remove(backup, ec);
        ec.clear();
        std::filesystem::rename(path, backup, ec);
        if (ec) return false; // path is still the old, complete copy
    } else if (ec) {
        return false;
    }
    std::filesystem::rename(temp, path, ec);
    if (ec) return false; // path.tmp (newest) and path.bak stay for the reader
    if (!commit("safe file replaced")) return false;
    std::filesystem::remove(backup, ec);
    commit("safe file backup removed");
    return true;
}

} // namespace switchu
