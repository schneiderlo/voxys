// ═══════════════════════════════════════════════════════════════════════════════
// bike.hpp - RIDGEBREAK motorcycle dynamics
// ═══════════════════════════════════════════════════════════════════════════════
// A deterministic CPU motorcycle model run at a fixed 60 Hz. The chassis is a
// single rigid body; the front and rear wheels are contact points with
// suspension travel. Terrain is sampled through a height callback so the sim
// never owns the heightmap and works identically on native and WASM.

#pragma once

#include <functional>
#include <cstdint>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace voxy::moto {

struct BikeConfig {
    // Masses and geometry (SI units, metres).
    float chassisMass = 152.0f;
    float wheelbase = 1.42f;
    float wheelRadius = 0.31f;
    float rearWheelMass = 9.0f;
    float frontWheelMass = 6.0f;
    // Effective rotational inertia around each axle. These include the tyre,
    // rim, brake rotor, sprocket, and a small reflected driveline component.
    float rearWheelInertia = 0.88f;   // kg*m^2
    float frontWheelInertia = 0.60f;
    // Chassis-local anchor points of the wheel contact bases. Y is the
    // suspension base above the wheel center; suspension compresses from here.
    glm::vec3 rearAnchorLocal{0.0f, 0.0f, -0.62f};
    glm::vec3 frontAnchorLocal{0.0f, 0.0f, 0.80f};
    float frontSuspensionTravel = 0.27f;
    float rearSuspensionTravel = 0.31f;
    // Suspension spring/damper.
    float frontSuspensionStiffness = 24000.0f;   // N/m
    float frontSuspensionDamping = 1300.0f;      // N/(m/s)
    float rearSuspensionStiffness = 32000.0f;
    float rearSuspensionDamping = 1700.0f;
    // Inertia (kg*m^2) of the combined rider + chassis.
    glm::vec3 chassisInertia{28.0f, 48.0f, 22.0f};
    // Powertrain.
    float engineMaxTorque = 96.0f;    // N*m at the crank
    float peakRPM = 8500.0f;
    float redlineRPM = 11000.0f;
    float idleRPM = 1400.0f;
    float primaryRatio = 2.0f;
    float finalDriveRatio = 3.0f;
    float gearRatios[6] = {2.46f, 1.85f, 1.50f, 1.25f, 1.08f, 0.96f};
    uint32_t gearCount = 5;
    float rearBrakeTorque = 900.0f;
    float frontBrakeTorque = 1400.0f;
    float brakeBias = 0.6f;  // fraction of braking on the front wheel
    // Grip and traction.
    float rearGrip = 2.3f;
    float frontGrip = 2.0f;
    float maxDriveForce = 2600.0f;   // N cap from engine at the contact patch
    float longitudinalSlipPeak = 0.16f;
    float slipReferenceSpeed = 1.5f; // m/s; regularizes launch/near-lock slip
    float wheelAngularDamping = 0.12f; // bearing/aero decay, 1/s
    float dragCoefficient = 0.42f;   // N per (m/s)^2
    float rollingResistance = 0.018f;
    // Steering.
    float steeringMaxAngle = 0.48f;  // rad
    float steeringSpeed = 6.0f;      // rad/s at full input
    float steeringSelfCenter = 3.5f; // 1/s restoring
    // Rider weight shift influence on the chassis torque.
    float leanTorque = 900.0f;       // N*m at full lean input
    // Wheelie / stoppie.
    float wheelieTorque = 1400.0f;   // N*m while seated and accelerating
    float stoppieTorque = 1300.0f;
    // Air control is intentionally weaker than grounded rider leverage. These
    // torques let a skilled rider correct pitch, roll, and whip angle without
    // making the bike feel weightless.
    float airPitchTorque = 520.0f;
    float airRollTorque = 390.0f;
    float airYawTorque = 780.0f;
    float airAngularDamping = 22.0f;
    float landingAssistTorque = 180.0f;
    // Recovery timing. A player can request an earlier remount after the bike
    // has stopped, while the timeout prevents a lost bike from blocking play.
    float groundedBeforeRemount = 0.65f;
    float automaticRemountDelay = 3.75f;
    float remountDuration = 0.85f;
    float barrierCrashSpeed = 12.0f;  // m/s normal impact speed
    float barrierRestitution = 0.08f;

