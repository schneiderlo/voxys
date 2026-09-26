#!/usr/bin/env python3
"""Generate the immutable C++ building catalog from installed canonical source."""
import argparse
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'data/adventure/building-kit-r01/catalog.json'
ADDON_SOURCE = ROOT / 'data/adventure/door-r01/catalog.json'
FRONTIER_SOURCE = ROOT / 'data/adventure/frontier-kit-r01/catalog.json'
LEGACY_SHA256 = 'fc9c763b22b5ef9d81e461608dea0f1b138d8f16a4940e9378442dc18a611150'
OUTPUT = ROOT / 'src/game/adventure/building_catalog.cpp'
ENUMS = ('Foundation', 'Floor', 'Wall', 'Doorway', 'Roof', 'Stair', 'Beam',
         'Brick1x2', 'Brick2x2', 'Brick2x4', 'Bed', 'Chest', 'Workbench', 'Pier')
FRONTIER_ENUMS = ('FrontierFoundation', 'FrontierDeck', 'FrontierWall', 'FrontierDoorway',
                  'FrontierRoof', 'FrontierStairs', 'FrontierBed', 'FrontierChest', 'FrontierWorkbench')


def validated_catalog():
    assert hashlib.sha256(SOURCE.read_bytes()).hexdigest() == LEGACY_SHA256, 'IDs 1–14 remain immutable'
    doc = json.loads(SOURCE.read_text())
    assert doc['schema'] == 1 and doc['lattice_metres'] == .02
    assert len(doc['pieces']) == len(ENUMS)
    for index, piece in enumerate(doc['pieces']):
        assert piece['id'] == index + 1
        bounds = piece['bounds']
        assert bounds['minimum'][1] == 0 and 1 <= len(piece['solids']) <= 12
        for box in [bounds, *piece['solids']]:
            assert len(box['minimum']) == len(box['maximum']) == 3
            for axis in range(3):
                lo, hi = box['minimum'][axis], box['maximum'][axis]
                assert type(lo) is int and type(hi) is int and -1000 <= lo < hi <= 1000
                assert bounds['minimum'][axis] <= lo < hi <= bounds['maximum'][axis]
        assert piece['furniture'] in ('None', 'Bed', 'Chest', 'Workbench')
        assert type(piece['terrain_anchor']) is bool
        assert set(piece['cost']) == {'wood', 'stone', 'scrap'}
        assert all(type(n) is int and 0 <= n <= 999 for n in piece['cost'].values())
    return doc


def validated_door():
    doc = json.loads(ADDON_SOURCE.read_text())
    legacy = validated_catalog()
    assert doc['schema'] == 1 and doc['lattice_metres'] == .02
    assert doc['asset_id'] == 'voxys-adventure-hinged-door-r01'
    assert doc['legacy_catalog_sha256'] == LEGACY_SHA256 and doc['frame_piece_id'] == 4
    assert doc['hinge'] == [-32, 2, 0] and doc['open_yaw_degrees'] == -90
    assert doc['open_leaf'] == {'minimum': [-34, 2, 0], 'maximum': [-30, 110, 64]}
    piece = doc['piece']
    assert (piece['id'], piece['key'], piece['name']) == (15, 'hinged_door', 'Hinged door')
    assert piece['bounds'] == legacy['pieces'][3]['bounds']
    assert piece['solids'][:3] == legacy['pieces'][3]['solids'] and len(piece['solids']) == 4
    assert piece['solids'][3] == {'minimum': [-32, 2, -2], 'maximum': [32, 110, 2]}
    assert piece['cost'] == {'wood': 6, 'stone': 0, 'scrap': 2}
    assert piece['furniture'] == 'Door' and piece['terrain_anchor'] is False
    return doc


def catalog_fingerprint():
    return hashlib.sha256(b'voxys-adventure-building-catalog-v2\0'+SOURCE.read_bytes()+b'\0'+ADDON_SOURCE.read_bytes()).hexdigest()



def validated_frontier():
    doc = json.loads(FRONTIER_SOURCE.read_text())
    assert doc['schema'] == 1 and doc['lattice_metres'] == .02
    assert doc['asset_id'] == 'voxys-frontier-building-kit-r01'
    assert (doc['character_height'], doc['character_radius']) == (4.76, 1.12)
    assert len(doc['pieces']) == len(FRONTIER_ENUMS)
    for index, piece in enumerate(doc['pieces']):
        assert piece['id'] == 16 + index
        assert piece['icon'] in ENUMS and piece['bounds']['minimum'][1] == 0
        assert piece['furniture'] in ('None', 'Bed', 'Chest', 'Workbench')
        assert type(piece['terrain_anchor']) is bool
        assert set(piece['cost']) == {'wood', 'stone', 'scrap'}
        assert all(type(n) is int and 0 <= n <= 999 for n in piece['cost'].values())
        assert 1 <= len(piece['solids']) <= 64 and 1 <= len(piece['visuals']) <= 16
        for box in [piece['bounds'], *piece['solids']]:
            assert len(box['minimum']) == len(box['maximum']) == 3
            for axis in range(3):
                lo, hi = box['minimum'][axis], box['maximum'][axis]
                assert type(lo) is int and type(hi) is int
                assert piece['bounds']['minimum'][axis] <= lo < hi <= piece['bounds']['maximum'][axis]
        for visual in piece['visuals']:
            assert visual['source'] in ENUMS
            assert len(visual['offset']) == len(visual['scale']) == 3
            assert all(type(n) in (float, int) and abs(n) < 100 for n in visual['offset'])
            assert all(type(n) in (float, int) and 0 < n <= 4 for n in visual['scale'])
    return doc


