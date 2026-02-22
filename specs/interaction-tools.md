# Interaction & Tools Specification

> **Project:** voxy  
> **Version:** 1.0  
> **Status:** Draft

---

## Overview

This document specifies the user interaction systems for voxy, including camera controls, character movement, and terrain editing tools.

---

## 1. Free-Fly Camera

The free-fly camera is the primary navigation mode for exploring the terrain.

### 1.1 Controls

| Action | Keyboard | Mouse |
|--------|----------|-------|
| Move Forward | W | - |
| Move Backward | S | - |
| Move Left | A | - |
| Move Right | D | - |
| Move Up | E / Space | - |
| Move Down | Q / Ctrl | - |
| Look Around | - | Mouse Move (when captured) |
| Speed Boost | Shift | - |
| Capture Mouse | - | Left Click on canvas |
| Release Mouse | Escape | - |

### 1.2 Camera Uniforms

```cpp
struct CameraState {
    glm::vec3 position;      // World-space position
    glm::vec3 forward;       // Normalized forward direction
    glm::vec3 right;         // Normalized right direction
    glm::vec3 up;            // Normalized up direction
    float yaw;               // Horizontal angle (radians)
    float pitch;             // Vertical angle (radians)
    float fovY;              // Vertical field of view (radians)
    float aspectRatio;       // Width / Height
    float nearPlane;         // Near clip plane
    float farPlane;          // Far clip plane
};
```

### 1.3 Matrix Computation

```cpp
glm::mat4 computeViewMatrix(const CameraState& cam) {
    return glm::lookAt(cam.position, cam.position + cam.forward, cam.up);
}

glm::mat4 computeProjectionMatrix(const CameraState& cam) {
    return glm::perspective(cam.fovY, cam.aspectRatio, cam.nearPlane, cam.farPlane);
}

glm::mat4 computeViewProjection(const CameraState& cam) {
    return computeProjectionMatrix(cam) * computeViewMatrix(cam);
}
```

### 1.4 Movement Implementation

```cpp
void FreeFlyCamera::update(float deltaTime, const Input& input) {
    // Mouse look (when captured)
    if (input.isMouseCaptured()) {
        float sensitivity = 0.002f;
        yaw_ -= input.mouseDeltaX() * sensitivity;
        pitch_ -= input.mouseDeltaY() * sensitivity;
        pitch_ = glm::clamp(pitch_, -glm::half_pi<float>() + 0.01f,
                                     glm::half_pi<float>() - 0.01f);
    }
    
    // Recompute orientation vectors
    forward_.x = cos(pitch_) * sin(yaw_);
    forward_.y = sin(pitch_);
    forward_.z = cos(pitch_) * cos(yaw_);
    forward_ = glm::normalize(forward_);
    
    right_ = glm::normalize(glm::cross(forward_, glm::vec3(0, 1, 0)));
    up_ = glm::normalize(glm::cross(right_, forward_));
    
    // Movement
    float speed = baseSpeed_ * (input.isKeyDown(Key::Shift) ? boostMultiplier_ : 1.0f);
    glm::vec3 velocity(0.0f);
    
    if (input.isKeyDown(Key::W)) velocity += forward_;
    if (input.isKeyDown(Key::S)) velocity -= forward_;
    if (input.isKeyDown(Key::A)) velocity -= right_;
    if (input.isKeyDown(Key::D)) velocity += right_;
    if (input.isKeyDown(Key::E) || input.isKeyDown(Key::Space)) velocity += glm::vec3(0, 1, 0);
    if (input.isKeyDown(Key::Q) || input.isKeyDown(Key::Ctrl)) velocity -= glm::vec3(0, 1, 0);
    
    if (glm::length(velocity) > 0.0f) {
        velocity = glm::normalize(velocity) * speed * deltaTime;
        position_ += velocity;
    }
}
```

### 1.5 Configuration

