// XOR + run-length difference between two equally sized buffers (used by rewind)
#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>

inline void rle_xor(const std::vector<uint8_t>& cur, const std::vector<uint8_t>& key, std::vector<uint8_t>& out) {
    out.clear();
    size_t n = cur.size(), i = 0;
    while (i < n) {
        size_t z = 0;
        while (i + z < n && cur[i + z] == key[i + z] && z < 0x7FFF) z++;
        size_t lit = 0;
        while (i + z + lit < n && cur[i + z + lit] != key[i + z + lit] && lit < 0x7FFF) lit++;
        out.push_back((uint8_t)(z & 0xFF)); out.push_back((uint8_t)(z >> 8));
        out.push_back((uint8_t)(lit & 0xFF)); out.push_back((uint8_t)(lit >> 8));
        for (size_t k = 0; k < lit; k++) out.push_back(cur[i + z + k] ^ key[i + z + k]);
        i += z + lit;
    }
}
inline bool un_rle_xor(const std::vector<uint8_t>& d, const std::vector<uint8_t>& key, std::vector<uint8_t>& out) {
    out = key;
    size_t p = 0, i = 0;
    while (p + 4 <= d.size()) {
        size_t z = d[p] | (d[p + 1] << 8), lit = d[p + 2] | (d[p + 3] << 8);
        p += 4; i += z;
        if (i + lit > out.size() || p + lit > d.size()) return false;
        for (size_t k = 0; k < lit; k++) out[i + k] ^= d[p + k];
        p += lit; i += lit;
    }
    return true;
}

