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
