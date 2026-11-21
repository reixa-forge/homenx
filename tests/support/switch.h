#pragma once
extern "C" {
#include <switch/result.h>
#include <switch/nacp.h>
#include <switch/services/fs.h>
#include <switch/services/ns.h>

// What the host tests need of libnx's runtime and kernel, done nothing.
inline Result fsdevCommitDevice(const char*) { return 0; }
inline Result svcOutputDebugString(const char*, u64) { return 0; }
}
