# Deploying the web flasher

The flasher at **https://flasher.oillie.cloud/** is also where boards check
for and download updates. It runs on the oillie.cloud server (a Hostinger
VPS) as two Docker Compose projects, kept here so the setup can be rebuilt or
moved. The files in this folder are copies of what runs on the server; when
one changes there, update it here too.

| Here | On the server |
|---|---|
| `traefik/docker-compose.yml` | `/docker/traefik-a5as/docker-compose.yml` (its `.env` holds `ACME_EMAIL`, see `traefik/.env.example`) |
| `muleskin-flasher/docker-compose.yml` | `/docker/muleskin-flasher/docker-compose.yml` |
| `ntfy/docker-compose.yml`, `ntfy/server.yml` | `/docker/muleskin-ntfy/` -- optional, see [Push alerts (ntfy)](#push-alerts-ntfy) |

The flasher image is built from a clone of this repo at `/root/MuleSkin-CYD`
(`web-flasher/Dockerfile`), and the firmware it serves is that clone's
`.pio/flasher-bins`, mounted read-only at `/firmware`. Everything on the
server needs `sudo`.

## Why boards get plain HTTP

Traefik sends everything on port 80 to HTTPS. The boards can't follow that:
a TLS handshake needs about 40 KB of memory in one piece, which a CYD running
its display doesn't have, so they fetch over plain HTTP on purpose (each
image is signed, and the board checks the signature before installing it).

Two settings make an exception for the update files and nothing else:

- Traefik's global redirect runs at **priority 1**
  (`--entrypoints.web.http.redirections.entrypoint.priority=1`). By default
  it outranks every router, so no exception could win.
- The flasher's **`flasher-ota`** router listens on port 80 at priority 100
  and matches only `/manifest-*.json`, `/*-firmware.bin` and
  `/*-firmware.sig` at the site root, on `flasher.oillie.cloud`.

With the redirect at priority 1, any router that listens on port 80 would
serve plain HTTP. Keep every other router pinned to `entrypoints=websecure`.

Editing Traefik's compose file does nothing until it is recreated:

```bash
cd /docker/traefik-a5as && sudo docker compose up -d
```

Check what the running Traefik was started with:

```bash
tr '\0' '\n' < /proc/$(pgrep -x traefik)/cmdline
```

## Releasing

On a development machine (or on the server):

```bash
tools/release.sh 3.1.2 --push
```

That stamps every `web-flasher/manifest-*.json` with the version, commits,
tags `v3.1.2` and pushes. Add `--name "Short name"` (19 characters) and up to
four `--note "What changed"` (39 each, plain ASCII) and boards show them with
the update notice, in System Properties. Then on the server, one command:

```bash
sudo OTA_SIGNING_KEY=/path/to/ota-key.pem /root/MuleSkin-CYD/deploy/redeploy.sh
```

`redeploy.sh` fetches, checks the clone out at the newest release tag (or the
one you name, e.g. `redeploy.sh v3.1.2` to roll back), builds and signs all
13 boards, rebuilds the flasher container, and then checks that the live site
serves that version, its signatures and `versions.json` -- it fails loudly
otherwise. The clone is left on the tag (detached HEAD); the next run just
fetches and moves it. The same steps by hand, if you need them:

```bash
sudo git -C /root/MuleSkin-CYD fetch --tags origin
sudo git -C /root/MuleSkin-CYD checkout v3.1.2
cd /root/MuleSkin-CYD && sudo OTA_SIGNING_KEY=/path/to/ota-key.pem tools/build_flasher_bins.sh
cd /docker/muleskin-flasher && sudo docker compose up -d --build
```

`build_flasher_bins.sh` builds all 13 boards in Docker, signs each image when
`OTA_SIGNING_KEY` points at the private key (it lives only on the server; the
public half is `include/ota_pubkey.h`), and, when the checkout is exactly on a
release tag, keeps a copy in `.pio/flasher-bins/vX.Y.Z/` and rewrites
`versions.json` for the flasher's "Firmware version" picker (newest five).

## New detection rules without a release

`web-flasher/signatures.txt` holds extra address-prefix (OUI) and
network-name (SSID) rules. Edit it, raise its `SERIAL`, and run the usual
redeploy (or just `build_flasher_bins.sh` with `OTA_SIGNING_KEY`): it is
signed into `manifest-signatures.json`, and every board picks the new set up
at its next boot update check -- no firmware release. Rules only add: a
prefix the firmware already knows keeps its built-in meaning. A board logs
`[rules] set N: K extra rules, signature good`.

## A lab build first

Boards with **SYSTEM -> UPDATES** set to **LAB** check for test builds
instead of releases. To try a change on your own board over WiFi before
releasing it, on the server, from the checkout you want to test:

