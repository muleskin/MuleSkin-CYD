# Cardputer ADV hotkeys

The Cardputer's screen is 240x135. Buttons are small and the cursor takes a
lot of arrow presses to get anywhere. Letters skip all of that: press the
first letter of a button and it is pressed. (Upstream SquachWatch v1.43,
laid out for MuleSkin's screens.)

## The four rules

1. **A letter presses the button whose label starts with it.** HUNT is H,
   SNOOZE is S, CANCEL is C, BACK is B. Brackets and arrows in a label do
   not count: `< LOG >` is L. (Upstream keeps B for the background; MuleSkin
   has only RADAR.)
2. **Two buttons with the same letter:** the key moves the cursor between
   them. ENTER presses the one it is on. Nothing is pressed by surprise.
3. **ESC is back. Hold ESC 3 seconds for home**, from anywhere, even while
   typing.
4. **On a text screen letters type.** The WiFi password, MY ALERTS' pattern
   and label, the PIN. Hotkeys come back when you leave it.

## Keys that work everywhere

| Key | What |
|-----|------|
| ESC or DEL | Back one screen. On the main screen, opens the menu |
| ESC held 3 s | Home |
| Arrows (`;` `,` `.` `/`) or TAB | Move the cursor |
| ENTER or SPACE | Press what the cursor is on (held is held: a long press is still one) |

## Screen by screen

| Screen | Keys |
|--------|------|
| Main screen | **W** WIFI TIME, **L** LOG, **M** IN MEETING, **T** TODAY, **S** the menu. ESC opens the menu |
| Time zone card | **T** this is right; the arrows and ENTER for < and > |
| ALERT | **H** hunt, **S** snooze (again: longer), **I** steps between INFO and IGNORE (**M** once it says MUTED). ESC dismisses |
| LOG | **L** home. Not **C**: CLR wipes the log with no question, so it is never one stray key away |
| LOG row (ENTER on it) | **W** watch, **H** hunt, **I** ignore, **M** more info, **C** cancel. ESC cancels |
| HUNT | **S** stop hunting. ESC back |
| WIFI NETWORKS | **U** use, **R** remove, **A** add. ESC back |
| Add a network | **R** rescan. ESC back |
| MY ALERTS | **A** add, **L** label, **R** remove. ESC back |
| UPDATE FIRMWARE | **U** steps between the two UPDATE OVER buttons. ESC back or cancel |
| AUTO UPDATE countdown | **N** now, **S** skip. ESC skips |
| iBeacon question | **E** enable, **K** keep disabled. ESC keeps them off |
| CHECK COLORS | **I** invert, **O** colour order, **L** looks good. ESC is the same as LOOKS GOOD |
| Settings and its pages | Letters do nothing on the rows (they are not buttons): arrows and ENTER |

## Typing

| Key | On a text screen |
|-----|------------------|
| Letters, digits, symbols | Typed. SHIFT for capitals and the upper symbols |
| DEL | Deletes the last character |
| ENTER | OK: joins the network, saves the pattern or label, checks the PIN |
| TAB | Shows or hides a password |
| FN + arrow | Moves the cursor instead of typing `;` `,` `.` `/` |
| ESC | Back, without saving |
