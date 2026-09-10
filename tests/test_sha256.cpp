#include "core/sha256.hpp"

#include <gtest/gtest.h>

namespace voxy::core {
namespace {
std::span<const std::byte> bytes(std::string_view text) {
    return std::as_bytes(std::span(text.data(), text.size()));
}

TEST(Sha256, StandardKnownAnswers) {
    EXPECT_EQ(sha256Hex(sha256(bytes(""))),
              "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    EXPECT_EQ(sha256Hex(sha256(bytes("abc"))),
              "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    EXPECT_EQ(sha256Hex(sha256(bytes(
        "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"))),
              "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    EXPECT_EQ(sha256Hex(sha256(bytes(std::string(1000000, 'a')))),
              "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

TEST(Sha256, SplitUpdatesPreservePaddingAndBlockBoundaries) {
    const std::string text(129, 'a');
    // Independent Python hashlib/OpenSSL SHA-256 reference, including two full
    // blocks and a tail. Exercise every possible split, then one-byte chunks.
    const auto expected = "c12cb024a2e5551cca0e08fce8f1c5e314555cc3fef6329ee994a3db752166ae";
    for (size_t split = 0; split <= text.size(); ++split) {
        Sha256 hash;
        hash.update(bytes(text).first(split));
        hash.update({});
        hash.update(bytes(text).subspan(split));
        EXPECT_EQ(sha256Hex(hash.finish()), expected) << split;
    }
    Sha256 hash;
    for (char value : text) hash.update(std::as_bytes(std::span(&value, 1)));
    EXPECT_EQ(sha256Hex(hash.finish()), expected);
}

TEST(Sha256, IndependentPaddingBoundaryVectors) {
    const std::array<std::pair<size_t, std::string_view>, 5> references{{
        {55, "9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318"},
        {56, "b35439a4ac6f0948b6d6f9e3c6af0f5f590ce20f1bde7090ef7970686ec6738a"},
        {63, "7d3e74a05d7db15bce4ad9ec0658ea98e3f06eeecf16b4c6fff2da457ddc2f34"},
        {64, "ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb"},
        {65, "635361c48bb9eab14198e76ea8ab7f1a41685d6ad62aa9146d301d4f17eb0ae0"},
    }};
    for (const auto& [length, expected] : references) {
        const std::string text(length, 'a');
        EXPECT_EQ(sha256Hex(sha256(bytes(text))), expected);
    }
}

TEST(Sha256, FinishIsANondestructivePrefixSnapshot) {
    Sha256 hash;
    hash.string("a");
    const auto first = hash.finish();
    EXPECT_EQ(first, hash.finish());
    hash.string("bc");
    EXPECT_NE(first, hash.finish());
    EXPECT_EQ(hash.finish(), sha256(bytes("abc")));
}

TEST(Sha256, ExplicitLittleEndianWordsMatchByteEncoding) {
    Sha256 encoded;
    encoded.u32Little(0x04030201u);
    encoded.u64Little(0x0c0b0a0908070605ull);
    const std::array<std::byte, 12> reference{
        std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4},
        std::byte{5}, std::byte{6}, std::byte{7}, std::byte{8},
        std::byte{9}, std::byte{10}, std::byte{11}, std::byte{12}};
    EXPECT_EQ(encoded.finish(), sha256(reference));
}
} // namespace
} // namespace voxy::core
