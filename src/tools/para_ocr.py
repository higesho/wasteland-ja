# パラグラフ画像（work/para/pNNN.png、10x19 の等幅、antialias あり）を文字に起こす
import os, json, sys, glob
import numpy as np
from PIL import Image
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
W, H = 10, 19
def cells(path):
    im = np.array(Image.open(path)).astype(np.float32) / 255
    h, w = im.shape
    out = []
    for r in range(h // H):
        out.append([im[r * H:(r + 1) * H, c * W:(c + 1) * W].ravel() for c in range(w // W)])
    return out
def load_templates():
    p = os.path.join(ROOT, 'work', 'para_tpl.npz')
    if not os.path.exists(p): return [], np.zeros((0, W * H), np.float32)
    z = np.load(p, allow_pickle=True)
    return list(z['chars']), z['vecs']
def save_templates(chars, vecs):
    np.savez(os.path.join(ROOT, 'work', 'para_tpl.npz'), chars=np.array(chars, dtype=object), vecs=np.array(vecs, np.float32))
def cells_dx(path, dx):
    im = np.array(Image.open(path)).astype(np.float32) / 255
    if dx > 0: im = np.pad(im, ((0, 0), (dx, 0)))[:, :im.shape[1]]
    elif dx < 0: im = np.pad(im, ((0, 0), (0, -dx)))[:, -dx:]
    h, w = im.shape
    return [[im[r * H:(r + 1) * H, c * W:(c + 1) * W].ravel() for c in range(w // W)] for r in range(h // H)]
def best_dx(path, V):
    best = None
    for dx in (-2, -1, 0, 1, 2):
        rows = cells_dx(path, dx)
        allv = np.array([v for row in rows for v in row])
        d = np.abs(allv[:, None, :] - V[None, :, :]).sum(2).min(1)
        bad = (d > 4).sum()
        if best is None or bad < best[0]: best = (bad, dx)
    return best[1]
