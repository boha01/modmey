#pragma once
// Lightweight KittyMemory-compatible helpers for /proc/self/maps and safe mprotect writes.
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cinttypes>
#include <sys/mman.h>
#include <unistd.h>

namespace KittyMemory {
    inline bool getLibraryRange(const char* lib, uintptr_t& outBase, uintptr_t& outEnd) {
        outBase = 0;
        outEnd = 0;
        if (!lib || !*lib) return false;
        FILE* f = fopen("/proc/self/maps", "r");
        if (!f) return false;
        char line[512];
        bool found = false;
        while (fgets(line, sizeof(line), f)) {
            if (!strstr(line, lib)) continue;
            uintptr_t start = 0, end = 0;
            if (sscanf(line, "%" SCNxPTR "-%" SCNxPTR, &start, &end) == 2) {
                if (!found || start < outBase) outBase = start;
                if (!found || end > outEnd) outEnd = end;
                found = true;
            }
        }
        fclose(f);
        return found && outEnd > outBase;
    }

    inline uintptr_t getBaseAddress(const char* lib) {
        uintptr_t base = 0, end = 0;
        return getLibraryRange(lib, base, end) ? base : 0;
    }

    inline bool memWrite(void* addr, const void* data, size_t len) {
        if (!addr || !data || len == 0) return false;
        long page = sysconf(_SC_PAGESIZE);
        if (page <= 0) page = 4096;
        uintptr_t start = reinterpret_cast<uintptr_t>(addr) & ~static_cast<uintptr_t>(page - 1);
        uintptr_t finish = (reinterpret_cast<uintptr_t>(addr) + len + page - 1) & ~static_cast<uintptr_t>(page - 1);
        size_t span = static_cast<size_t>(finish - start);
        if (mprotect(reinterpret_cast<void*>(start), span, PROT_READ | PROT_WRITE | PROT_EXEC) != 0) {
            return false;
        }
        memcpy(addr, data, len);
        __builtin___clear_cache(reinterpret_cast<char*>(addr), reinterpret_cast<char*>(addr) + len);
        return true;
    }

    template<typename T>
    inline bool writeValue(uintptr_t addr, T v) {
        return addr != 0 && memWrite(reinterpret_cast<void*>(addr), &v, sizeof(T));
    }
}
