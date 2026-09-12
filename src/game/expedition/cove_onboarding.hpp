#pragma once

#include <cstdint>
#include <string_view>

namespace voxy::game::expedition {

// Presentation-only learning history. No command, reward or save identity is
// issued by this state machine; facts must come from accepted game state.
class CoveOnboarding {
public:
    enum class Step : uint8_t { Walk, Workshop, Edit, TryBoat, Board, Sail, Save, Finished };
    struct Facts {
        uint64_t epoch=0, tick=0;
        double walkedMetres=0, sailedMetres=0;
        bool workshop=false, keptEdit=false, tested=false, launched=false;
        bool onBoat=false, atHelm=false, saved=false;
    };
    struct Card {
        std::string_view key, title, text, actionLabel;
        int action=0;
        Step step=Step::Walk;
        unsigned number=1, total=7;
        bool complete=false;
    };
    void observe(const Facts&) noexcept;
    void restart() noexcept;
    [[nodiscard]] Step step() const noexcept { return step_; }
    [[nodiscard]] Card card(bool controller, bool paused, bool workshop) const noexcept;
private:
    Step step_=Step::Walk;
    uint64_t epoch_=0,tick_=0;
    bool seen_=false, workshopSeen_=false, editSeen_=false, trySeen_=false;
    bool boardSeen_=false, sailSeen_=false, savedSeen_=false;
    double walked_=0;
};

// Stable keys and English fallback are shared by native and browser. New
// translated catalogs can replace the values without changing tutorial logic.
[[nodiscard]] std::string_view coveUiText(std::string_view key) noexcept;
}