| Parameter | Default | Range | Description |
|-----------|---------|-------|-------------|
| `baseSpeed` | 50.0 | 1-1000 | Units per second |
| `boostMultiplier` | 5.0 | 1-20 | Speed multiplier when Shift held |
| `mouseSensitivity` | 0.002 | 0.0001-0.01 | Radians per pixel |
| `fovY` | 60° | 30-120 | Vertical FOV |
| `nearPlane` | 0.1 | 0.01-10 | Near clip distance |
| `farPlane` | 10000 | 100-100000 | Far clip distance |

---

## 2. Character Controller (Future Milestone)

A grounded character controller for first-person exploration.

### 2.1 State Machine

```
        ┌──────────────┐
        │   Grounded   │◄────────────────┐
        └──────┬───────┘                 │
               │ Jump                    │ Land
               ▼                         │
        ┌──────────────┐                 │
        │    Jumping   │─────────────────┤
        └──────┬───────┘                 │
               │ Apex                    │
               ▼                         │
        ┌──────────────┐                 │
        │   Falling    │─────────────────┘
        └──────────────┘
```

### 2.2 Movement Parameters

| Parameter | Value | Description |
|-----------|-------|-------------|
| `walkSpeed` | 4.0 | Walking speed (units/sec) |
| `runSpeed` | 8.0 | Running speed (units/sec) |
| `jumpHeight` | 2.0 | Maximum jump height (units) |
| `gravity` | 20.0 | Downward acceleration (units/sec²) |
| `groundOffset` | 1.8 | Eye height above terrain |
| `collisionRadius` | 0.4 | Collision capsule radius |

### 2.3 Terrain Collision

```cpp
float sampleTerrainHeight(const Heightmap& heightmap, float worldX, float worldZ) {
    // Convert world coords to heightmap coords
    float u = (worldX + terrainHalfWidth) / terrainWidth;
    float v = (worldZ + terrainHalfHeight) / terrainHeight;
    
    // Clamp to valid range
    u = glm::clamp(u, 0.0f, 1.0f);
    v = glm::clamp(v, 0.0f, 1.0f);
    
    // Sample with bilinear interpolation
    float heightmapX = u * (heightmap.width - 1);
    float heightmapY = v * (heightmap.height - 1);
    
    int x0 = (int)heightmapX;
    int y0 = (int)heightmapY;
    int x1 = std::min(x0 + 1, (int)heightmap.width - 1);
    int y1 = std::min(y0 + 1, (int)heightmap.height - 1);
    
    float fx = heightmapX - x0;
    float fy = heightmapY - y0;
    
    float h00 = heightmap.sample(x0, y0);
    float h10 = heightmap.sample(x1, y0);
    float h01 = heightmap.sample(x0, y1);
    float h11 = heightmap.sample(x1, y1);
    
    float h0 = glm::mix(h00, h10, fx);
    float h1 = glm::mix(h01, h11, fx);
    
    return glm::mix(h0, h1, fy);
}

void CharacterController::update(float deltaTime) {
    // Apply gravity
    if (state_ != State::Grounded) {
        velocity_.y -= gravity_ * deltaTime;
    }
    
    // Apply horizontal movement
    glm::vec3 moveDir = computeMoveDirection();
    velocity_.x = moveDir.x * currentSpeed_;
    velocity_.z = moveDir.z * currentSpeed_;
    
    // Update position
    position_ += velocity_ * deltaTime;
    
    // Ground check
    float terrainHeight = sampleTerrainHeight(heightmap_, position_.x, position_.z);
    float feetHeight = position_.y - groundOffset_;
    
    if (feetHeight <= terrainHeight) {
        position_.y = terrainHeight + groundOffset_;
        velocity_.y = 0.0f;
        state_ = State::Grounded;
    }
}
```

### 2.4 Slope Handling