```bash
cd /root/MuleSkin-CYD && sudo LAB_VERSION=3.1.4 OTA_SIGNING_KEY=/root/.config/muleskin/ota-signing-key.pem tools/build_flasher_bins.sh
```

It builds the checkout as `v3.1.4-lab` and publishes only
`lab-<board>-firmware.bin/.sig` and `manifest-lab-<board>.json` -- the
stable files, the archive and `versions.json` are untouched, and no
container rebuild is needed (they are served from the `/firmware` mount).
The flasher page offers them at `?lab=1`. The version must be newer than the
release the lab boards run. Then release it as usual; a lab board on
`3.1.4-lab` sees release `3.1.4` as the same version, so it stays put.

## The signing key

Boards install an update over WiFi only if it is signed with a key whose
public half is in `include/ota_pubkey.h`. Whoever holds the private key can
make every board install anything, so:

- It lives at `/root/.config/muleskin/ota-signing-key.pem` on the server
  today. Better is a machine that does not serve the web: build and sign
  there (`tools/build_flasher_bins.sh` with `OTA_SIGNING_KEY`), then copy
  `.pio/flasher-bins/` to `/root/MuleSkin-CYD/.pio/flasher-bins/` on the
  server.
- Never paste it into a chat, a ticket or a commit, and check that no copy
  sits in a shell history, a log or an AI assistant's session transcript
  (`grep -rl "PRIVATE KEY" /root` finds them).

### Rotating the signing key

If the key may have been seen, replace it. Boards trust a list of keys, so
nothing has to be reinstalled by cable:

1. `tools/new_ota_key.sh /path/to/new-key.pem` on the signing machine. It
   prints the new public key twice -- as PEM and as a C array.
2. Add both to `include/ota_pubkey.h`: the PEM to the comment, the array to
   `OTA_PUBKEYS`, **first**. Keep the old key in the list.
3. Release that build **signed with the old key**. Boards take it over WiFi
   and from then on trust both keys.
4. Sign every release after it with the new key.
5. Once boards have moved on (or a few releases later), remove the old key
   from `OTA_PUBKEYS` and the comment, and destroy the old private key.

A board that missed step 3 (left unplugged through it) still has only the
old key: it needs one install from the web flasher.

## Checking it

```bash
curl -s http://flasher.oillie.cloud/manifest-cyd-ili9341.json | grep version
curl -s -o /dev/null -w "%{http_code}\n" http://flasher.oillie.cloud/cyd-ili9341-firmware.sig
curl -s -o /dev/null -w "%{http_code}\n" http://flasher.oillie.cloud/
curl -s https://flasher.oillie.cloud/versions.json
```

Expect the new version, `200` for the signature, `301` for the page (still
redirected), and the release list. A board then logs this at boot:

```
[ota] boot check: site has v3.1.2, running v3.1.1
[ota] newer release known: 3.1.2
```

## Push alerts (ntfy)

PUSH ALERTS works with the public **ntfy.sh** and nothing here at all: the
flasher page makes a long random topic, the phone's ntfy app subscribes to it,
and the board posts to `http://ntfy.sh/<topic>`. The topic is the only
secret, and the alerts cross the internet in plain HTTP on the way.

Running your own ntfy instead keeps the alerts on this server and puts them
behind accounts. One-time setup:

1. DNS: an A record for `ntfy.oillie.cloud` pointing at this server.
2. Copy `deploy/ntfy/` to `/docker/muleskin-ntfy/`, then:
   ```bash
   cd /docker/muleskin-ntfy && sudo docker compose up -d
   ```
   Traefik picks it up from the labels: HTTPS for the phone, and plain HTTP
   on port 80 only for a POST to a `muleskin-...` topic -- the boards.
3. Accounts (each `user add` asks for a password, which you type):
   ```bash
   sudo docker exec -it muleskin-ntfy ntfy user add --role=admin <you>
   sudo docker exec -it muleskin-ntfy ntfy user add muleskin-board
   sudo docker exec muleskin-ntfy ntfy access muleskin-board 'muleskin-*' write-only
   sudo docker exec muleskin-ntfy ntfy token add muleskin-board
   ```
   The last one prints a token (`tk_...`): the board's.
4. Phone: in the ntfy app, add the server `https://ntfy.oillie.cloud`, log in
   as `<you>`, and subscribe to your `muleskin-...` topic.
5. Flasher page, SET UP PUSH ALERTS: server `ntfy.oillie.cloud`, the same
   topic, and the token.

`server.yml` sets `upstream-base-url: https://ntfy.sh`: that is how an iPhone
gets instant notifications from a self-hosted server. ntfy.sh is told only a
hash of the topic, never the message; the app fetches the message from here.

Check it from anywhere (the board's own route, plain HTTP, with the token):

```bash
curl -i -X POST http://ntfy.oillie.cloud/muleskin-yourtopic -H "Authorization: Bearer tk_..." -d "test"
```
