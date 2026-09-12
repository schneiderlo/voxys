#include "game/expedition/cove_effects.hpp"
#include <gtest/gtest.h>
#include <limits>
#include <vector>

namespace voxy::game::expedition {
namespace {
using Effects=CoveEffects;
using Kind=Effects::Kind;
Effects::MotionSource boat() {
    Effects::MotionSource source;
    source.id.owner.world.bytes[0]=1;source.id.owner.counter=9;source.id.body={2,3};
    source.waterContact=true;source.point={0,0,0};source.footprintRadius=1;
    return source;
}
bool observe(Effects& effects,uint64_t tick,const Effects::MotionSource& source,bool running=true,
    std::span<const Effects::Impact> impacts={}) {
    return effects.observe({1,tick,running,std::span(&source,1),impacts});
}
std::vector<Effects::Instance> copy(const Effects& effects) {
    return {effects.instances().begin(),effects.instances().end()};
}

TEST(CoveEffects, WakesRequireActualAcceptedMovementAndCannotReplayOnDisplayFrames) {
    Effects effects;ASSERT_TRUE(effects.reset(1,0));auto source=boat();source.pointVelocity={6,0,0};
    ASSERT_TRUE(observe(effects,1,source));
    for(uint64_t tick=2;tick<=20;++tick)ASSERT_TRUE(observe(effects,tick,source));
    EXPECT_EQ(effects.stats().emitted,0); // A velocity/control value alone is not travelled water.
    for(uint64_t tick=21;tick<=40;++tick){source.point.x+=.1;ASSERT_TRUE(observe(effects,tick,source));}
    EXPECT_GT(effects.stats().emittedByKind[size_t(Kind::Wake)],0);
    const auto before=copy(effects);const auto total=effects.stats().emitted;
    for(int i=0;i<100;++i)ASSERT_TRUE(observe(effects,40,source));
    EXPECT_EQ(copy(effects),before);EXPECT_EQ(effects.stats().emitted,total);
    source.waterContact=false;
    for(uint64_t tick=41;tick<=50;++tick){source.point.x+=.1;ASSERT_TRUE(observe(effects,tick,source));}
    EXPECT_EQ(effects.stats().emitted,total); // A boat on land has no wake.
}

TEST(CoveEffects, PropellerFoamUsesEnabledEffectiveDriveAndWetness) {
    Effects positive,negative,dry,disabled;
    for(auto* effects:{&positive,&negative,&dry,&disabled})ASSERT_TRUE(effects->reset(1,0));
    auto source=boat();source.hasPropeller=true;source.propellerPoint.y=-.2;source.effectiveDrive=1;
    for(uint64_t tick=1;tick<=12;++tick) {
        ASSERT_TRUE(observe(positive,tick,source));
        auto reverse=source;reverse.effectiveDrive=-1;ASSERT_TRUE(observe(negative,tick,reverse));
        auto out=source;out.propellerPoint.y=.5;ASSERT_TRUE(observe(dry,tick,out));
        auto stopped=source;stopped.effectiveDrive=0;ASSERT_TRUE(observe(disabled,tick,stopped));
    }
    EXPECT_EQ(positive.stats().emittedByKind[size_t(Kind::Foam)],4);
    EXPECT_EQ(negative.stats().emitted,positive.stats().emitted);
    EXPECT_EQ(dry.stats().emitted,0);EXPECT_EQ(disabled.stats().emitted,0);
    EXPECT_EQ(positive.stats().emittedByKind[size_t(Kind::Wake)],0);
}

TEST(CoveEffects, WaterCrossingsCreateOneSplashThenRealSurfaceRunoff) {
    Effects effects;ASSERT_TRUE(effects.reset(1,0));auto source=boat();source.id.kind=Effects::SourceKind::Cargo;
    source.waterContact=false;source.point.y=.1;source.pointVelocity.y=-2;
    ASSERT_TRUE(observe(effects,1,source));source.waterContact=true;source.point.y=-.03;
    ASSERT_TRUE(observe(effects,2,source));EXPECT_EQ(effects.stats().emittedByKind[size_t(Kind::Splash)],12);
    ASSERT_TRUE(observe(effects,3,source));EXPECT_EQ(effects.stats().emittedByKind[size_t(Kind::Splash)],12);
    source.waterContact=false;source.point.y=.12;source.pointVelocity.y=.7;
    ASSERT_TRUE(observe(effects,4,source));EXPECT_EQ(effects.stats().emittedByKind[size_t(Kind::Drain)],8);
    // Restoring a source already under water cannot manufacture an entry event.
    ASSERT_TRUE(effects.reset(1,4));source.waterContact=true;source.pointVelocity.y=-2;
    ASSERT_TRUE(observe(effects,5,source));EXPECT_EQ(effects.stats().emitted,0);
}

TEST(CoveEffects, SwimmingEndpointVelocityClampStillUsesAcceptedEntryDisplacement) {
    Effects effects;ASSERT_TRUE(effects.reset(1,0));auto source=boat();
    source.id.kind=Effects::SourceKind::Player;source.id.body={};
    source.waterContact=false;source.point={0,-.2,0};source.pointVelocity={0,-7,0};
    ASSERT_TRUE(observe(effects,1,source));
    source.waterContact=true;source.point.y=-.8;source.pointVelocity={};
    ASSERT_TRUE(observe(effects,7,source));
    EXPECT_EQ(effects.stats().emittedByKind[size_t(Kind::Splash)],12);
    EXPECT_EQ(effects.stats().playerEntrySplashes,12);
    ASSERT_TRUE(observe(effects,13,source));EXPECT_EQ(effects.stats().emitted,12);
    EXPECT_EQ(effects.stats().playerEntrySplashes,12);
    // A water-mode change without actual entry motion does not fake a splash.
    ASSERT_TRUE(effects.reset(1,13));source.waterContact=false;
    ASSERT_TRUE(observe(effects,14,source));source.waterContact=true;
    ASSERT_TRUE(observe(effects,20,source));EXPECT_EQ(effects.stats().emitted,0);
    EXPECT_EQ(effects.stats().playerEntrySplashes,0);
    // Another body's real crossing contributes to global splashes only.
    ASSERT_TRUE(effects.reset(1,20));source=boat();source.waterContact=false;
    source.point.y=.2;source.pointVelocity.y=-2;ASSERT_TRUE(observe(effects,21,source));
    source.point.y=-.1;source.waterContact=true;ASSERT_TRUE(observe(effects,27,source));
    EXPECT_EQ(effects.stats().emittedByKind[size_t(Kind::Splash)],12);
    EXPECT_EQ(effects.stats().playerEntrySplashes,0);
}

TEST(CoveEffects, ExactPairedImpactsRejectStaleUnknownAndMalformedInputsAtomically) {
    Effects effects;ASSERT_TRUE(effects.reset(1,0));auto source=boat();
    Effects::Impact impact;impact.source=source.id;impact.tick=1;impact.point={0,2,0};impact.speed=3;impact.impulse=4;
    ASSERT_TRUE(observe(effects,1,source,true,std::span(&impact,1)));
    EXPECT_EQ(effects.stats().emittedByKind[size_t(Kind::Dust)],10);
    const auto before=copy(effects);
    EXPECT_FALSE(observe(effects,2,source,true,std::span(&impact,1))); // Stale event / newer pose forbidden.
    impact.tick=2;impact.source.body.generation++;EXPECT_FALSE(observe(effects,2,source,true,std::span(&impact,1)));
    impact.source=source.id;impact.normal={0,0,0};EXPECT_FALSE(observe(effects,2,source,true,std::span(&impact,1)));
    impact.normal={0,1,0};impact.impulse=std::numeric_limits<double>::quiet_NaN();
    EXPECT_FALSE(observe(effects,2,source,true,std::span(&impact,1)));
    EXPECT_EQ(copy(effects),before);EXPECT_EQ(effects.stats().tick,1);EXPECT_EQ(effects.stats().rejected,4);
}

TEST(CoveEffects, ContactsAreDeduplicatedWithoutMergingDifferentBodiesAndDeepImpactsDoNotSpray) {
    Effects effects;ASSERT_TRUE(effects.reset(1,0));auto source=boat();
    Effects::Impact impact;impact.source=source.id;impact.tick=1;impact.point={0,.04,0};impact.speed=2;impact.impulse=2;
    std::array impacts{impact,impact,impact};impacts[2].otherBody={7,1};
    ASSERT_TRUE(observe(effects,1,source,true,impacts));
    EXPECT_EQ(effects.stats().emittedByKind[size_t(Kind::Splash)],16);
    impact.tick=2;impact.point.y=-1;ASSERT_TRUE(observe(effects,2,source,true,std::span(&impact,1)));
    EXPECT_EQ(effects.stats().emitted,16);
}

TEST(CoveEffects, PauseFreezesParticlesAndRebasesWithoutResumeCatchup) {
    Effects effects;ASSERT_TRUE(effects.reset(1,0));auto source=boat();source.hasPropeller=true;
    source.propellerPoint.y=-.2;source.effectiveDrive=1;
    for(uint64_t tick=1;tick<=10;++tick)ASSERT_TRUE(observe(effects,tick,source));
    const auto before=copy(effects);const auto emitted=effects.stats().emitted;
    for(uint64_t tick=11;tick<=20;++tick){source.point.x+=.1;ASSERT_TRUE(observe(effects,tick,source,false));}
    EXPECT_EQ(copy(effects),before);EXPECT_EQ(effects.stats().emitted,emitted);
    source.effectiveDrive=0;ASSERT_TRUE(observe(effects,21,source));
    EXPECT_EQ(effects.stats().emitted,emitted);
}

TEST(CoveEffects, CapacityDropsNewCosmeticsAndAllInstancesFadeWithinBounds) {
    Effects effects;ASSERT_TRUE(effects.reset(1,0));auto source=boat();
    std::array<Effects::Impact,Effects::maximumImpacts> impacts{};
    for(size_t i=0;i<impacts.size();++i){auto& impact=impacts[i];impact.source=source.id;
        impact.tick=1;impact.feature=static_cast<uint32_t>(i);impact.point={0,2,0};impact.speed=10;impact.impulse=100;}
    ASSERT_TRUE(observe(effects,1,source,true,impacts));ASSERT_EQ(effects.instances().size(),512);
    for(auto& impact:impacts)impact.tick=2;
    ASSERT_TRUE(observe(effects,2,source,true,impacts));EXPECT_EQ(effects.stats().dropped,512);
    EXPECT_EQ(effects.stats().highWater,512);EXPECT_EQ(effects.instances().size(),512);
    for(uint64_t tick=3;tick<=180;++tick)ASSERT_TRUE(observe(effects,tick,source));
    EXPECT_TRUE(effects.instances().empty());EXPECT_EQ(effects.stats().active,0);
}

TEST(CoveEffects, EpochResetRetiredHandlesAndLongGapsCannotBridgeOldMotion) {
    Effects effects;ASSERT_TRUE(effects.reset(1,0));auto source=boat();source.pointVelocity={6,0,0};
    ASSERT_TRUE(observe(effects,1,source));source.point.x=1;source.id.body.generation++;
    ASSERT_TRUE(observe(effects,2,source));EXPECT_EQ(effects.stats().emitted,0);
    source.point.x=2;ASSERT_TRUE(observe(effects,30,source));
    EXPECT_EQ(effects.stats().discontinuities,1);EXPECT_EQ(effects.stats().emitted,0);
    EXPECT_FALSE(effects.observe({2,31,true,std::span(&source,1),{}}));
    ASSERT_TRUE(effects.reset(2,31));EXPECT_TRUE(effects.instances().empty());EXPECT_EQ(effects.stats().epoch,2);
    EXPECT_FALSE(effects.reset(0,0));EXPECT_EQ(effects.stats().epoch,2);
}

TEST(CoveEffects, DuplicateSourcesAndInvalidNumbersPreserveExistingPool) {
    Effects effects;ASSERT_TRUE(effects.reset(1,0));auto source=boat();
    const std::array sources{source,source};EXPECT_FALSE(effects.observe({1,1,true,sources,{}}));
    source.pointVelocity.x=std::numeric_limits<double>::infinity();EXPECT_FALSE(observe(effects,1,source));
    EXPECT_EQ(effects.stats().tick,0);EXPECT_TRUE(effects.instances().empty());
}
}
} // namespace voxy::game::expedition
