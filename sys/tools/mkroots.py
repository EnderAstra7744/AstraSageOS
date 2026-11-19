#!/usr/bin/env python3
"""Builds assets/roots.bin, the TLS trust anchors, from a PEM bundle (default: the Mozilla list shipped with the Python
`certifi` package). Entry: [u16 subject_len][u16 spki_len][subject DER][SPKI DER] - the subject exactly as encoded in the
certificate, so that it compares byte for byte with the issuer of server certificates. Only keys the OS can verify
(RSA >= 2048, EC P-256 / P-384) and certificates that have not expired are kept."""
import sys, os, datetime
from cryptography import x509
from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric import rsa, ec

def tlv(b, i):
    tag = b[i]; l = b[i + 1]; h = 2
    if l & 0x80:
        nb = l & 0x7f; l = int.from_bytes(b[i + 2:i + 2 + nb], "big"); h = 2 + nb
    return tag, i + h, l, h

def raw_fields(der):
    _, c0, l0, _ = tlv(der, 0)
    _, t0, tl, _ = tlv(der, c0)               # tbs
    i = t0
    tag, ci, l, h = tlv(der, i)
    if tag == 0xa0: i = ci + l; tag, ci, l, h = tlv(der, i)
    i = ci + l                                # serial
    tag, ci, l, h = tlv(der, i); i = ci + l   # sigalg
    tag, ci, l, h = tlv(der, i); i = ci + l   # issuer
    tag, ci, l, h = tlv(der, i); i = ci + l   # validity
    tag, ci, l, h = tlv(der, i); subj = der[i:ci + l]; i = ci + l
    tag, ci, l, h = tlv(der, i); spki = der[i:ci + l]
    return subj, spki

src = sys.argv[2] if len(sys.argv) > 2 else None
if not src:
    import certifi; src = certifi.where()
out = sys.argv[1]
pem = open(src, "rb").read().decode()
blobs = []; kept = skipped = 0
now = datetime.datetime.now(datetime.timezone.utc)
for chunk in pem.split("-----END CERTIFICATE-----"):
    if "-----BEGIN CERTIFICATE-----" not in chunk: continue
    cert = x509.load_pem_x509_certificate((chunk + "-----END CERTIFICATE-----\n").encode())
    k = cert.public_key()
    ok = (isinstance(k, rsa.RSAPublicKey) and k.key_size >= 2048) or (isinstance(k, ec.EllipticCurvePublicKey) and k.curve.name in ("secp256r1", "secp384r1"))
    if not ok or cert.not_valid_after_utc < now: skipped += 1; continue
    subj, spki = raw_fields(cert.public_bytes(serialization.Encoding.DER))
    blobs.append(len(subj).to_bytes(2, "big") + len(spki).to_bytes(2, "big") + subj + spki); kept += 1
os.makedirs(os.path.dirname(out) or ".", exist_ok=True)
open(out, "wb").write(b"".join(blobs))
print("roots.bin: %d certificates kept, %d skipped, %d bytes" % (kept, skipped, sum(map(len, blobs))))
