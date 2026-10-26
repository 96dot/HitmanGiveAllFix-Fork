# Hitman: Blood Money GiveAll Fix (fork)

A fork of DustyRogue's [Hitman Blood Money GiveAll Fix](https://www.nexusmods.com/hitmanbloodmoney/mods/62). It makes the same two fixes, but patches the game in memory from a DLL instead of shipping a modified `HitmanBloodMoney.exe`, and it works on the Steam, GOG and disc releases.

> **AI disclosure:** this fork's code was written with Claude Code (Anthropic). DustyRogue's original patch was also made with Claude.

## What it fixes

The "Give All" cheat creates every weapon and item at once, which crashes the game in two ways:

1. **The item memory pool runs out.** The game sets up `ZItem::s_ItemMemoryAllocator` on a fixed 96 KB buffer. When it is full, creating an item hits an assertion and the game stops. This fix gives the pool a 4 MB block instead.
2. **A name lookup reads a garbage pointer.** Looking up an item's display name (for example `AllLevels/Items/Ammo_Blank/Shortname`) can walk into an entry that has no child table and pass a garbage string pointer to `_strnicmp`. This fix makes the lookup return "not found" when a table offset is above 16 MB, so callers fall back to `<NONAME>` instead of crashing.

The diagnosis and the original patch are DustyRogue's (from their own session with Claude); this fork only changes how the patch is applied.

## Differences from the original

- **No modified exe.** Everything is patched in memory at startup; the game files stay as they are, and the DRM stays intact.
- **Steam, GOG and disc.** Each build has its own addresses (see below). The build is recognised by the exe's link time, and every byte is checked before anything is written; on an unknown build, or if anything differs, nothing is changed.
- **No code cave in the exe.** The pool fix only changes two operands of the allocator's set-up call (the buffer size and address). The two lookup checks live in a small block of memory the DLL allocates.
- **SecuROM (GOG).** The GOG exe is encrypted when the DLL loads. The DLL replaces `msvcr71!_initterm` (which only calls each function in a list) and applies the patches when the game's startup calls it to run its static initialisers: after decryption, before the item pool is set up.
- **Not included:** the original's "purge loop" change (an earlier patch that keeps the level's weapon resources loaded) is part of DustyRogue's modified exe, not of the `.ips`, and is not in this fork.

## Install

Pick **one** of these:

- **`dsound.dll`** (no ASI loader needed): copy it into the game folder, next to `HitmanBloodMoney.exe`. The game loads DirectSound from its folder; this DLL applies the fix and passes every DirectSound call on to Windows' own `dsound.dll`. It works alongside mods that use `dinput8.dll` or `d3d9.dll` (for example the [Reprisal features](https://github.com/96dot/HitmanReprisalPort) mod or nemesis2000's widescreen fix).
- **`HitmanGiveAllFix.asi`**: for an ASI loader (Ultimate ASI Loader). If another mod already uses `dinput8.dll`, give the loader a different name that the game loads, such as `dsound.dll`.

The fix writes `HitmanGiveAllFix.log` next to the game. On Steam it should say:

```
HitmanGiveAllFix: Steam build
item pool: 4096 KB at ... instead of 96 KB at 00982920
lookup checks: added
```

To uninstall, delete the DLL (and the log).

### Cheats

The cheat menu opens with **C** during a mission once `EnableCheats 1` is in `HitmanBloodMoney.ini`. On the **GOG** build that works. On the **Steam** build it does not: its code sets the cheat flag to 0 when it finds `EnableCheats`, so the menu never opens there.

## Supported builds

| Build | Exe link time | Item pool set-up | Lookup checks | "Not found" exit |
|---|---|---|---|---|
| Steam | `447EF98A` | `0x73CA80` | `0x464EF9`, `0x464F3D` | `0x464F92` |
| GOG | `446B55B0` | `0x7290F0` | `0x465019`, `0x46505D` | `0x4650B2` |
| Disc (4,366,336-byte exe) | `4492B845` | `0x73D3E0` | `0x465059`, `0x46509D` | `0x4650F2` |

The disc addresses come from DustyRogue's `.ips`, which was made for that exe. An exe that has already been patched with the `.ips` (or the original mod's exe) does not match, and the DLL leaves it alone.

## Testing so far

- **Steam and GOG:** the fix loads, the pool is 4 MB in the running game, and both lookup checks are in place (read back from memory). On GOG, Give All runs from the cheat menu without crashing in the training mission.
- **Not tested yet:** the actual out-of-memory crash, which needs a later mission (DustyRogue saw it in The Hideout and A Dance With The Devil), and the disc build, for want of a copy.

## Building

Needs Visual Studio with the C++ tools (x86). Run `build.cmd`; it writes `out\dsound.dll` and `out\HitmanGiveAllFix.asi`, both from the same `dllmain.cpp`. `dllmain.cpp` also drops into DustyRogue's original Visual Studio project in place of theirs (build Release / Win32, preferably with the static runtime, `/MT`).

## Credits

- **DustyRogue** – the original [GiveAll Fix](https://www.nexusmods.com/hitmanbloodmoney/mods/62): finding both bugs and the patch this fork applies.
- **96dot** – this fork. Code written with Claude Code (Anthropic).
- IO Interactive – Hitman: Blood Money.

Not affiliated with IO Interactive.