```cpp
glm::vec3 sampleTerrainNormal(const Heightmap& heightmap, float x, float z) {
    float step = cellScale_;
    float hL = sampleTerrainHeight(heightmap, x - step, z);
    float hR = sampleTerrainHeight(heightmap, x + step, z);
    float hU = sampleTerrainHeight(heightmap, x, z - step);
    float hD = sampleTerrainHeight(heightmap, x, z + step);
    
    glm::vec3 normal;
    normal.x = (hL - hR) / (2.0f * step);
    normal.y = 1.0f;
    normal.z = (hU - hD) / (2.0f * step);
    
    return glm::normalize(normal);
}

bool canWalkOnSlope(const glm::vec3& normal, float maxSlopeAngle) {
    float cosAngle = glm::dot(normal, glm::vec3(0, 1, 0));
    return cosAngle >= cos(glm::radians(maxSlopeAngle));
}
```

---

## 3. Terrain Editing (Future Milestone)

Tools for modifying the heightmap in real-time.

### 3.1 Brush Modes

| Mode | Effect | Description |
|------|--------|-------------|
| Raise | `height += strength` | Build up terrain |
| Lower | `height -= strength` | Dig down terrain |
| Smooth | `height = average(neighbors)` | Smooth out bumps |
| Flatten | `height = targetHeight` | Level to specific height |
| Noise | `height += noise * strength` | Add procedural detail |

### 3.2 Brush Parameters

```cpp
struct BrushSettings {
    BrushMode mode = BrushMode::Raise;
    float radius = 10.0f;           // World units
    float strength = 0.5f;          // 0-1 normalized
    float falloff = 0.5f;           // Edge softness (0=hard, 1=soft)
    float targetHeight = 0.0f;      // For Flatten mode
    uint32_t noiseOctaves = 3;      // For Noise mode
    float noiseFrequency = 0.1f;    // For Noise mode
};
```

### 3.3 Brush Falloff Functions

```cpp
float computeBrushFalloff(float distance, float radius, float falloff) {
    float t = distance / radius;
    if (t >= 1.0f) return 0.0f;
    
    // Smooth falloff using smoothstep
    float falloffStart = 1.0f - falloff;
    if (t < falloffStart) return 1.0f;
    
    float ft = (t - falloffStart) / falloff;
    return 1.0f - (3.0f * ft * ft - 2.0f * ft * ft * ft);  // smoothstep
}
```

### 3.4 Ray-Cast for Brush Placement

```cpp
struct RaycastResult {
    bool hit;
    glm::vec3 worldPosition;
    glm::vec2 heightmapCoord;
    float distance;
};

RaycastResult raycastTerrain(const glm::vec3& rayOrigin,
                              const glm::vec3& rayDir,
                              const Heightmap& heightmap) {
    RaycastResult result = {};
    
    // Binary search along ray for intersection
    float tMin = 0.0f;
    float tMax = 10000.0f;
    
    // First, find rough intersection with binary search
    for (int i = 0; i < 64; i++) {
        float tMid = (tMin + tMax) * 0.5f;
        glm::vec3 pos = rayOrigin + rayDir * tMid;
        float terrainH = sampleTerrainHeight(heightmap, pos.x, pos.z);
        
        if (pos.y < terrainH) {
            tMax = tMid;
        } else {
            tMin = tMid;
        }
    }
    
    // Refine with linear search
    float t = tMin;
    for (int i = 0; i < 128; i++) {
        glm::vec3 pos = rayOrigin + rayDir * t;
        float terrainH = sampleTerrainHeight(heightmap, pos.x, pos.z);
        
        if (pos.y <= terrainH) {
            result.hit = true;
            result.worldPosition = pos;
            result.heightmapCoord = worldToHeightmap(pos.x, pos.z);
            result.distance = t;
            return result;
        }
        
        t += 0.5f;  // Step size
    }
    
    return result;
}
```

### 3.5 Applying Brush Edits

