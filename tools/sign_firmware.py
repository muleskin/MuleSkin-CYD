"""Sign a firmware image for Bluetooth updates.

The board hashes exactly this message while the image streams in, and checks
the signature against the public key compiled into include/ota_pubkey.h:

    SHA-256( b"SQWOTA1\\n" + env + b"\\n" + image )

The build name is part of what is signed, so an image signed for one board is
refused by every other one -- a white screen cannot be navigated to switch back.

Usage:
    python tools/sign_firmware.py --key key.pem --env cyd-fast \\
        --bin cyd-fast-firmware.bin --out cyd-fast-firmware.sig

Needs only the openssl command line tool. The output is a DER ECDSA signature,
which is what the board's mbedtls_pk_verify() reads.
"""
import argparse
import os
import subprocess
import sys
import tempfile

PREFIX = b"SQWOTA1\n"


def verify_any(pub_path, sig_path, msg_path):
    """openssl reads only the first key in a PEM file; try each block."""
    with open(pub_path) as f:
        text = f.read()
    blocks, cur = [], []
    for line in text.splitlines():
        if "BEGIN PUBLIC KEY" in line:
            cur = [line]
        elif cur:
            cur.append(line)
            if "END PUBLIC KEY" in line:
                blocks.append("\n".join(cur) + "\n")
                cur = []
    if not blocks:
        sys.exit("no public key in %s" % pub_path)
    for b in blocks:
        fd, p = tempfile.mkstemp(suffix=".pem")
        try:
            with os.fdopen(fd, "w") as f:
                f.write(b)
            r = subprocess.run(["openssl", "dgst", "-sha256", "-verify", p, "-signature", sig_path, msg_path],
                               capture_output=True)
            if r.returncode == 0:
                return
        finally:
            os.remove(p)
    sys.exit("the signature matches none of the %d trusted key(s) in %s" % (len(blocks), pub_path))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--key", required=True, help="P-256 private key, PEM")
    ap.add_argument("--env", required=True, help="PlatformIO environment the image was built for")
    ap.add_argument("--bin", required=True, help="the app image (firmware.bin)")
    ap.add_argument("--out", required=True, help="where to write the DER signature")
    ap.add_argument("--pub", help="optional public key(s) to verify the result against: a PEM file "
                                  "with one or more keys (any one matching passes -- a key rotation "
                                  "trusts two)")
    ap.add_argument("--raw", action="store_true",
                    help="sign a file that is not a firmware image (the extra detection rules, "
                         "--env signatures): skip the image check")
    a = ap.parse_args()

    with open(a.bin, "rb") as f:
        image = f.read()
    if not a.raw and (not image or image[0] != 0xE9):
        sys.exit("%s does not start with the ESP32 image magic byte" % a.bin)

    fd, msg_path = tempfile.mkstemp(suffix=".msg")
    try:
        with os.fdopen(fd, "wb") as f:
            f.write(PREFIX + a.env.encode("ascii") + b"\n" + image)
        subprocess.run(["openssl", "dgst", "-sha256", "-sign", a.key, "-out", a.out, msg_path],
                       check=True)
        if a.pub:
            verify_any(a.pub, a.out, msg_path)
    finally:
        os.remove(msg_path)
    print("signed %s for %s (%d bytes) -> %s" % (a.bin, a.env, len(image), a.out))


if __name__ == "__main__":
    main()
