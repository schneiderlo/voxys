#!/usr/bin/env python3
"""Reject a blank game frame even if its FPS counter and controls are visible.
Requires Pillow. This is a presence check, not visual-quality acceptance.
"""
import sys
from PIL import Image, ImageStat
image = Image.open(sys.argv[1]).convert('RGB')
w, h = image.size
# Exclude the FPS label and bottom controls/intro. Inspect the game itself.
crop = image.crop((w//5, h//8, 4*w//5, 5*h//8))
variance = max(ImageStat.Stat(crop).var)
colors = len(crop.resize((96, 54)).getcolors(96*54) or [])
assert variance > 50 and colors > 32, f'Blank landscape: variance={variance:.1f}, colors={colors}'
print(f'PASS: visible landscape ({colors} sampled colors, variance {variance:.1f})')
