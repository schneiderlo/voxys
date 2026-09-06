#!/usr/bin/env python3
"""Reproduce the unfiltered LEGO study crop (requires numpy and zstandard)."""
from array import array
import hashlib
import json
from pathlib import Path
import struct
import numpy as np
import zstandard
from terrain_diffusion_import import write_ldh_python

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'data/generated/td_seed_1234_8192.ldh'
X, Z, SIZE = 2816, 7424, 256

def main():
    data = SOURCE.read_bytes()
    magic, version, width, height, flags, low_size, high_size, *_ = struct.unpack('<16I', data[:64])
    if (magic,version,flags & 3) != (0x4C444831,1,1):
        raise ValueError('Unsupported source LDH format')
    decoder = zstandard.ZstdDecompressor()
    low = np.frombuffer(decoder.decompress(data[64:64+low_size],max_output_size=width*height),dtype=np.uint8)
    high = np.frombuffer(decoder.decompress(data[64+low_size:64+low_size+high_size],max_output_size=width*height),dtype=np.uint8)
    delta = (low.astype(np.uint16) + (high.astype(np.uint16)<<8)).reshape(height,width)
    # Invert the planar predictor modulo 2^16. Row-wise accumulation limits
    # temporary memory and is equivalent to the engine's left+top-diagonal.
    previous = np.zeros(width,dtype=np.uint16)
    crop = np.empty((SIZE,SIZE),dtype=np.uint16)
    for z in range(Z+SIZE):
        previous = ((delta[z].astype(np.uint32).cumsum()+previous)&65535).astype(np.uint16)
        if z >= Z:
            crop[z-Z] = previous[X:X+SIZE]
    output = ROOT / 'data/lego_shore.ldh'
    write_ldh_python(output,array('H',crop.ravel().tolist()),SIZE,SIZE,19,True)
    metadata = dict(source=str(SOURCE.relative_to(ROOT)),source_sha256=hashlib.sha256(data).hexdigest(),
                    origin=[X,Z],samples=[SIZE,SIZE],height_scale=600,cell_scale=1,
                    water_height=-200,filter='none',raw_sha256=hashlib.sha256(crop.astype('<u2').tobytes()).hexdigest())
    (ROOT/'data/lego_shore.json').write_text(json.dumps(metadata,indent=2)+'\n')
    print(f'{output.name}: {output.stat().st_size} bytes; original terrain samples preserved')

if __name__ == '__main__':
    main()
