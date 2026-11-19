#!/usr/bin/env python3
"""Writes test vectors for crypto_host.c (made with the `cryptography` package / hashlib / hmac = reference results)."""
import hashlib, hmac, os, sys, random
from cryptography.hazmat.primitives.ciphers.aead import AESGCM
from cryptography.hazmat.primitives.asymmetric import x25519, ec, rsa, padding
from cryptography.hazmat.primitives import hashes, serialization
random.seed(7)
out = []
def h(b): return b.hex() if b else "-"
def rb(n): return bytes(random.getrandbits(8) for _ in range(n))
for ln in [0, 1, 55, 56, 63, 64, 65, 111, 112, 127, 128, 129, 1000, 5000]:
    d = rb(ln)
    out.append("HASH %s %s %s %s" % (h(d), hashlib.sha256(d).hexdigest(), hashlib.sha384(d).hexdigest(), hashlib.sha512(d).hexdigest()))
for kl in [5, 32, 64, 100, 200]:
    k, d = rb(kl), rb(77)
    out.append("HMAC %s %s %s %s %s" % (h(k), h(d), hmac.new(k, d, hashlib.sha256).hexdigest(), hmac.new(k, d, hashlib.sha384).hexdigest(), hmac.new(k, d, hashlib.sha512).hexdigest()))
for kl in (16, 32):
    for ln in [0, 1, 15, 16, 17, 31, 32, 100, 1000, 4097]:
        for al in (0, 13, 33):
            k, iv, pt, aad = rb(kl), rb(12), rb(ln), rb(al)
            ct = AESGCM(k).encrypt(iv, pt, aad or None)
            out.append("GCM %s %s %s %s %s" % (h(k), h(iv), h(aad), h(pt), h(ct)))
for _ in range(6):
    a = x25519.X25519PrivateKey.generate(); b = x25519.X25519PrivateKey.generate()
    sk = a.private_bytes(serialization.Encoding.Raw, serialization.PrivateFormat.Raw, serialization.NoEncryption())
    pk = b.public_key().public_bytes(serialization.Encoding.Raw, serialization.PublicFormat.Raw)
    ap = a.public_key().public_bytes(serialization.Encoding.Raw, serialization.PublicFormat.Raw)
    out.append("X25519 %s %s %s %s" % (sk.hex(), pk.hex(), a.exchange(b.public_key()).hex(), ap.hex()))
for _ in range(6):
    a = ec.generate_private_key(ec.SECP256R1()); b = ec.generate_private_key(ec.SECP256R1())
    skb = a.private_numbers().private_value.to_bytes(32, "big")
    pb = b.public_key().public_bytes(serialization.Encoding.X962, serialization.PublicFormat.UncompressedPoint)
    pa = a.public_key().public_bytes(serialization.Encoding.X962, serialization.PublicFormat.UncompressedPoint)
    out.append("P256 %s %s %s %s" % (skb.hex(), pb.hex(), a.exchange(ec.ECDH(), b.public_key()).hex(), pa.hex()))
for curve, hh, hn, cid in ((ec.SECP256R1(), hashes.SHA256(), "sha256", 1), (ec.SECP384R1(), hashes.SHA384(), "sha384", 2), (ec.SECP256R1(), hashes.SHA384(), "sha384", 1), (ec.SECP384R1(), hashes.SHA256(), "sha256", 2)):
    for i in range(4):
        k = ec.generate_private_key(curve)
        msg = rb(50 + i)
        sig = k.sign(msg, ec.ECDSA(hh))
        pub = k.public_key().public_bytes(serialization.Encoding.X962, serialization.PublicFormat.UncompressedPoint)
        dg = hashlib.new(hn, msg).digest()
        out.append("ECDSA %d %s %s %s 1" % (cid, pub.hex(), dg.hex(), sig.hex()))
        bad = bytearray(dg); bad[3] ^= 1
        out.append("ECDSA %d %s %s %s 0" % (cid, pub.hex(), bytes(bad).hex(), sig.hex()))
for bits in (2048, 3072, 4096):
    k = rsa.generate_private_key(65537, bits)
    nn = k.public_key().public_numbers()
    nb = nn.n.to_bytes(bits // 8, "big"); eb = nn.e.to_bytes(3, "big")
    for hn, hh, hid in (("sha256", hashes.SHA256(), 1), ("sha384", hashes.SHA384(), 2), ("sha512", hashes.SHA512(), 3)):
        msg = rb(40)
        sig = k.sign(msg, padding.PKCS1v15(), hh)
        dg = hashlib.new(hn, msg).digest()
        out.append("RSA %s %s %d %s %s 1" % (nb.hex(), eb.hex(), hid, dg.hex(), sig.hex()))
        bad = bytearray(sig); bad[10] ^= 4
        out.append("RSA %s %s %d %s %s 0" % (nb.hex(), eb.hex(), hid, dg.hex(), bytes(bad).hex()))
        d2 = bytearray(dg); d2[0] ^= 1
        out.append("RSA %s %s %d %s %s 0" % (nb.hex(), eb.hex(), hid, bytes(d2).hex(), sig.hex()))
open(sys.argv[1], "w").write("\n".join(out) + "\n")
print(len(out), "vectors")
