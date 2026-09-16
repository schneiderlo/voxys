#!/usr/bin/env bash
set -euo pipefail
g++ -std=c++20 -O2 -ffunction-sections -fdata-sections -I src -I third_party/glm \
  build-adventure-g-b/village-checks/full-terrain-village.cpp \
  src/game/adventure/village_layout.cpp src/game/adventure/town_residents.cpp \
  src/game/adventure/adventure_session.cpp src/game/adventure/adventure_player.cpp \
  src/game/adventure/spatial_queries.cpp src/game/adventure/world_definition.cpp \
  src/game/adventure/construction_policy.cpp src/game/adventure/building_catalog.cpp \
  src/game/expedition/cove_camera.cpp src/game/construction/construction_types.cpp \
  -Wl,--gc-sections -o build-adventure-g-b/village-checks/full-terrain-village
sha256sum /tmp/voxys-adventure-world.r16
build-adventure-g-b/village-checks/full-terrain-village /tmp/voxys-adventure-world.r16
