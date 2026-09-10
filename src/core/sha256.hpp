#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <string_view>

namespace voxy::core {

struct Sha256Digest {
    std::array<std::byte, 32> bytes{};
    bool operator==(const Sha256Digest&) const = default;
};

inline constexpr std::array<uint32_t, 64> kSha256RoundConstants{{
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u,
    0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
    0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
    0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
    0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
    0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u,
    0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u,
    0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
}};

class Sha256 {
public:
    // Returns a digest of the current prefix without consuming the accumulator.
    // Inputs in this project are bounded well below SHA-256's 2^64-bit limit.
    [[nodiscard]] Sha256Digest finish() const noexcept {
        auto copy = *this;
        return copy.finalize();
    }

    void update(std::span<const std::byte> bytes) noexcept {
        if (bytes.empty()) return;
        totalBytes_ += bytes.size();
        if (blockBytes_ != 0u) {
            const size_t copied = std::min(
                bytes.size(), block_.size() - blockBytes_);
            std::memcpy(
                block_.data() + blockBytes_,
                bytes.data(),
                copied);
            blockBytes_ += copied;
            bytes = bytes.subspan(copied);
            if (blockBytes_ == block_.size()) {
                transform(block_);
                blockBytes_ = 0u;
            }
        }
        while (bytes.size() >= block_.size()) {
            transform(bytes.first(block_.size()));
            bytes = bytes.subspan(block_.size());
        }
        if (!bytes.empty()) {
            std::memcpy(
                block_.data(), bytes.data(), bytes.size());
            blockBytes_ = bytes.size();
        }
    }

    void string(std::string_view text) noexcept {
        update(std::as_bytes(
            std::span<const char>(text.data(), text.size())));
    }

    void u32Little(uint32_t value) noexcept {
        std::array<std::byte, sizeof(uint32_t)> bytes{};
        for (uint32_t index = 0u; index < 4u; ++index) {
            bytes[index] = std::byte{
                static_cast<uint8_t>(value >> (index * 8u))};
        }
        update(bytes);
    }

    void u64Little(uint64_t value) noexcept {
        std::array<std::byte, sizeof(uint64_t)> bytes{};
        for (uint32_t index = 0u; index < 8u; ++index) {
            bytes[index] = std::byte{
                static_cast<uint8_t>(value >> (index * 8u))};
        }
        update(bytes);
    }

private:
    [[nodiscard]] Sha256Digest finalize() noexcept {
        const uint64_t bitLength = totalBytes_ * 8u;
        std::array<std::byte, 128> padding{};
        padding[0] = std::byte{0x80u};
        const size_t paddingBytes =
            blockBytes_ < 56u
            ? 56u - blockBytes_
            : 120u - blockBytes_;
        update(std::span<const std::byte>(
            padding.data(), paddingBytes));

        std::array<std::byte, 8> encodedLength{};
        for (uint32_t index = 0u; index < 8u; ++index) {
            encodedLength[index] = std::byte{
                static_cast<uint8_t>(
                    bitLength >> ((7u - index) * 8u))};
        }
        update(encodedLength);

        Sha256Digest result;
        for (size_t word = 0u; word < state_.size(); ++word) {
            for (uint32_t byte = 0u; byte < 4u; ++byte) {
                result.bytes[word * 4u + byte] = std::byte{
                    static_cast<uint8_t>(
                        state_[word] >> ((3u - byte) * 8u))};
            }
        }
        return result;
    }

private:
    static uint32_t loadBigEndian(
        std::span<const std::byte> bytes, size_t offset) noexcept {
        uint32_t value = 0u;
        for (uint32_t index = 0u; index < 4u; ++index) {
            value = (value << 8u)
                | std::to_integer<uint32_t>(
                    bytes[offset + index]);
        }
        return value;
    }

    void transform(std::span<const std::byte> block) noexcept {
        std::array<uint32_t, 64> words{};
        for (size_t index = 0u; index < 16u; ++index) {
            words[index] = loadBigEndian(block, index * 4u);
        }
        for (size_t index = 16u; index < words.size(); ++index) {
            const uint32_t x = words[index - 15u];
            const uint32_t y = words[index - 2u];
            const uint32_t small0 =
                std::rotr(x, 7) ^ std::rotr(x, 18) ^ (x >> 3u);
            const uint32_t small1 =
                std::rotr(y, 17) ^ std::rotr(y, 19) ^ (y >> 10u);
            words[index] =
                words[index - 16u] + small0
                + words[index - 7u] + small1;
        }

        uint32_t a = state_[0];
        uint32_t b = state_[1];
        uint32_t c = state_[2];
        uint32_t d = state_[3];
        uint32_t e = state_[4];
        uint32_t f = state_[5];
        uint32_t g = state_[6];
        uint32_t h = state_[7];
        for (size_t index = 0u; index < words.size(); ++index) {
            const uint32_t big1 =
                std::rotr(e, 6) ^ std::rotr(e, 11)
                ^ std::rotr(e, 25);
            const uint32_t choose = (e & f) ^ (~e & g);
            const uint32_t first =
                h + big1 + choose
                + kSha256RoundConstants[index] + words[index];
            const uint32_t big0 =
                std::rotr(a, 2) ^ std::rotr(a, 13)
                ^ std::rotr(a, 22);
            const uint32_t majority =
                (a & b) ^ (a & c) ^ (b & c);
            const uint32_t second = big0 + majority;
            h = g;
            g = f;
            f = e;
            e = d + first;
            d = c;
            c = b;
            b = a;
            a = first + second;
        }
        state_[0] += a;
        state_[1] += b;
        state_[2] += c;
        state_[3] += d;
        state_[4] += e;
        state_[5] += f;
        state_[6] += g;
        state_[7] += h;
    }

    std::array<uint32_t, 8> state_{{
        0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
        0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u,
    }};
    std::array<std::byte, 64> block_{};
    uint64_t totalBytes_ = 0u;
    size_t blockBytes_ = 0u;
};

[[nodiscard]] inline Sha256Digest sha256(std::span<const std::byte> bytes) noexcept {
    Sha256 hash;
    hash.update(bytes);
    return hash.finish();
}

[[nodiscard]] inline std::string sha256Hex(const Sha256Digest& digest) {
    constexpr std::string_view digits = "0123456789abcdef";
    std::string result(64, '0');
    for (size_t i = 0; i < digest.bytes.size(); ++i) {
        const auto value = std::to_integer<uint8_t>(digest.bytes[i]);
        result[2 * i] = digits[value >> 4u];
        result[2 * i + 1] = digits[value & 15u];
    }
    return result;
}

} // namespace voxy::core
