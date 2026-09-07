#pragma once
#include "physics/physics_types.hpp"
#include <array>
#include <memory>
#include <span>

namespace voxy::physics {
class PhysicsWorld;
}
namespace voxy::game {
// A bounded island in the existing GPU world. The landscape remains a
// heightmap. Collision uses the same box + cylindrical studs emitted for
// rendering.
class LegoPlayground {
public:
  static constexpr uint32_t MaxBricks = 48, MaxBalls = 8, MaxDust = 48;
  static constexpr float BrickHeight = .96f, StudHeight = .18f,
                         StudRadius = .30f;
  struct State {
    uint32_t bricks = 0, balls = 0, awake = 0, sleeping = 0, levels = 0;
    uint32_t sleepTransitions = 0, wakeTransitions = 0, impacts = 0;
    uint32_t phase = 0; // 0 build, 1 target ready, 2 success
    uint32_t clicks = 0, dust = 0;
    bool previewValid = false;
    glm::vec3 target{0};
  };
  LegoPlayground();
  ~LegoPlayground();
  bool initialize(physics::PhysicsWorld &world, glm::vec3 worldOrigin);
  void reset();
  void select(uint32_t shape);
  void rotate();
  void preview(glm::vec3 eye, glm::vec3 direction);
  bool place();
  bool placeAt(float x,
               float z); // Same snapping/validation as pointer placement.
  bool launch(glm::vec3 eye, glm::vec3 direction);
  void step(float deltaTime);
  [[nodiscard]] State state() const;
  [[nodiscard]] uint32_t residentCount() const;
  [[nodiscard]] glm::vec3 origin() const;
  [[nodiscard]] std::span<const physics::DynamicBodySnapshot> instances();
  [[nodiscard]] std::span<const uint32_t> bodyIds();
  [[nodiscard]] glm::vec3 brickPosition(uint32_t index) const;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
} // namespace voxy::game