```cpp
void applyBrush(Heightmap& heightmap, const BrushSettings& brush,
                const glm::vec2& centerCoord) {
    // Convert radius to heightmap cells
    int cellRadius = (int)ceil(brush.radius / cellScale_);
    
    int cx = (int)centerCoord.x;
    int cy = (int)centerCoord.y;
    
    for (int dy = -cellRadius; dy <= cellRadius; dy++) {
        for (int dx = -cellRadius; dx <= cellRadius; dx++) {
            int x = cx + dx;
            int y = cy + dy;
            
            if (x < 0 || x >= heightmap.width || y < 0 || y >= heightmap.height)
                continue;
            
            float worldDist = sqrt(dx*dx + dy*dy) * cellScale_;
            float falloff = computeBrushFalloff(worldDist, brush.radius, brush.falloff);
            
            if (falloff <= 0.0f) continue;
            
            float& height = heightmap.at(x, y);
            float delta = brush.strength * falloff * deltaTime_;
            
            switch (brush.mode) {
                case BrushMode::Raise:
                    height += delta;
                    break;
                case BrushMode::Lower:
                    height -= delta;
                    break;
                case BrushMode::Flatten:
                    height = glm::mix(height, brush.targetHeight, falloff * brush.strength);
                    break;
                case BrushMode::Smooth:
                    height = computeNeighborAverage(heightmap, x, y, falloff);
                    break;
            }
            
            // Clamp to valid range
            height = glm::clamp(height, 0.0f, 1.0f);
        }
    }
    
    // Mark region dirty for GPU upload
    heightmap.markDirty(cx - cellRadius, cy - cellRadius,
                        cx + cellRadius, cy + cellRadius);
}
```

### 3.6 GPU Update

```cpp
void updateHeightmapTexture(const Heightmap& heightmap) {
    if (!heightmap.isDirty()) return;
    
    auto [minX, minY, maxX, maxY] = heightmap.getDirtyRegion();
    int width = maxX - minX + 1;
    int height = maxY - minY + 1;
    
    // Extract dirty region
    std::vector<uint16_t> regionData(width * height);
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            float h = heightmap.at(minX + x, minY + y);
            regionData[y * width + x] = (uint16_t)(h * 65535.0f);
        }
    }
    
    // Upload to GPU
    wgpu::ImageCopyTexture dst = {};
    dst.texture = heightmapTexture_;
    dst.origin = {(uint32_t)minX, (uint32_t)minY, 0};
    
    wgpu::TextureDataLayout layout = {};
    layout.bytesPerRow = width * sizeof(uint16_t);
    layout.rowsPerImage = height;
    
    wgpu::Extent3D size = {(uint32_t)width, (uint32_t)height, 1};
    
    queue_.WriteTexture(&dst, regionData.data(),
                        regionData.size() * sizeof(uint16_t),
                        &layout, &size);
    
    // Regenerate affected mip levels
    regenerateMipsInRegion(minX, minY, maxX, maxY);
    
    heightmap.clearDirty();
}
```

---

## 4. Input System

### 4.1 Input State

```cpp
class Input {
public:
    void update();  // Call once per frame
    
    // Keyboard
    bool isKeyDown(Key key) const;
    bool wasKeyPressed(Key key) const;   // Just pressed this frame
    bool wasKeyReleased(Key key) const;  // Just released this frame
    
    // Mouse
    bool isMouseCaptured() const;
    void captureMouse();
    void releaseMouse();
    
    glm::vec2 mousePosition() const;
    glm::vec2 mouseDelta() const;
    
    bool isMouseButtonDown(MouseButton btn) const;
    bool wasMouseButtonPressed(MouseButton btn) const;
    bool wasMouseButtonReleased(MouseButton btn) const;
    
    float scrollDelta() const;
    
private:
    std::array<bool, 256> currentKeys_;
    std::array<bool, 256> previousKeys_;
    
    glm::vec2 mousePos_;
    glm::vec2 prevMousePos_;
    
    std::array<bool, 3> currentButtons_;
    std::array<bool, 3> previousButtons_;
    
    float scroll_;
    bool captured_;
};
```

### 4.2 Platform-Specific Input

