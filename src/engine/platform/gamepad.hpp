#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace voxy {
// Standard Gamepad API order, independent of native GLFW button indices.
enum class PadButton : uint8_t {
    Confirm, Back, Tool, Alternate, LeftShoulder, RightShoulder,
    LeftTrigger, RightTrigger, View, Menu, LeftStick, RightStick,
    Up, Down, Left, Right, Home, Count
};
struct GamepadSample {
    int device = -1;
    bool connected = false;
    std::array<float,4> axes{}; // left x/y, right x/y; down is positive
    std::array<bool,17> buttons{};
};
// A context change, lost focus or reconnection must see a neutral sample before
// rearming. Confirm never repeats; only navigation repeats, on a bounded clock.
class GamepadInput {
public:
    void disarm() noexcept {
        armed_=false; axes_.fill(0); down_.fill(false); pressed_.fill(false);
        navigation_.fill(false); repeatAt_.fill(0);
    }
    void update(GamepadSample sample, bool focused, double seconds) noexcept {
        pressed_.fill(false); navigation_.fill(false);
        const bool changed=sample.device!=device_ || sample.connected!=connected_;
        device_=sample.device; connected_=sample.connected;
        for(float& axis:sample.axes) {
            if(!std::isfinite(axis)) { sample.connected=false;break; }
            axis=std::clamp(axis,-1.f,1.f);
        }
        if(!sample.connected||!focused||!std::isfinite(seconds)) { disarm();return; }
        if(changed)disarm();
        bool neutral=true;
        for(float axis:sample.axes)neutral=neutral&&std::abs(axis)<=.2f;
        for(bool button:sample.buttons)neutral=neutral&&!button;
        if(!armed_) { if(neutral)armed_=true;return; }
        for(size_t i=0;i<axes_.size();i+=2) {
            const float length=std::hypot(sample.axes[i],sample.axes[i+1]);
            const float gain=length>.2f?(std::min(length,1.f)-.2f)/(.8f*length):0.f;
            axes_[i]=sample.axes[i]*gain; axes_[i+1]=sample.axes[i+1]*gain;
        }
        for(size_t i=0;i<down_.size();++i) {
            pressed_[i]=sample.buttons[i]&&!down_[i]; down_[i]=sample.buttons[i];
        }
        const std::array directions{
            down(PadButton::Up)||axes_[1]<-.55f, down(PadButton::Down)||axes_[1]>.55f,
            down(PadButton::Left)||axes_[0]<-.55f, down(PadButton::Right)||axes_[0]>.55f};
        for(size_t i=0;i<directions.size();++i) {
            if(!directions[i])repeatAt_[i]=0;
            else if(repeatAt_[i]==0) { navigation_[i]=true;repeatAt_[i]=seconds+.4; }
            else if(seconds>=repeatAt_[i]) { navigation_[i]=true;repeatAt_[i]=seconds+.12; }
        }
    }
    [[nodiscard]] bool connected() const noexcept {return connected_;}
    [[nodiscard]] bool armed() const noexcept {return armed_;}
    [[nodiscard]] bool down(PadButton b) const noexcept {return down_[static_cast<size_t>(b)];}
    [[nodiscard]] bool pressed(PadButton b) const noexcept {return pressed_[static_cast<size_t>(b)];}
    [[nodiscard]] float axis(size_t i) const noexcept {return i<axes_.size()?axes_[i]:0;}
    [[nodiscard]] bool navigation(size_t i) const noexcept {return i<navigation_.size()&&navigation_[i];}
private:
    int device_=-1;
    bool connected_=false,armed_=false;
    std::array<float,4> axes_{};
    std::array<bool,17> down_{},pressed_{};
    std::array<bool,4> navigation_{};
    std::array<double,4> repeatAt_{};
};
}
