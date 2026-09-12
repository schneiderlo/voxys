#pragma once
#include "game/expedition/cove_input_preferences.hpp"
#include "game/expedition/cove_onboarding.hpp"
#include <filesystem>
#include <glm/vec3.hpp>
#include <optional>
#include <vector>

namespace voxy {
// Local presentation preferences and learning history. Kept outside the
// expedition ownership/save model and never used to award progress.
struct CoveUiState {
    game::expedition::CoveInputPreferences preferences;
    game::expedition::CoveInputRouter input;
    game::expedition::CoveOnboarding tutorial;
    std::filesystem::path preferencesPath;
    std::string preferenceStatus;
    bool saveRequested=false,pauseMenuRequested=false,saveConfirmed=false;
    bool preferencesLoaded=false,orbitLatched=false;
    uint64_t observedTick=0,observedEpoch=0;
    glm::dvec3 previousFeet{},previousBoat{};
    double walkedMetres=0,sailedMetres=0;
    bool previousOnBoat=false,previousHelm=false;
    uint64_t previousInteractions=0;
    std::string caption;
    uint64_t captionTick=0,previousLaunches=0,previousTests=0,previousReturns=0;
    int previousMode=-1;
    bool previousTow=false,previousTowBroken=false,previousDelivered=false;

    std::vector<std::pair<std::string,std::string>> worlds;
    std::string currentWorld,worldStatus;
    std::optional<std::string> worldRequest;
    bool worldPreparing=false,hostBusy=false,exitAfterLeave=false;
};
}
