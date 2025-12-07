extern "C" {
#include "librope/rope.h"
}

#include <string>
#include <vector>
#include <cstdint>

// Minimal C++ wrapper around librope for benchmarking.
// Provides insert, erase, size, and scan operations.
class LibRope {
public:
    LibRope() : r(rope_new()) {}
    ~LibRope() {
        if (r) rope_free(r);
    }

    // Non-copyable
    LibRope(const LibRope&) = delete;
    LibRope& operator=(const LibRope&) = delete;

    // Move
    LibRope(LibRope&& other) noexcept : r(other.r) { other.r = nullptr; }
    LibRope& operator=(LibRope&& other) noexcept {
        if (this != &other) {
            if (r) rope_free(r);
            r = other.r;
            other.r = nullptr;
        }
        return *this;
    }

    void insert(size_t pos, const std::string& s) {
        rope_insert(r, pos, reinterpret_cast<const uint8_t*>(s.c_str()));
    }

    void erase(size_t pos, size_t len) {
        rope_del(r, pos, len);
    }

    size_t size() const { return rope_char_count(r); }

    // Scan all characters and apply func(c)
    template <typename Func>
    void scan(Func func) const {
        const size_t bytes = rope_byte_count(r);
        std::vector<uint8_t> buf(bytes + 1); // room for trailing '\0'
        rope_write_cstr(const_cast<rope*>(r), buf.data());
        for (size_t i = 0; i < bytes; ++i) {
            func(static_cast<char>(buf[i]));
        }
    }

private:
    rope* r;
};
