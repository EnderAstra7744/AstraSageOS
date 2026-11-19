#!/bin/sh
# Builds the TLS client for the host, makes a throw-away PKI and talks to `openssl s_server` over loopback.
# Usage: sh sys/tests/run_tls_test.sh   (needs openssl, python3 with `cryptography`)
set -e
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
W=${TMPDIR:-/tmp}/astls; rm -rf $W; mkdir -p $W; cd $W
gcc -O2 -Wall -Wextra -o tls_host $ROOT/sys/tests/tls_host.c $ROOT/sys/tls.c $ROOT/sys/x509.c $ROOT/sys/crypto.c
mkca() { # name keyspec
  openssl req -x509 -new -nodes -newkey $2 -keyout $1.key -out $1.crt -days 400 -subj "/CN=$1" -addext "basicConstraints=critical,CA:TRUE" -addext "keyUsage=critical,keyCertSign,cRLSign" -sha256 2>/dev/null
}
mkleaf() { # name issuer keyspec sigdigest  san
  openssl req -new -nodes -newkey $3 -keyout $1.key -out $1.csr -subj "/CN=$1" 2>/dev/null
  printf "subjectAltName=$5\nbasicConstraints=CA:FALSE\nextendedKeyUsage=serverAuth\nkeyUsage=digitalSignature,keyEncipherment\n" > $1.ext
  openssl x509 -req -in $1.csr -CA $2.crt -CAkey $2.key -CAcreateserial -out $1.crt -days 300 -$4 -extfile $1.ext 2>/dev/null
}
mkca rootrsa rsa:3072
mkca rootec ec:secp384r1 2>/dev/null || { openssl ecparam -name secp384r1 -genkey -noout -out rootec.key; openssl req -x509 -new -key rootec.key -out rootec.crt -days 400 -subj /CN=rootec -addext basicConstraints=critical,CA:TRUE -addext keyUsage=critical,keyCertSign -sha384 2>/dev/null; }
# intermediate (RSA) signed by rootrsa
openssl req -new -nodes -newkey rsa:2048 -keyout interm.key -out interm.csr -subj "/CN=interm" 2>/dev/null
printf "basicConstraints=critical,CA:TRUE,pathlen:0\nkeyUsage=critical,keyCertSign\n" > interm.ext
openssl x509 -req -in interm.csr -CA rootrsa.crt -CAkey rootrsa.key -CAcreateserial -out interm.crt -days 300 -sha384 -extfile interm.ext 2>/dev/null
mkleaf leafrsa interm rsa:2048 sha256 "DNS:localhost,DNS:*.wild.test"
cat leafrsa.crt interm.crt > leafrsa.chain
openssl ecparam -name prime256v1 -genkey -noout -out ecleaf.key
openssl req -new -key ecleaf.key -out ecleaf.csr -subj "/CN=ecleaf" 2>/dev/null
printf "subjectAltName=DNS:localhost,IP:127.0.0.1\nbasicConstraints=CA:FALSE\n" > ecleaf.ext
openssl x509 -req -in ecleaf.csr -CA rootec.crt -CAkey rootec.key -CAcreateserial -out ecleaf.crt -days 300 -sha384 -extfile ecleaf.ext 2>/dev/null
# trust lists
python3 - <<PY
import sys
sys.argv=["x"]
from cryptography import x509
from cryptography.hazmat.primitives import serialization
exec(open("$ROOT/sys/tools/mkroots.py").read().split("src = sys.argv")[0].split('"""',2)[2].split("import sys, os, datetime",1)[0] if False else "")
PY
cat > mkblob.py <<'PY'
import sys
sys.path.insert(0, sys.argv[1])
src = open(sys.argv[1] + "/mkroots.py").read()
head = src.split("src = sys.argv")[0]
exec(head)
out = b""
for f in sys.argv[3:]:
    pem = open(f, "rb").read()
    cert = x509.load_pem_x509_certificate(pem)
    subj, spki = raw_fields(cert.public_bytes(serialization.Encoding.DER))
    out += len(subj).to_bytes(2, "big") + len(spki).to_bytes(2, "big") + subj + spki
open(sys.argv[2], "wb").write(out)
PY
python3 mkblob.py $ROOT/sys/tools both.bin rootrsa.crt rootec.crt
python3 mkblob.py $ROOT/sys/tools other.bin rootec.crt
PASS=0; FAIL=0
run() { # label port expect(OK|ERR) args...
  label=$1; port=$2; expect=$3; shift 3
  out=$(timeout 20 ./tls_host "$@" 2>&1 | tail -1) || true
  case "$out" in $expect*) PASS=$((PASS+1)); echo "PASS: $label  -> $out";; *) FAIL=$((FAIL+1)); echo "FAIL: $label  -> $out";; esac
}
serve() { # port args...
  port=$1; shift
  openssl s_server -accept $port -www -quiet "$@" >/dev/null 2>&1 &
  echo $! >> pids; sleep 0.4
}
: > pids
serve 4431 -tls1_2 -cert leafrsa.crt -cert_chain interm.crt -key leafrsa.key -cipher ECDHE-RSA-AES128-GCM-SHA256 -groups X25519
serve 4432 -tls1_2 -cert leafrsa.crt -cert_chain interm.crt -key leafrsa.key -cipher ECDHE-RSA-AES256-GCM-SHA384 -groups P-256
serve 4433 -tls1_2 -cert ecleaf.crt -key ecleaf.key -cipher ECDHE-ECDSA-AES128-GCM-SHA256 -groups X25519
serve 4434 -tls1_2 -cert ecleaf.crt -key ecleaf.key -cipher ECDHE-ECDSA-AES256-GCM-SHA384 -groups P-256
serve 4435 -tls1_2 -cert leafrsa.crt -cert_chain interm.crt -key leafrsa.key -cipher ECDHE-RSA-CHACHA20-POLY1305
run "RSA leaf+intermediate, AES128-GCM, X25519"   4431 "OK" localhost 4431 both.bin 1
run "RSA leaf+intermediate, AES256-GCM, P-256"    4432 "OK" localhost 4432 both.bin 1
run "EC P-256 leaf, AES128-GCM, X25519"           4433 "OK" localhost 4433 both.bin 1
run "EC P-256 leaf (P-384 root), AES256-GCM, P-256" 4434 "OK" localhost 4434 both.bin 1
run "wildcard SAN host name"                      4431 "OK" a.wild.test 4431 both.bin 1
run "IP address in SAN"                           4433 "OK" 127.0.0.1 4433 both.bin 1
run "wrong host name refused"                     4431 "ERR the certificate is not valid for this host" other.test 4431 both.bin 1
run "unknown root refused"                        4431 "ERR the certificate chain does not lead" localhost 4431 other.bin 1
run "no verification (-k) works"                  4431 "OK" localhost 4431 other.bin 0
run "server with only ChaCha20 -> clear error"    4435 "ERR" localhost 4435 both.bin 1
for p in $(cat pids); do kill $p 2>/dev/null || true; done
echo "$PASS passed, $FAIL failed"
[ $FAIL -eq 0 ]
