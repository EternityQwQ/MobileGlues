#include "cache.h"
#include <fstream>
#include <cstring>

using namespace std;

static constexpr uint64_t XXH_PRIME64_1 = 0x9E3779B185EBCA87ULL;
static constexpr uint64_t XXH_PRIME64_2 = 0xC2B2AE3D27D4EB4FULL;
static constexpr uint64_t XXH_PRIME64_3 = 0x165667B19E3779F9ULL;
static constexpr uint64_t XXH_PRIME64_4 = 0x85EBCA77C2B2AE63ULL;
static constexpr uint64_t XXH_PRIME64_5 = 0x27D4EB2F165667C5ULL;

static inline uint64_t XXH64_round(uint64_t acc, uint64_t input) {
    acc += input * XXH_PRIME64_2;
    acc = (acc << 31) | (acc >> 33);
    acc *= XXH_PRIME64_1;
    return acc;
}

static inline uint64_t XXH64_avalanche(uint64_t h64) {
    h64 ^= h64 >> 33;
    h64 *= XXH_PRIME64_2;
    h64 ^= h64 >> 29;
    h64 *= XXH_PRIME64_3;
    h64 ^= h64 >> 32;
    return h64;
}

static uint64_t XXH64(const void* input, size_t len, uint64_t seed) {
    const uint8_t* p = (const uint8_t*)input;
    const uint8_t* const bEnd = p + len;
    uint64_t h64;

    if (len >= 32) {
        const uint8_t* const limit = bEnd - 32;
        uint64_t v1 = seed + XXH_PRIME64_1 + XXH_PRIME64_2;
        uint64_t v2 = seed + XXH_PRIME64_2;
        uint64_t v3 = seed + 0;
        uint64_t v4 = seed - XXH_PRIME64_1;

        do {
            uint64_t k1, k2, k3, k4;
            memcpy(&k1, p, 8); memcpy(&k2, p+8, 8); memcpy(&k3, p+16, 8); memcpy(&k4, p+24, 8);
            p += 32;
            v1 = XXH64_round(v1, k1);
            v2 = XXH64_round(v2, k2);
            v3 = XXH64_round(v3, k3);
            v4 = XXH64_round(v4, k4);
        } while (p <= limit);

        h64 = ((v1 << 1) | (v1 >> 63)) + ((v2 << 7) | (v2 >> 57)) +
              ((v3 << 12) | (v3 >> 52)) + ((v4 << 18) | (v4 >> 46));

        v1 *= XXH_PRIME64_2; v1 = (v1 << 31) | (v1 >> 33); v1 *= XXH_PRIME64_1;
        h64 = (h64 ^ v1) * XXH_PRIME64_1 + XXH_PRIME64_4;
        v2 *= XXH_PRIME64_2; v2 = (v2 << 31) | (v2 >> 33); v2 *= XXH_PRIME64_1;
        h64 = (h64 ^ v2) * XXH_PRIME64_1 + XXH_PRIME64_4;
        v3 *= XXH_PRIME64_2; v3 = (v3 << 31) | (v3 >> 33); v3 *= XXH_PRIME64_1;
        h64 = (h64 ^ v3) * XXH_PRIME64_1 + XXH_PRIME64_4;
        v4 *= XXH_PRIME64_2; v4 = (v4 << 31) | (v4 >> 33); v4 *= XXH_PRIME64_1;
        h64 = (h64 ^ v4) * XXH_PRIME64_1 + XXH_PRIME64_4;
    } else {
        h64 = seed + XXH_PRIME64_5;
    }

    h64 += (uint64_t)len;

    while (p + 8 <= bEnd) {
        uint64_t k1;
        memcpy(&k1, p, 8);
        k1 *= XXH_PRIME64_2; k1 = (k1 << 31) | (k1 >> 33); k1 *= XXH_PRIME64_1;
        h64 ^= k1;
        h64 = ((h64 << 27) | (h64 >> 37)) * XXH_PRIME64_1 + XXH_PRIME64_4;
        p += 8;
    }

    if (p + 4 <= bEnd) {
        uint32_t v;
        memcpy(&v, p, 4);
        h64 ^= (uint64_t)v * XXH_PRIME64_1;
        h64 = ((h64 << 23) | (h64 >> 41)) * XXH_PRIME64_2 + XXH_PRIME64_3;
        p += 4;
    }

    while (p < bEnd) {
        h64 ^= (uint64_t)(*p) * XXH_PRIME64_5;
        h64 = (h64 << 11) | (h64 >> 53);
        h64 *= XXH_PRIME64_1;
        p++;
    }

    return XXH64_avalanche(h64);
}

