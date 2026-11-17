#!/usr/bin/env python3
"""Export model-bank position NPZ to a fixed, validated Runtime float64 blob.

Usage: export_position_weights.py source.npz destination.bin
The source is never modified. Runtime does not depend on NumPy at execution.
"""
import pathlib
import struct
import sys

import numpy as np

GRU = [('cell.cell.weight_ih', (192, 68)),
       ('cell.cell.weight_hh', (192, 64)),
       ('cell.cell.bias_ih', (192,)), ('cell.cell.bias_hh', (192,)),
       ('cell.head.weight', (4, 64)), ('cell.head.bias', (4,))]
MAMBA = [('cell.emb.weight', (48, 68)), ('cell.emb.bias', (48,)),
         ('cell.norm.w', (48,)), ('cell.in_proj.weight', (192, 48)),
         ('cell.conv_w', (96, 4)), ('cell.conv_b', (96,)),
         ('cell.x_proj.weight', (19, 96)), ('cell.dt_proj.weight', (96, 3)),
         ('cell.dt_proj.bias', (96,)), ('cell.A_log', (96, 8)),
         ('cell.D', (96,)), ('cell.out_proj.weight', (48, 96)),
         ('cell.norm_f.w', (48,)), ('cell.head.weight', (4, 48)),
         ('cell.head.bias', (4,))]

def export(source, destination):
    with np.load(source) as z:
        kind = 1 if 'cell.cell.weight_ih' in z else 2
        fields = GRU if kind == 1 else MAMBA
        flags = sum(int(float(z[key][0]) > 0) << bit for bit, key in
                    enumerate(('opt_no_edges', 'opt_warp_fix', 'opt_box_mode_targets'))
                    if key in z)
        values = []
        for key, shape in fields:
            if key not in z or z[key].shape != shape:
                raise ValueError(f'{key}: expected {shape}')
            block = np.asarray(z[key], dtype='<f8')
            if not np.isfinite(block).all():
                raise ValueError(f'{key}: nonfinite weight')
            values.append(block.tobytes(order='C'))
    data = b'LWPM68V1' + struct.pack('<II', kind, flags) + b''.join(values)
    pathlib.Path(destination).parent.mkdir(parents=True, exist_ok=True)
    pathlib.Path(destination).write_bytes(data)

if __name__ == '__main__':
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    export(sys.argv[1], sys.argv[2])