**Native (GLFW):**
```cpp
void setupGLFWInput(GLFWwindow* window, Input* input) {
    glfwSetKeyCallback(window, [](GLFWwindow* w, int key, int, int action, int) {
        auto* input = static_cast<Input*>(glfwGetWindowUserPointer(w));
        if (action == GLFW_PRESS) input->onKeyDown(key);
        else if (action == GLFW_RELEASE) input->onKeyUp(key);
    });
    
    glfwSetCursorPosCallback(window, [](GLFWwindow* w, double x, double y) {
        auto* input = static_cast<Input*>(glfwGetWindowUserPointer(w));
        input->onMouseMove((float)x, (float)y);
    });
    
    glfwSetMouseButtonCallback(window, [](GLFWwindow* w, int button, int action, int) {
        auto* input = static_cast<Input*>(glfwGetWindowUserPointer(w));
        if (action == GLFW_PRESS) input->onMouseDown(button);
        else if (action == GLFW_RELEASE) input->onMouseUp(button);
    });
}
```

**Web (Emscripten):**
```cpp
EM_BOOL onKeyDown(int eventType, const EmscriptenKeyboardEvent* e, void* userData) {
    auto* input = static_cast<Input*>(userData);
    input->onKeyDown(emscriptenKeyToCode(e->code));
    return EM_TRUE;
}

void setupEmscriptenInput(Input* input) {
    emscripten_set_keydown_callback(EMSCRIPTEN_EVENT_TARGET_DOCUMENT, input, false, onKeyDown);
    emscripten_set_keyup_callback(EMSCRIPTEN_EVENT_TARGET_DOCUMENT, input, false, onKeyUp);
    emscripten_set_mousemove_callback("#canvas", input, false, onMouseMove);
    emscripten_set_mousedown_callback("#canvas", input, false, onMouseDown);
    emscripten_set_mouseup_callback("#canvas", input, false, onMouseUp);
    emscripten_set_wheel_callback("#canvas", input, false, onWheel);
}
```

---

## 5. UI Overlay

### 5.1 Debug HUD

Display runtime information:
- FPS / Frame time
- Camera position
- Active rendering path
- GPU timing (when available)
- Memory usage

### 5.2 Controls Help

```
┌─────────────────────────────────┐
│  Controls                       │
│  ─────────                      │
│  WASD      - Move               │
│  Mouse     - Look around        │
│  E/Space   - Move up            │
│  Q/Ctrl    - Move down          │
│  Shift     - Speed boost        │
│  Escape    - Release mouse      │
│  F1        - Toggle HUD         │
│  F2        - Toggle wireframe   │
│  F3        - Toggle path        │
└─────────────────────────────────┘
```

### 5.3 Implementation Note

For the initial milestone, UI can be implemented as:
1. **Native:** Simple text rendering or ImGui integration
2. **Web:** HTML overlay elements positioned over the canvas

---

## 6. Development Tools

Internal tools for development, testing, and documentation.

### 6.1 Camera Recording & Teleportation

A system to record interesting viewpoints and quickly navigate between them.

**Usage:**
*   **Record:** Press `R` to capture the current camera position and orientation (yaw/pitch).
    *   The system logs the state to the console in a C++ initializer list format (e.g., `{ {x,y,z}, yaw, pitch }`).
    *   Developers can copy this output and paste it into the `teleportTargets_` initialization in `Application::init` to persist the points.
*   **Teleport:** Press number keys `1` through `9` to instantly teleport the camera to the corresponding index in the hardcoded target list.

### 6.2 Automated Screenshot System

A headless automation system for capturing high-quality screenshots of specific viewpoints, primarily for regression testing or generating documentation assets.

**Features:**
*   Automated application launch and navigation.
*   Rendering warm-up (to allow TAA/temporal effects to settle).
*   GPU texture readback and PNG file saving.

**Command-Line Interface:**
| Argument | Description |
|----------|-------------|
| `--teleport-index <N>` | Teleport to the Nth recorded target immediately on startup. |
| `--screenshot <path>` | Enable screenshot mode; save the frame to `<path>` and exit. |
| `--screenshot-frames <N>` | Number of frames to render before capturing (default: 10). |

**Build Target (Native Only):**
A helper script `scripts/screenshot_tour.sh` automates the process of building the native binary and capturing a sequence of images for all defined teleport targets.

*Note: This feature is available only on native builds due to filesystem access requirements.*
