#!/bin/bash
# Makes a new over-the-air signing key pair, for a key rotation
# (deploy/README.md, "Rotating the signing key").
#
#   tools/new_ota_key.sh ~/.config/muleskin/ota-signing-key-2026-11.pem
#
# Writes the PRIVATE key there (owner-only, refusing to overwrite anything)
# and prints the two things that go into include/ota_pubkey.h: the public key
# as PEM, for the comment the build checks signatures against, and as the
# 65-byte C array for OTA_PUBKEYS. Nothing else is touched.
#
# Run it on the machine that will do the signing -- ideally not the web
# server that serves the firmware. Anyone with the private key can sign
# firmware every board will install over WiFi: never commit it, paste it into
# a chat or a ticket, or leave copies in shell history or logs.
set -euo pipefail
[ $# -eq 1 ] || { sed -n '2,15p' "$0" | sed 's/^# \{0,1\}//'; exit 2; }
out=$1
[ -e "$out" ] && { echo "new_ota_key: $out already exists -- not overwriting a key" >&2; exit 1; }
command -v openssl >/dev/null || { echo "new_ota_key: needs openssl" >&2; exit 1; }
mkdir -p "$(dirname "$out")"
umask 077
openssl ecparam -name prime256v1 -genkey -noout -out "$out"
chmod 600 "$out"
pub=$(openssl ec -in "$out" -pubout 2>/dev/null)

echo "private key: $out (mode 600). Keep it off the web server if you can."
echo
echo "1. Add this PEM to the comment in include/ota_pubkey.h, next to the current one:"
echo
echo "$pub" | sed 's|^|//     |'
echo
echo "2. Add this point to OTA_PUBKEYS in include/ota_pubkey.h, FIRST (newest first):"
echo
printf '%s\n' "$pub" | openssl ec -pubin -outform DER 2>/dev/null | tail -c 65 \
  | od -An -v -tx1 | tr -s ' ' '\n' | grep -v '^$' \
  | awk '{ printf "%s0x%s,", (NR % 12 == 1 ? (NR > 1 ? "\n    " : "    ") : " "), toupper($1) } END { print "" }' \
  | sed '1s/^/{\n/; $s/$/\n},/'
echo
echo "3. Release that build signed with the OLD key (deploy/README.md), then"
echo "   switch OTA_SIGNING_KEY to $out for the releases after it."