def frontier_catalog_fingerprint():
    return hashlib.sha256(b'voxys-frontier-building-kit-r01\0' + FRONTIER_SOURCE.read_bytes()).hexdigest()

def cpp_box(box):
    return '{{' + ','.join(map(str, box['minimum'])) + '},{' + ','.join(map(str, box['maximum'])) + '}}'


def generate():
    doc = validated_catalog()
    frontier = validated_frontier()['pieces']
    pieces = [*doc['pieces'], validated_door()['piece'], *frontier]
    enums = (*ENUMS, 'HingedDoor', *FRONTIER_ENUMS)
    lines = ['// Generated by tools/adventure_assets/generate_catalog.py; frozen legacy + door + versioned frontier modules.',
             '#include "game/adventure/building_catalog.hpp"', '', '#include <array>', '',
             'namespace voxy::game::adventure {', 'namespace {']
    for index, piece in enumerate(pieces):
        lines.append(f'constexpr std::array<GridBox,{len(piece["solids"])}> solids{index + 1}{{{{')
        lines += ['    ' + cpp_box(box) + ',' for box in piece['solids']]
        lines.append('}};')
    lines.append('const std::array<BuildingDefinition,kBuildingPieceCount> definitions{{')
    for index, piece in enumerate(pieces):
        costs = ','.join(str(piece['cost'][k]) for k in ('wood', 'stone', 'scrap'))
        lines.append('    {PieceKind::' + enums[index] + ',"' + piece['key'] + '","' + piece['name'] + '",'
                     + cpp_box(piece['bounds']) + ',solids' + str(index + 1) + ',{' + costs + '},'
                     + 'FurnitureKind::' + piece['furniture'] + ',' + str(piece['terrain_anchor']).lower() + '},')
    lines.append('}};')
    for index, piece in enumerate(pieces):
        # Legacy pieces retain a single identity transform. Hinged door leaf is
        # articulated separately; this helper supplies only its frame.
        visuals = piece.get('visuals', [{'source': 'Doorway' if index == 14 else enums[index],
                                        'offset': [0, 0, 0], 'scale': [1, 1, 1]}])
        lines.append(f'const std::array<BuildingVisual,{len(visuals)}> visuals{index + 1}{{{{')
        for visual in visuals:
            values = lambda field: ','.join(format(float(v), '.17g') for v in visual[field])
            lines.append('    {PieceKind::' + visual['source'] + ',{' + values('offset')
                         + '},{' + values('scale') + '}},')
        lines.append('}};')
    lines.append('const std::array<std::span<const BuildingVisual>,kBuildingPieceCount> visuals{{')
    lines.append('    ' + ','.join('visuals' + str(i + 1) for i in range(len(pieces))))
    lines += ['}};', 'constexpr std::array<PieceKind,kBuildingPieceCount> icons{{']
    lines.append('    ' + ','.join('PieceKind::' + piece.get('icon', 'Doorway' if i == 14 else enums[i])
                                 for i, piece in enumerate(pieces)))
    lines += ['}};', '} // namespace', '',
              'std::span<const BuildingDefinition> buildingCatalog() noexcept { return definitions; }',
              'bool pieceKindValid(uint32_t value) noexcept { return value >= 1 && value <= kBuildingPieceCount; }',
              'const BuildingDefinition* buildingDefinition(PieceKind kind) noexcept {',
              '    const auto value=static_cast<uint32_t>(kind);',
              '    return pieceKindValid(value) ? &definitions[value-1] : nullptr;', '}',
              'std::span<const BuildingVisual> buildingVisuals(PieceKind kind) noexcept {',
              '    const auto value=static_cast<uint32_t>(kind);',
              '    return pieceKindValid(value) ? visuals[value-1] : std::span<const BuildingVisual>{};', '}',
              'PieceKind buildingIconKind(PieceKind kind) noexcept {',
              '    const auto value=static_cast<uint32_t>(kind);',
              '    return pieceKindValid(value) ? icons[value-1] : PieceKind::Foundation;', '}',
              'bool isFrontierPiece(PieceKind kind) noexcept {',
              '    const auto value=static_cast<uint32_t>(kind);',
              '    return value>kLegacyBuildingPieceCount && value<=kBuildingPieceCount;', '}',
              'std::string_view frontierBuildingCatalogFingerprint() noexcept {', f'    return "{frontier_catalog_fingerprint()}";', '}',
              'std::string_view buildingCatalogFingerprint() noexcept {', f'    return "{catalog_fingerprint()}";', '}',
              'std::string_view legacyBuildingCatalogFingerprint() noexcept {', f'    return "{LEGACY_SHA256}";', '}',
              '} // namespace voxy::game::adventure', '']
    return '\n'.join(lines)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    generated = generate()
    if args.check:
        assert OUTPUT.read_text() == generated, 'compiled catalog differs; regenerate deliberately'
        print('Catalog source and compiled definitions agree.')
    else:
        OUTPUT.write_text(generated)
