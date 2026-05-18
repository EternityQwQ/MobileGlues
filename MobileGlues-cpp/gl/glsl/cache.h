#ifndef MOBILEGLUES_PLUGIN_CACHE_H
#define MOBILEGLUES_PLUGIN_CACHE_H

#include "../mg.h"
#include "../../config/config.h"
#include "../../config/settings.h"

#include <list>
#include <string>
#include <cstdint>

class Cache {
public:
    Cache();
    ~Cache();
    const char* get(const char* glsl);
    void put(const char* glsl, const char* essl);
    bool load();
    void save();

    static Cache& get_instance();

private:
    struct CacheEntry {
        uint64_t hash;
        std::string essl;
        size_t size;
    };

    std::list<CacheEntry> cacheList;
    using ListIterator = std::list<CacheEntry>::iterator;
    UnorderedMap<uint64_t, ListIterator> cacheMap;
    size_t cacheSize = 0;
    bool dirty = false;

    static uint64_t computeHash(const char* data, size_t len);
    void maintainCacheSize();
};

#endif