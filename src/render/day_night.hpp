#pragma once

#include <algorithm>
#include <cmath>
#include <string_view>
#include <glm/glm.hpp>

namespace voxy::render {

inline bool isDayNightDrivenSetting(std::string_view name) noexcept {
    return name == "lighting.sunAzimuth" || name == "lighting.sunElevation"
        || name == "lighting.sunColor.r" || name == "lighting.sunColor.g"
        || name == "lighting.sunColor.b" || name == "lighting.sunIntensity"
        || name == "lighting.ambientColor.r" || name == "lighting.ambientColor.g"
        || name == "lighting.ambientColor.b" || name == "lighting.ambientIntensity"
        || name == "lighting.fogColor.r" || name == "lighting.fogColor.g"
        || name == "lighting.fogColor.b" || name == "lighting.exposure";
}

// The clock is bounded and double precision; simulation stalls and paused
// frames must not fast-forward a quiet building session.
inline double wrapDayHour(double hour) noexcept {
    if (!std::isfinite(hour)) return 9.0;
    return hour - std::floor(hour / 24.0) * 24.0;
}

inline double advanceDayHour(double hour, double seconds, double cycleMinutes) noexcept {
    hour = wrapDayHour(hour);
    if (!std::isfinite(seconds) || seconds <= 0.0
        || !std::isfinite(cycleMinutes) || cycleMinutes < 1.0) return hour;
    return wrapDayHour(hour + std::min(seconds, 0.25) * 24.0 / (cycleMinutes * 60.0));
}

struct DayNightLighting {
    glm::vec3 sunDirection;
    glm::vec3 lightDirection;
    glm::vec3 lightColor;
    glm::vec3 ambientColor;
    glm::vec3 fogColor;
    float intensity;
    float ambientIntensity;
    float exposure;
    float daylight;
};

inline DayNightLighting sampleDayNight(double hour) noexcept {
    constexpr float tau = 6.28318530718f;
    const float angle = static_cast<float>((wrapDayHour(hour) - 6.0) / 24.0) * tau;
    const glm::vec3 sun = glm::normalize(glm::vec3(
        std::cos(angle), std::sin(angle) * 0.88f, std::sin(angle) * 0.48f));
    const float daylight = glm::smoothstep(-0.12f, 0.22f, sun.y);
    const float golden = 1.0f - glm::smoothstep(0.08f, 0.55f, sun.y);
    const float sunlight = glm::smoothstep(-0.035f, 0.16f, sun.y);
    const float moonlight = 1.0f - glm::smoothstep(-0.22f, -0.04f, sun.y);
    // Change shadow direction only in the unlit interval around the horizon.
    // No slerp between antipodal vectors, and no light shining from below ground.
    const bool moon = sun.y < -0.04f;
    return {
        sun, moon ? -sun : sun,
        moon ? glm::vec3(0.60f, 0.73f, 1.0f)
             : glm::mix(glm::vec3(1.0f, 0.95f, 0.83f), glm::vec3(1.0f, 0.48f, 0.23f), golden),
        glm::mix(glm::vec3(0.32f, 0.38f, 0.55f), glm::vec3(0.62f, 0.64f, 0.72f), daylight),
        glm::mix(glm::vec3(0.025f, 0.040f, 0.080f),
                 glm::mix(glm::vec3(0.57f, 0.71f, 0.80f), glm::vec3(0.52f, 0.28f, 0.22f), golden), daylight),
        moon ? 0.28f * moonlight : glm::mix(1.25f, 1.7f, golden) * sunlight,
        glm::mix(0.60f, 0.65f, daylight),
        glm::mix(1.25f, 1.10f, daylight), daylight
    };
}

} // namespace voxy::render
