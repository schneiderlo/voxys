#include "game/lego_playground.hpp"
#include "physics/physics_world.hpp"
#include "render/primitive_material.hpp"
#include <algorithm>
#include <cmath>
#include <glm/gtc/quaternion.hpp>

namespace voxy::game {
namespace {
constexpr uint32_t TargetSlot =
    LegoPlayground::MaxBricks + LegoPlayground::MaxBalls;
constexpr float Height =
    LegoPlayground::BrickHeight + LegoPlayground::StudHeight;
constexpr std::array<glm::vec2, 4> Sizes{{{1, 1}, {2, 2}, {2, 4}, {4, 2}}};
constexpr std::array<glm::vec3, 5> Colors{{{.64f, .28f, .16f},
                                           {.85f, .64f, .26f},
                                           {.32f, .49f, .25f},
                                           {.75f, .60f, .40f},
                                           {.25f, .43f, .46f}}};
bool finite(glm::vec3 v) {
  return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}
} // namespace
struct LegoPlayground::Impl {
  struct Piece {
    physics::BodyHandle id{};
    glm::vec2 size{1};
    glm::vec3 position{
        0}; // Relative to the playground origin; GPU is authoritative.
    glm::quat rotation{1, 0, 0, 0};
    bool awake = true, observed = false;
    float age = 0;
    uint64_t tick = 0;
  };
  struct Dust {
    glm::vec3 position{0}, velocity{0};
    float life = 0;
  };
  physics::PhysicsWorld *world = nullptr;
  physics::BodyHandle floor{};
  std::array<Piece, TargetSlot + 1> pieces{};
  std::array<Dust, MaxDust> dust{};
  std::array<physics::DynamicBodySnapshot, MaxDust + 8> render{};
  std::array<uint32_t, MaxBricks> ids{};
  glm::vec3 worldOrigin{0}, candidate{0}, targetStart{0};
  State status;
  uint32_t selected = 2, dustCursor = 0, ballCursor = 0;
  float snapshotDebt = 0, stableTime = 0, dustCooldown = 0, shotCooldown = 0;
  bool ballHit = false, previewVisible = false;
  ~Impl() {
    if (world) {
      for (auto &p : pieces)
        if (p.id.valid())
          static_cast<void>(world->destroyBody(p.id));
      if (floor.valid())
        static_cast<void>(world->destroyBody(floor));
      world->setEventReadbackEnabled(false);
    }
  }
  physics::BodyHandle add(physics::ThrowableShape shape, glm::vec3 position,
                          glm::vec3 dimensions, float mass, glm::vec3 color,
                          bool brick = false, glm::vec3 velocity = {0, 0, 0}) {
    physics::BodySpawnDesc desc;
    desc.shape = shape;
    desc.position = position + worldOrigin;
    desc.dimensions = dimensions;
    desc.inverseMass = mass > 0 ? 1.f / mass : 0.f;
    desc.linearVelocity = velocity;
    desc.bullet = shape == physics::ThrowableShape::Sphere;
    physics::PhysicsMaterial material;
    material.friction = .8f;
    material.restitution = .05f;
    material.rollingResistance = .03f;
    material.flags = render::packPrimitiveMaterial({color, .38f, 0.f, true});
    if (brick)
      material.flags =
          (material.flags & 0x0fffffffu) | physics::kLegoBrickMaterial;
    desc.material = material;
    return world->spawnBody(desc);
  }
  void remove(uint32_t i) {
    auto &p = pieces[i];
    if (p.id.valid())
      static_cast<void>(world->destroyBody(p.id));
    p = {};
  }
  bool upright(uint32_t i) const {
    const auto &q = pieces[i].rotation;
    return glm::dot(q * glm::vec3(0, 1, 0), glm::vec3(0, 1, 0)) > .998f &&
           std::abs(q.y) < .03f;
  }
  bool snap(float x, float z) {
    status.previewValid = false;
    if (!std::isfinite(x) || !std::isfinite(z))
      return false;
    const auto size = Sizes[selected];
    x = std::round(x - size.x * .5f) + size.x * .5f;
    z = std::round(z - size.y * .5f) + size.y * .5f;
    candidate = {x, Height * .5f, z};
    previewVisible = std::abs(x) < 12.f && std::abs(z) < 12.f;
    if (std::abs(x) + size.x * .5f > 8.f || std::abs(z) + size.y * .5f > 8.f ||
        status.bricks >= MaxBricks)
      return false;
    float support = 0;
    for (uint32_t i = 0; i < MaxBricks; ++i) {
      const auto &p = pieces[i];
      if (!p.id.valid())
        continue;
      if (std::abs(p.position.x - x) < (p.size.x + size.x) * .5f - .04f &&
          std::abs(p.position.z - z) < (p.size.y + size.y) * .5f - .04f) {
        if (!p.observed || p.awake || !upright(i))
          return false;
        support = std::max(support, p.position.y + Height * .5f);
      }
    }
    candidate.y = support + Height * .5f + .003f;
    if (candidate.y > 10.f)
      return false;
    if (pieces[TargetSlot].id.valid() &&
        glm::length(candidate - pieces[TargetSlot].position) < 2.f)
      return false;
    status.previewValid = true;
    return true;
  }
  int slot(physics::BodyHandle id) const {
    for (uint32_t i = 0; i < pieces.size(); ++i)
      if (pieces[i].id == id && id.valid())
        return int(i);
    return -1;
  }
};
LegoPlayground::LegoPlayground() = default;
LegoPlayground::~LegoPlayground() = default;
bool LegoPlayground::initialize(physics::PhysicsWorld &world,
                                glm::vec3 origin) {
  if (!finite(origin) || !world.isInitialized() ||
      world.backendType() != physics::BackendType::WebGpuSoft)
    return false;
  auto p = std::make_unique<Impl>();
  p->world = &world;
  p->worldOrigin = origin;
  p->floor = p->add(physics::ThrowableShape::Box, {0, -.32f, 0}, {18, .64f, 18},
                    0, {.21f, .34f, .27f});
  if (!p->floor.valid() || !world.setEventReadbackEnabled(true))
    return false;
  impl_ = std::move(p);
  return true;
}
void LegoPlayground::reset() {
  if (!impl_)
    return;
  auto &p = *impl_;
  for (uint32_t i = 0; i <= TargetSlot; ++i)
    p.remove(i);
  p.dust = {};
  p.status = {};
  p.stableTime = p.shotCooldown = 0;
  p.ballHit = false;
}
void LegoPlayground::select(uint32_t shape) {
  if (impl_)
    impl_->selected = std::min(shape, 2u);
}
void LegoPlayground::rotate() {
  if (impl_ && impl_->selected >= 2)
    impl_->selected = 5 - impl_->selected;
}
glm::vec3 LegoPlayground::origin() const {
  return impl_ ? impl_->worldOrigin : glm::vec3(0);
}
void LegoPlayground::preview(glm::vec3 eye, glm::vec3 direction) {
  if (!impl_)
    return;
  auto &p = *impl_;
  p.status.previewValid = false;
  p.previewVisible = false;
  if (!finite(eye) || !finite(direction) || glm::length(direction) < .1f)
    return;
  eye -= p.worldOrigin;
  direction = glm::normalize(direction);
  // Construction uses horizontal snap planes from acknowledged sleeping
  // bricks. No GPU round trip is needed for each cursor movement.
  if (direction.y >= -.01f)
    return;
  float best = -eye.y / direction.y;
  if (best < 0 || best > 60.f)
    return;
  glm::vec3 point = eye + direction * best;
  for (uint32_t i = 0; i < MaxBricks; ++i) {
    const auto &b = p.pieces[i];
    if (!b.id.valid() || !b.observed || !p.upright(i))
      continue;
    const float t = (b.position.y + Height * .5f - eye.y) / direction.y;
    const auto hit = eye + direction * t;
    if (t >= 0 && t < best &&
        std::abs(hit.x - b.position.x) <= b.size.x * .5f &&
        std::abs(hit.z - b.position.z) <= b.size.y * .5f) {
      best = t;
      point = b.position;
    }
  }
  p.snap(point.x, point.z);
}
bool LegoPlayground::placeAt(float x, float z) {
  if (!impl_ || !impl_->snap(x, z))
    return false;
  return place();
}
bool LegoPlayground::place() {
  if (!impl_ || !impl_->status.previewValid)
    return false;
  auto &p = *impl_;
  if (!p.snap(p.candidate.x, p.candidate.z))
    return false;
  for (uint32_t i = 0; i < MaxBricks; ++i)
    if (!p.pieces[i].id.valid()) {
      const auto size = Sizes[p.selected];
      const auto id =
          p.add(physics::ThrowableShape::Box, p.candidate,
                {size.x - .04f, Height, size.y - .04f}, size.x * size.y * .35f,
                Colors[i % Colors.size()], true);
      if (!id.valid())
        return false;
      auto &b = p.pieces[i];
      b = {};
      b.id = id;
      b.size = size;
      b.position = p.candidate;
      ++p.status.bricks;
      ++p.status.awake;
      ++p.status.clicks;
      p.status.previewValid = false;
      return true;
    }
  return false;
}
bool LegoPlayground::launch(glm::vec3 eye, glm::vec3 direction) {
  if (!impl_ || !finite(eye) || !finite(direction) ||
      glm::length(direction) < .1f)
    return false;
  auto &p = *impl_;
  if (p.shotCooldown > 0)
    return false;
  eye -= p.worldOrigin;
  if (glm::length(eye) > 60.f)
    return false;
  direction = glm::normalize(direction);
  const uint32_t slot = MaxBricks + p.ballCursor;
  p.ballCursor = (p.ballCursor + 1) % MaxBalls;
  if (p.pieces[slot].id.valid() && p.pieces[slot].age < 2.f)
    return false;
  p.remove(slot);
  const auto pos = eye + direction * 1.2f;
  auto id = p.add(physics::ThrowableShape::Sphere, pos, glm::vec3(.76f), 5.f,
                  {.88f, .72f, .30f}, false, direction * 24.f);
  if (!id.valid())
    return false;
  auto &b = p.pieces[slot];
  b.id = id;
  b.position = pos;
  p.shotCooldown = .3f;
  ++p.status.clicks;
  return true;
}
void LegoPlayground::step(float dt) {
  if (!impl_ || !std::isfinite(dt) || dt <= 0)
    return;
  auto &p = *impl_;
  dt = std::min(dt, .05f);
  p.shotCooldown = std::max(0.f, p.shotCooldown - dt);
  p.dustCooldown -= dt;
  // Read only this island's ID interval, capped by its 58 owned bodies plus
  // the scene's bounded balls. The GPU continues to render resident poses.
  if (auto snapshot = p.world->pollDebugSnapshot()) {
    for (const auto &body : snapshot->bodies) {
      const int i = p.slot(body.handle);
      if (i < 0 || !body.alive)
        continue;
      auto &b = p.pieces[size_t(i)];
      if (snapshot->tick <= b.tick)
        continue;
      if (b.observed && body.awake && !b.awake)
        ++p.status.wakeTransitions;
      if (!body.awake && b.awake)
        ++p.status.sleepTransitions;
      b.observed = true;
      b.awake = body.awake;
      b.tick = snapshot->tick;
      b.position = body.position +
                   glm::vec3(body.sector) * physics::kWorldSectorSize -
                   p.worldOrigin;
      b.rotation = body.orientation;
    }
  }
  p.snapshotDebt += dt;
  if (p.snapshotDebt >= .1f) {
    p.snapshotDebt = 0;
    uint32_t first = p.floor.index, last = first;
    for (const auto &b : p.pieces)
      if (b.id.valid()) {
        first = std::min(first, b.id.index);
        last = std::max(last, b.id.index);
      }
    p.world->requestDebugSnapshot({first, std::min(last - first + 1, 256u)});
  }
  if (auto events = p.world->pollEvents())
    for (const auto &e : events->events) {
      if (e.type != physics::PhysicsEventType::ContactHit ||
          e.impactSpeed < .8f)
        continue;
      const int a = p.slot(e.bodyHandleA()), b = p.slot(e.bodyHandleB());
      if (a < 0 && b < 0)
        continue;
      ++p.status.impacts;
      const auto ball = [](int i) {
        return i >= int(MaxBricks) && i < int(TargetSlot);
      };
      if ((ball(a) && b >= 0) || (ball(b) && a >= 0))
        p.ballHit = true;
      if (p.dustCooldown <= 0) {
        const int i = a >= 0 ? a : b;
        const auto &piece = p.pieces[size_t(i)];
        const glm::vec3 point =
            piece.position +
            piece.rotation * (a >= 0 ? e.localAnchorA : e.localAnchorB);
        for (uint32_t n = 0; n < 5; ++n) {
          const float angle = float(p.dustCursor) * 2.4f;
          auto &d = p.dust[p.dustCursor++ % MaxDust];
          d = {point,
               {std::cos(angle) * .8f, .8f + float(n) * .12f,
                std::sin(angle) * .8f},
               .45f};
        }
        p.dustCooldown = .1f;
      }
    }
  p.status.awake = p.status.sleeping = p.status.balls = p.status.bricks = 0;
  float highest = 0;
  uint32_t top = 0;
  for (uint32_t i = 0; i <= TargetSlot; ++i) {
    auto &b = p.pieces[i];
    if (!b.id.valid())
      continue;
    b.age += dt;
    if (i != TargetSlot && b.observed &&
        (b.position.y < -12 || glm::length(b.position) > 45.f)) {
      p.remove(i);
      continue;
    }
    b.awake ? ++p.status.awake : ++p.status.sleeping;
    if (i < MaxBricks) {
      ++p.status.bricks;
      if (b.observed && p.upright(i) && b.position.y + Height * .5f > highest) {
        highest = b.position.y + Height * .5f;
        top = i;
      }
    } else if (i < TargetSlot)
      ++p.status.balls;
  }
  p.status.levels =
      uint32_t(std::max(0.f, std::floor((highest + .05f) / Height)));
  if (p.status.phase == 0) {
    if (p.status.levels >= 3 && p.status.bricks >= 3 && p.status.awake == 0)
      p.stableTime += dt;
    else
      p.stableTime = 0;
    if (p.stableTime > .6f) {
      auto pos = p.pieces[top].position;
      pos.y = highest + .325f + .005f;
      auto id = p.add(physics::ThrowableShape::Box, pos,
                      glm::vec3(1.3f, .65f, 1.3f), .7f, {.85f, .15f, .08f});
      if (id.valid()) {
        auto &b = p.pieces[TargetSlot];
        b = {};
        b.id = id;
        b.position = pos;
        p.targetStart = pos;
        p.status.phase = 1;
        p.ballHit = false;
      }
    }
  }
  if (p.pieces[TargetSlot].id.valid()) {
    const auto pos = p.pieces[TargetSlot].position;
    p.status.target = pos + p.worldOrigin;
    if (p.status.phase == 1 && p.ballHit &&
        (glm::length(pos - p.targetStart) > 1.4f ||
         p.targetStart.y - pos.y > BrickHeight)) {
      p.status.phase = 2;
      ++p.status.clicks;
    }
  }
  p.status.dust = 0;
  for (auto &d : p.dust)
    if (d.life > 0) {
      d.life -= dt;
      d.velocity.y -= 4.f * dt;
      d.position += d.velocity * dt;
      ++p.status.dust;
    }
}
uint32_t LegoPlayground::residentCount() const {
  if (!impl_) return 0;
  uint32_t count = impl_->floor.valid() ? 1 : 0;
  for (const auto &piece : impl_->pieces) count += piece.id.valid();
  return count;
}
LegoPlayground::State LegoPlayground::state() const {
  return impl_ ? impl_->status : State{};
}
glm::vec3 LegoPlayground::brickPosition(uint32_t i) const {
  return impl_ && i < MaxBricks && impl_->pieces[i].id.valid()
             ? impl_->pieces[i].position
             : glm::vec3(0);
}
std::span<const uint32_t> LegoPlayground::bodyIds() {
  if (!impl_)
    return {};
  auto &p = *impl_;
  size_t count = 0;
  for (uint32_t i = 0; i < MaxBricks; ++i)
    if (p.pieces[i].id.valid())
      p.ids[count++] = p.pieces[i].id.index;
  return {p.ids.data(), count};
}
std::span<const physics::DynamicBodySnapshot> LegoPlayground::instances() {
  if (!impl_)
    return {};
  auto &p = *impl_;
  size_t count = 0;
  const auto emit = [&](physics::ThrowableShape shape, glm::vec3 pos,
                        glm::vec3 dims, glm::vec4 color) {
    auto &r = p.render[count++];
    r = {shape, pos + p.worldOrigin, {1, 0, 0, 0}, dims};
    r.color = color;
  };
  const auto size = Sizes[p.selected];
  const auto color = p.status.previewValid ? glm::vec4(.35f, .85f, .60f, 1)
                                           : glm::vec4(.9f, .24f, .18f, 1);
  if (p.previewVisible && p.status.phase != 2)
    for (float y : {-Height * .5f, Height * .5f})
      for (float sign : {-1.f, 1.f}) {
        emit(physics::ThrowableShape::Cube,
             p.candidate + glm::vec3(0, y, sign * size.y * .5f),
             {size.x, .035f, .035f}, color);
        emit(physics::ThrowableShape::Cube,
             p.candidate + glm::vec3(sign * size.x * .5f, y, 0),
             {.035f, .035f, size.y}, color);
      }
  for (const auto &d : p.dust)
    if (d.life > 0)
      emit(physics::ThrowableShape::Sphere, d.position,
           glm::vec3(.09f * std::clamp(d.life / .25f, 0.f, 1.f)),
           {.72f, .65f, .50f, 1});
  return {p.render.data(), count};
}
} // namespace voxy::game
