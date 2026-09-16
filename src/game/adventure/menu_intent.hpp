#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace voxy::game::adventure {
struct MenuIntentChoice {
    std::string identity;
    bool enabled=false;
    bool operator==(const MenuIntentChoice&) const = default;
};
// Tokens name one immutable set of logical choices, not a frame or world tick.
// A raw row index is never an intent. Zero is always invalid; tokens never wrap.
class MenuIntents {
public:
    static constexpr uint32_t maximumToken=(1u<<23)-1;
    static constexpr size_t maximumRows=64;
    [[nodiscard]] bool publish(std::string context,std::vector<MenuIntentChoice> choices) {
        if(exhausted_)return false;
        if(context==context_&&choices==choices_&&token_!=0)return true;
        if(choices.size()>maximumRows||token_==maximumToken){exhausted_=true;return false;}
        context_=std::move(context);choices_=std::move(choices);++token_;return true;
    }
    [[nodiscard]] uint32_t token() const noexcept {return exhausted_?0:token_;}
    void beginFrame() noexcept {activated_=false;}
    [[nodiscard]] bool claimActivation() noexcept {
        if(activated_)return false;
        activated_=true;return true;
    }
    [[nodiscard]] int intent(size_t row) const noexcept {
        if(!token()||row>=choices_.size()||!choices_[row].enabled)return 0;
        return static_cast<int>(token_*64u+static_cast<uint32_t>(row)+1u);
    }
    [[nodiscard]] std::optional<size_t> resolve(int intentValue) const noexcept {
        if(intentValue<=0||!token())return std::nullopt;
        const auto raw=static_cast<uint32_t>(intentValue)-1u;
        const auto row=static_cast<size_t>(raw%64u);
        if(raw/64u!=token_||row>=choices_.size()||!choices_[row].enabled)return std::nullopt;
        return row;
    }
    [[nodiscard]] std::optional<size_t> consume(int intentValue) noexcept {
        const auto row=resolve(intentValue);
        if(!row||!claimActivation())return std::nullopt;
        return row;
    }
private:
    uint32_t token_=0;
    bool exhausted_=false,activated_=false;
    std::string context_;
    std::vector<MenuIntentChoice> choices_;
};
} // namespace voxy::game::adventure
