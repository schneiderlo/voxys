#include "game/expedition/save_generation.hpp"
#include "core/sha256.hpp"
#include <gtest/gtest.h>
#include <limits>

namespace {
using namespace voxy::game::expedition;
constexpr voxy::game::construction::WorldNamespace kStoreWorld{{'s','t','o','r','e','-','f','i','x','t','u','r','e','-','0','1'}};
TEST(SaveGeneration, PortableEnvelopeKeepsFullCounterAndPayload) {
    const std::vector<std::byte> payload{std::byte{0},std::byte{127},std::byte{255}};
    std::vector<std::byte> bytes;StoreIssue issue;StoredGeneration decoded;
    const uint64_t generation=std::numeric_limits<uint64_t>::max();
    ASSERT_TRUE(encodeStoredGeneration(kStoreWorld,generation,payload,bytes,issue));
    EXPECT_EQ(voxy::core::sha256Hex(voxy::core::sha256(bytes)),"0e0aa18596f322d5b6988fcc25fe1cf97d29794f6ff83ab77c351219b68877b9");
    EXPECT_EQ(bytes.size(),75u);EXPECT_EQ(bytes.at(24),std::byte{255});EXPECT_EQ(bytes.at(31),std::byte{255});
    ASSERT_TRUE(decodeStoredGeneration(bytes,kStoreWorld,decoded,issue));EXPECT_EQ(decoded.generation,generation);EXPECT_EQ(decoded.payload,payload);
    std::vector<std::byte> repeated;ASSERT_TRUE(encodeStoredGeneration(kStoreWorld,decoded.generation,decoded.payload,repeated,issue));EXPECT_EQ(bytes,repeated);
}
TEST(SaveGeneration, RejectsEveryTruncationCorruptionAndWrongIdentityWithoutPublishing) {
    std::vector<std::byte> bytes;StoreIssue issue;const std::vector<std::byte> payload{std::byte{42}};
    ASSERT_TRUE(encodeStoredGeneration(kStoreWorld,7,payload,bytes,issue));
    StoredGeneration output{8,{std::byte{9}},true};
    for(size_t cut=0;cut<bytes.size();++cut) {
        EXPECT_FALSE(decodeStoredGeneration(std::span(bytes).first(cut),kStoreWorld,output,issue));EXPECT_EQ(output.generation,8u);EXPECT_TRUE(output.needsRepair);
    }
    for(size_t at=0;at<bytes.size();++at) {
        auto bad=bytes;bad[at]^=std::byte{1};EXPECT_FALSE(decodeStoredGeneration(bad,kStoreWorld,output,issue));EXPECT_EQ(output.payload,(std::vector<std::byte>{std::byte{9}}));
    }
    auto other=kStoreWorld;other.bytes[0]^=1;EXPECT_FALSE(decodeStoredGeneration(bytes,other,output,issue));
    auto future=bytes;future.at(4)=std::byte{2};EXPECT_FALSE(decodeStoredGeneration(future,kStoreWorld,output,issue));EXPECT_EQ(issue.error,StoreError::UnsupportedSchema);
    auto zero=bytes;for(size_t i=24;i<32;++i)zero.at(i)=std::byte{};
    const auto hash=voxy::core::sha256(std::span(zero).first(zero.size()-32));std::copy(hash.bytes.begin(),hash.bytes.end(),zero.end()-32);
    EXPECT_FALSE(decodeStoredGeneration(zero,kStoreWorld,output,issue));EXPECT_EQ(output.generation,8u);
}
TEST(SaveGeneration, BoundsPayloadAndPreservesEncodeDestination) {
    std::vector<std::byte> output{std::byte{3}};StoreIssue issue;
    EXPECT_FALSE(encodeStoredGeneration(kStoreWorld,0,output,output,issue));EXPECT_EQ(output,(std::vector<std::byte>{std::byte{3}}));
    EXPECT_FALSE(encodeStoredGeneration(kStoreWorld,1,{},output,issue));EXPECT_EQ(issue.error,StoreError::InvalidData);
    const std::vector<std::byte> oversized(kMaximumStoredPayloadBytes+1);
    EXPECT_FALSE(encodeStoredGeneration(kStoreWorld,1,oversized,output,issue));EXPECT_EQ(issue.error,StoreError::Capacity);EXPECT_EQ(output.size(),1u);
}
} // namespace