uint64_t Cache::computeHash(const char* data, size_t len) {
    return XXH64(data, len, 0);
}

Cache::Cache() {
    load();
}

Cache::~Cache() {
    if (dirty) {
        save();
    }
}

const char* Cache::get(const char* glsl) {
    if (global_settings.max_glsl_cache_size <= 0) return nullptr;
    uint64_t hash = computeHash(glsl, strlen(glsl));
    auto it = cacheMap.find(hash);
    if (it == cacheMap.end()) return nullptr;

    cacheList.splice(cacheList.end(), cacheList, it->second);
    return it->second->essl.c_str();
}

void Cache::put(const char* glsl, const char* essl) {
    if (global_settings.max_glsl_cache_size <= 0) return;
    uint64_t hash = computeHash(glsl, strlen(glsl));
    size_t esslStrSize = strlen(essl) + 1;

    size_t entryMemory = sizeof(uint64_t) + sizeof(size_t) + esslStrSize;

    if (auto it = cacheMap.find(hash); it != cacheMap.end()) {
        cacheSize -= (sizeof(uint64_t) + sizeof(size_t) + it->second->size);
        cacheList.erase(it->second);
        cacheMap.erase(it);
    }

    cacheList.emplace_back(CacheEntry{hash, essl, esslStrSize});
    cacheMap[hash] = prev(cacheList.end());
    cacheSize += entryMemory;

    maintainCacheSize();
    dirty = true;
}

void Cache::maintainCacheSize() {
    if (global_settings.max_glsl_cache_size <= 0) return;
    while (cacheSize > global_settings.max_glsl_cache_size && !cacheList.empty()) {
        const auto& oldEntry = cacheList.front();
        size_t removedMemory = sizeof(uint64_t) + sizeof(size_t) + oldEntry.size;
        cacheSize -= removedMemory;
        cacheMap.erase(oldEntry.hash);
        cacheList.pop_front();
    }
}

bool Cache::load() {
    try {
        ifstream file(glsl_cache_file_path, ios::binary);
        if (!file) return false;

        size_t count;
        file.read(reinterpret_cast<char*>(&count), sizeof(count));

        while (count--) {
            uint64_t hash = 0;
            size_t esslSize;

            file.read(reinterpret_cast<char*>(&hash), sizeof(hash));
            file.read(reinterpret_cast<char*>(&esslSize), sizeof(esslSize));

            string essl(esslSize, '\0');
            file.read(essl.data(), (long)esslSize);

            if (cacheMap.count(hash)) continue;

            size_t entryMemory = sizeof(uint64_t) + sizeof(size_t) + esslSize;
            cacheSize += entryMemory;

            cacheList.emplace_back(CacheEntry{hash, move(essl), esslSize});
            cacheMap[hash] = prev(cacheList.end());
        }

        maintainCacheSize();
        return true;
    }
    catch (...) {
        LOG_W_FORCE("Error while loading glsl cache file. Clearing it...")
        cacheMap.clear();
        cacheSize = 0;
        cacheList.clear();
        save();
        return false;
    }
}

void Cache::save() {
    if (global_settings.max_glsl_cache_size <= 0 || !dirty) return;
    ofstream file(glsl_cache_file_path, ios::binary);
    if (!file) return;

    size_t count = cacheList.size();
    file.write(reinterpret_cast<const char*>(&count), sizeof(count));

    for (const auto& entry : cacheList) {
        file.write(reinterpret_cast<const char*>(&entry.hash), sizeof(entry.hash));
        size_t esslSize = entry.size;
        file.write(reinterpret_cast<const char*>(&esslSize), sizeof(esslSize));
        file.write(entry.essl.data(), (long)esslSize);
    }
    dirty = false;
}

Cache& Cache::get_instance() {
    static Cache s_cache;
    return s_cache;
}