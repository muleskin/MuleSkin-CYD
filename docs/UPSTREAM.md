# Upstream sync log

MuleSkin forked [SquachWatch-CYD](https://github.com/skizzophrenic/SquachWatch-CYD)
at **v1.28.0**. This file records, for each upstream release since then, what
came across, in which commit, and what was left out on purpose -- so the next
sync starts from the first release not marked done, and nothing gets ported
twice or missed.

Check what's new upstream:

```sh
gh release list -R skizzophrenic/SquachWatch-CYD -L 10
gh release view v1.50.0 -R skizzophrenic/SquachWatch-CYD   # its notes
```

When you port, add the commit to that release's row and move anything you
decide against to **Never porting** (or the row's notes) with the reason.

## Status by release

| Upstream | Status | MuleSkin commits | Notes |
|---|---|---|---|
| v1.29.0, v1.30.0 | not recorded | -- | No port commit names them. Check their notes before assuming they're in. |
| v1.31.0 – v1.36.0 | done | 5d39c30 (+ boards in 0d3a22e) | GPS/WARDRIVE on `cyd-gps` only; the emulator GPS replay and main-screen GPS label not ported. |
| v1.37.0 – v1.40.0 | done | d577a3f, 53ce1f5 | MuleSkin keeps its own FMDN, SmartTag and separated-only DULT checks. |
| v1.41.0 – v1.49.0 | done | 33d2d73, e96849d, 6c24a90, 0d3a22e, d1b5186, 9075908, 46e4422, bc84783, 7d084bb, 923d7ef, c548641, 0e99a5c, 5738fa6 | Tab5: upstream's mascot screens, its Tab5 engine and the web emulator's DEMO left out. Cardputer EXT SCREEN not ported. |
| v1.50.0 "Roll With It" (2026-11-22) | done | 52be162, 22b5fab, 489fb2d | LISTENER is 21 here (MOTION had 20). The Trail drawn into MuleSkin's card, not upstream's replacement card. AUTO UPLOAD at HOME, not on the charger. Mascot content and the skins left out: see below. |

## v1.50.0, how it came across

Ported (v3.16.0):

- The signatures: dashcams, fleet cameras, hidden-camera setup hotspots,
  Wyze, SimpliSafe, Rokid and Vuzix, and Cradlepoint / Sierra AirLink at
  AXON LOW (set 3 of the rules had left them out; at LOW they log without
  alerting unless ALERT FILTER lets LOW through, which answers the reason).
- LISTENER, off by default -- numbered 21, since MuleSkin's MOTION is 20.
- AirTags: Proximity Pairing only with the AirTag's model (the AirPods
  fix), the nearby frame, and the owner state on the card (WITH OWNER /
  AWAY / NOT SET UP), read through MuleSkin's own trackerVerdict().
- Flock and ShotSpotter info text. (The LoRaWAN icon fix was already here.)
- The Trail: places.h and trail.h as upstream wrote them; on the card as a
  verdict in the banner line, a stops line, a signal ladder and the owner
  in the readout -- MuleSkin's card keeps its shape rather than being
  replaced. Catches queue behind an open card; holding a LOG row opens the
  card (with WATCH where SNOOZE is, since the card replaces the panel that
  had it).
- WARDRIVE page (CYD GPS builds), the web flasher's UPLOAD TO WIGLE and
  SEND KEY, `WIGLEKEY`, and the on-board upload on the T-Deck Plus and the
  Freenove S3 GPS builds. AUTO UPLOAD runs at HOME (main screen, idle, the
  home network heard): these boards cannot tell a charger from a full cell,
  which is what upstream waits for.

Left out on purpose (moved to Never porting): random encounters, the d20,
FIELD JOURNAL, FREE ROLL, the Mimic, Squachy's walk-off fix, the alert card's
eleven OS-window skins and the DU5K theme, Network Goblin Scout.

## Never porting

- **The squad mesh (SquachMesh)** and anything that rides on it.
- **The Squachy mascot's scenes and screens**: MuleSkin's main screen, splash
  and look are its own (docs/MULESKINWARE-AESTHETIC.md). That takes v1.50.0's
  random encounters, d20, FIELD JOURNAL, FREE ROLL and the Mimic with it,
  and the walk-off fix for a Squachy MuleSkin does not draw.
- **v1.50.0's alert card skins and the DU5K theme**: an OS-window look over
  the card. MuleSkin's card has its own look; the Trail went into it instead.
- **Network Goblin Scout** (v1.50.0): its goblins arrive over SquachMesh.
- The C5 async frame push's keyboard-flicker fix (#52) and the CALIBRATE
  TOUCH white-screen fix: MuleSkin has no counterpart of either.
- Upstream's DMA frame push on the T-Deck Plus (09744c1) and the Marauder V8
  (3fdf8bf, with its battery lamp): MuleSkin pushes with pushSprite.