    [[nodiscard]] float gearRatio(uint32_t gear) const noexcept {
        const uint32_t clamped = (gear >= 1u && gear <= gearCount) ? gear : 1u;
        return gearRatios[clamped - 1u];
    }
};

struct BikeInput {
    float throttle = 0.0f;  // [0, 1]
    float brake = 0.0f;     // [0, 1]
    float steer = 0.0f;     // [-1, 1], positive = right
    float lean = 0.0f;      // [-1, 1], rider weight shift
    bool seated = true;     // true = seated, false = standing
    bool duck = false;      // crouch, lowers center of mass
    bool shiftUp = false;   // edge-triggered
    bool shiftDown = false; // edge-triggered
    bool remount = false;   // edge-triggered; accepted once the bike is down
};

enum class CrashState : uint8_t {
    Riding = 0,
    HighSided = 1,
    WipedOut = 2,
    OnGround = 3,
    Remounting = 4,
};

enum class LandingQuality : uint8_t {
    None = 0,
    Sketchy = 1,
    Clean = 2,
    Perfect = 3,
    Crashed = 4,
};

/// Bits may be combined when one jump contains several rotations.
enum class TrickFlags : uint32_t {
    None = 0u,
    Whip = 1u << 0u,
    Backflip = 1u << 1u,
    Frontflip = 1u << 2u,
    BarrelRoll = 1u << 3u,
};

struct BikeState {
    glm::vec3 chassisPosition{0.0f, 1.0f, 0.0f};
    glm::quat chassisOrientation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 chassisLinearVelocity{0.0f};
    glm::vec3 chassisAngularVelocity{0.0f};
    float frontSuspension = 0.0f;   // 0..frontSuspensionTravel
    float rearSuspension = 0.0f;
    float steeringAngle = 0.0f;
    float frontWheelSpin = 0.0f;    // radians
    float rearWheelSpin = 0.0f;
    float frontWheelAngularVelocity = 0.0f; // rad/s, forward rotation positive
    float rearWheelAngularVelocity = 0.0f;
    float engineRPM = 1400.0f;
    float speed = 0.0f;             // forward speed along heading
    float throttle = 0.0f;
    uint32_t gear = 1;
    bool frontTouch = false;
    bool rearTouch = false;
    bool airborne = true;
    float airTime = 0.0f;
    float lastAirTime = 0.0f;
    // Signed rotations accumulated in chassis-local pitch/yaw/roll axes for
    // the current jump. They are gameplay telemetry, not visual-only values.
    glm::vec3 aerialRotation{0.0f};
    uint32_t aerialTricks = 0u;
    uint32_t lastLandedTricks = 0u;
    LandingQuality lastLanding = LandingQuality::None;
    uint32_t lastTrickPoints = 0u;
    uint64_t trickScore = 0u;
    uint32_t comboCount = 0u;
    uint32_t bestCombo = 0u;
    uint32_t tricksLanded = 0u;
    float totalDistance = 0.0f;
    CrashState crash = CrashState::Riding;
    float crashTime = 0.0f;
    float remountTime = 0.0f;
    float recoveryHeading = 0.0f;
    float barrierImpactSpeed = 0.0f;
};

/// Motorcycle dynamics. One instance per player. All math is scalar float;
/// determinism comes from the fixed step and a stable evaluation order.
class Bike {
public:
    explicit Bike(const BikeConfig& config = {});
    ~Bike();

    Bike(const Bike&) = delete;
    Bike& operator=(const Bike&) = delete;

    /// Reset to a pose. yaw is the heading in radians.
    void reset(const glm::vec3& position, float yaw);

    /// Advance one fixed step. heightAt returns the terrain world height at a
    /// world (x, z); the sim clamps its own travel and never calls it with
    /// non-finite coordinates.
    void step(const BikeInput& input,
              const std::function<float(float x, float z)>& heightAt,
              float waterHeight,
              float dt);

    /// Advance one fixed step with a default terrain height function.
    void step(const BikeInput& input, float flatHeight, float waterHeight,
              float dt);

    [[nodiscard]] const BikeState& state() const noexcept { return state_; }
    [[nodiscard]] const BikeConfig& config() const noexcept { return config_; }

private:
    void integrate(float dt);

    BikeConfig config_;
    BikeState state_;
};

}  // namespace voxy::moto
