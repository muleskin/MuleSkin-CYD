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
| **v1.50.0** "Roll With It" (2026-11-22) | **pending** | -- | See below. |

## v1.50.0, to sort

Likely worth taking (detection and fixes, no mascot needed):

- Signatures: dashcams (BlackVue, 70mai, Thinkware, VIOFO, Nextbase, Rove,
  Redtiger, Lytx, Vantrue), Samsara, hidden-camera setup hotspots,
  Rokid/Vuzix (META, low), Wyze F0:C8:8B / 80:48:2C, SimpliSafe F8:51:28.
  Each OUI through `tools/check_ouis.py` as usual.
- Cradlepoint / Sierra AirLink routers as AXON low -- MuleSkin left these out
  on purpose in 07aa44e; decide again with upstream's reasoning in hand.
- LISTENER type (Sonos, HomePod, GoogleHome hotspot), off by default.
- AirTag owner state (WITH OWNER / AWAY / NOT SET UP), the set-up and
  unpaired frames; **AirPods counted as AirTags** fix.
- LoRaWAN type icon drew a router under it; old Flock/ShotSpotter info text.
- Holding a LOG row opens that device's alert card; up to four catches queue
  behind an open card.
- The Trail card (places from nearby WiFi, NEW / MET BEFORE / FOLLOWING /
  SPAM FLOOD) -- large; it replaces the alert card, so it needs a MuleSkin
  design pass rather than a straight port.
- WARDRIVE page, flasher WiGLE upload and `WIGLEKEY`.

Probably not (mascot or look MuleSkin replaced): random encounters, the d20,
FIELD JOURNAL, FREE ROLL, the Mimic, Squachy's walk-off fix, the alert card
OS-window skins and DU5K theme, Network Goblin Scout (needs SquachMesh, which
MuleSkin doesn't have).

## Never porting

- **The squad mesh (SquachMesh)** and anything that rides on it.
- **The Squachy mascot's scenes and screens**: MuleSkin's main screen, splash
  and look are its own (docs/MULESKINWARE-AESTHETIC.md).
- The C5 async frame push's keyboard-flicker fix (#52) and the CALIBRATE
  TOUCH white-screen fix: MuleSkin has no counterpart of either.
- Upstream's DMA frame push on the T-Deck Plus (09744c1) and the Marauder V8
  (3fdf8bf, with its battery lamp): MuleSkin pushes with pushSprite.
