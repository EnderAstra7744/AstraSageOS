import pickle, zipfile, numpy as np, sys, time
class T:
    def __init__(s, storage, off, size, stride): s.st, s.off, s.size, s.stride = storage, off, size, stride
class Stor:
    def __init__(s, key, dt): s.key, s.dt = key, dt
def rebuild(storage, off, size, stride, *a): return T(storage, off, size, stride)
class U(pickle.Unpickler):
    def find_class(s, mod, name):
        if name == '_rebuild_tensor_v2': return rebuild
        if name == 'OrderedDict':
            import collections; return collections.OrderedDict
        if mod == 'torch' and name.endswith('Storage'): return name
        return super().find_class(mod, name)
    def persistent_load(s, pid): return Stor(pid[2], pid[1])
def load(path):
    z = zipfile.ZipFile(path)
    sd = U(z.open('archive/data.pkl')).load()
    sd = sd.get('params_ema', sd.get('params', sd))
    out = {}
    for k, t in sd.items():
        raw = z.read('archive/data/' + t.st.key)
        a = np.frombuffer(raw, np.float32)
        n = int(np.prod(t.size)) if len(t.size) else 1
        out[k] = a[t.off:t.off + n].reshape(t.size).copy()
    return out

def conv(x, w, b, relu=False):                      # x: C,H,W (already padded by 1 later), w: O,C,3,3
    C, H, W = x.shape
    xp = np.pad(x, ((0, 0), (1, 1), (1, 1)))
    cols = np.empty((C * 9, H * W), np.float32)
    k = 0
    for c in range(C):
        for dy in range(3):
            for dx in range(3):
                cols[k] = xp[c, dy:dy + H, dx:dx + W].reshape(-1); k += 1
    y = w.reshape(w.shape[0], -1) @ cols + b[:, None]
    y = y.reshape(-1, H, W)
    if relu: y = np.where(y > 0, y, 0.2 * y)
    return y

def rdb(x, P, pre):
    feats = [x]
    for i in range(1, 5):
        feats.append(conv(np.concatenate(feats, 0), P[f'{pre}.conv{i}.weight'], P[f'{pre}.conv{i}.bias'], True))
    x5 = conv(np.concatenate(feats, 0), P[f'{pre}.conv5.weight'], P[f'{pre}.conv5.bias'])
    return x5 * 0.2 + x

def net(x, P):
    fea = conv(x, P['conv_first.weight'], P['conv_first.bias'])
    t = fea
    for i in range(23):
        r = t
        for j in (1, 2, 3): r = rdb(r, P, f'body.{i}.rdb{j}')
        t = r * 0.2 + t
    t = conv(t, P['conv_body.weight'], P['conv_body.bias']) + fea
    def up(a):
        return np.repeat(np.repeat(a, 2, 1), 2, 2)
    t = conv(up(t), P['conv_up1.weight'], P['conv_up1.bias'], True)
    t = conv(up(t), P['conv_up2.weight'], P['conv_up2.bias'], True)
    t = conv(t, P['conv_hr.weight'], P['conv_hr.bias'], True)
    return conv(t, P['conv_last.weight'], P['conv_last.bias'])

def upscale(img, P, tile=64, pad=10):               # img HxWx3 uint8 RGB -> 4x
    H, W, _ = img.shape
    x = (img.astype(np.float32) / 255).transpose(2, 0, 1)
    out = np.zeros((3, H * 4, W * 4), np.float32)
    for y0 in range(0, H, tile):
        for x0 in range(0, W, tile):
            ya, yb = max(0, y0 - pad), min(H, y0 + tile + pad)
            xa, xb = max(0, x0 - pad), min(W, x0 + tile + pad)
            o = net(x[:, ya:yb, xa:xb], P)
            ty, tx = (y0 - ya) * 4, (x0 - xa) * 4
            hh, ww = min(tile, H - y0) * 4, min(tile, W - x0) * 4
            out[:, y0 * 4:y0 * 4 + hh, x0 * 4:x0 * 4 + ww] = o[:, ty:ty + hh, tx:tx + ww]
    return (np.clip(out, 0, 1).transpose(1, 2, 0) * 255 + 0.5).astype(np.uint8)

if __name__ == '__main__':
    from PIL import Image
    P = load('/tmp/sr/esrgan.pth')
    print(len(P), P['conv_first.weight'].shape)
    im = np.asarray(Image.open(sys.argv[1]).convert('RGB'))
    if len(sys.argv) > 3:
        x, y, w, h = map(int, sys.argv[3].split(',')); im = im[y:y + h, x:x + w]
    t = time.time(); o = upscale(im, P); print('time', time.time() - t, o.shape)
    Image.fromarray(o).save(sys.argv[2])
