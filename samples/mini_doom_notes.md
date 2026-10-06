# Mini Doom: implementation notes and Goose feedback

`31_mini_doom.goose` is an independent game built around the supplied Freedoom
E1M1 WAD. The source stays in one file and reads from asset loading down to
the update/draw loop. It imports `gfx` and `audio`; neither `ui` nor `physics`
was needed. The WAD already has a bitmap font, and map-plane collision is
shorter to explain directly than as a second physics representation.

## Scope and tradeoffs

The sample loads the original sector geometry, composed wall textures,
64×64 flats, palette patches, sprites, font and PCM sounds. BSP clipping
produces complete convex floor/ceiling cells, including the otherwise
missing edges between subsectors. The GPU draws triangles with a depth
buffer and alpha-tested sprites. This is not Doom's software renderer.

The gameplay supports all specials present in this map: ordinary, fast and
blue-key doors, triggered doors that stay open, lifts, a lowering floor and
the exit. It includes stairs, falling, damage, armor, health/ammo pickups,
secrets, four enemy families, imp fireballs, barrels and four weapons.
Walking, sector motion, combat and animation advance at 60 fixed steps per
second. Pause/focus handling stops simulation and releases mouse capture.

The deliberate limits are documented in the source and sample README.
Monsters pursue directly with sliding, so they can get stuck around complex
obstacles. They use frontal artwork and approximate attack timing. Sight
and shots are geometric, but combat, armor and lighting are not intended to
match Doom exactly. Unsupported weapons and their ammo become bullets.
Music would need a MUS/MIDI decoder and synthesizer; sound effects provide
more immediate gameplay value for much less code. There is no save system.

The map is small enough to rebuild its triangle list each rendered frame.
That keeps moving floors, doors and sprites easy to follow. Static mesh
caching, spatial buckets, directional sprites and exact monster state
machines are reasonable next projects, but were not necessary for this
sample. The atlas is 2048×4096 RGBA8; approximately half its height is used.

## Validation

The checked-in `--test` mode exercises:

- WAD decoding, map counts, supported specials, and BSP triangle centroids.
- Player start, movement and solid-wall collision/visibility.
- Real blue-key pickup/use, passage through an opened door, and obstruction
  protection when a door closes.
- Tagged lift lowering, waiting, rising, and carrying the player.
- Weapon/ammo/health pickups, armor, shooting, death drops and barrel chains.
- Ten seconds of the real monster update and mesh construction.
- Automated walks from the start to the key and from the key to the exit,
  using normal movement, use and sector updates after planning a route.
  These navigation checks remove enemies and ignore floor damage, so they
  validate traversal rather than prove a human combat playthrough.
- Offline decoding and mixing of the WAD pistol sound, when audio is built in.
- Exit activation and complete gameplay reset.

Validation also used generated C compiled with MSVC and the TinyCC backend,
and headless `gfx` rendering. Startup and courtyard images were visually
inspected. A temporary input harness used `gfx::inject_*` to check walking,
focus loss/gain, Escape resume, a press-and-release fire click in one frame,
map/help, death and restart. This was a Windows run; other platforms have
not been exercised.

The sample runner's arguments and expected output live in
`data/mini_doom.args` and `expected/mini_doom.out`. Run the focused checks with:

```text
goose samples/31_mini_doom.goose -- --test
goose samples/31_mini_doom.goose -- --test --frames 30
```

The second command opens a window unless `GOOSE_GFX_HEADLESS=1` is set.

## Language and API feedback

No confirmed compiler or native API correctness bug remains from this work.
The issues found in the game itself—floating-point slivers in clipping,
tangent-wall movement, viewport/crosshair alignment, and short-click input—
were fixed in the sample. The following are concrete ergonomics/API requests,
not claims of compiler bugs:

1. **A checked binary reader would remove repeated plumbing.** WAD parsing
   needs little-endian unsigned/signed 16-bit fields, unsigned 32-bit fields,
   bounded byte slices, and fixed-width NUL-terminated names. `from_bytes`
   serves Goose's serialization format, not arbitrary external formats.
   The local `u16le`, `i16le`, `u32le`, `name_at`, and bounds helpers are small,
   but a cursor reader with those operations and useful offsets on failure
   would make this kind of sample substantially easier to start.

2. **Recursive algorithms cannot keep growable scratch across recursive
   calls.** The first BSP implementation returned growable clipped polygons;
   compilation rejected calling `build_floors` with those locals in scope
   under §7.8. The sample now uses `float2[..128]`, with a checked corner
   limit. That is adequate here and keeps frames fixed in size, but imposes
   a limit on an otherwise naturally variable-size algorithm. An official
   pattern for recursive scratch arenas or an explicit work stack would
   help newcomers understand the intended solution. The diagnostic itself
   clearly identified the problem.

3. **Mixed string-producing branches need explicit ownership.** HUD labels
   such as `if dead { "STFDEAD0" } else { str(...) }` inferred a slice tied
   to a temporary and were rejected. Declaring the result `u8[>..]` fixes it.
   The diagnostic suggests slicing the whole construct for branches of one
   array type; for mixed literals/builders it could additionally recommend
   an explicitly owned result. The final sample shows that annotation for
   `face` and `ammo`.

4. **Named struct fields still require declaration order.** Test actor
   literals written with `health` before `radius` failed until reordered.
   The diagnostic explains front-to-back construction well. Since these
   particular fields are all scalar and their initializers have no side
   effects, allowing this case would reduce friction without changing the
   lifetime rules for variable-size fields. Alternatively, editor completion
   that inserts fields in the required order would help.

5. **Asset path resolution is manual.** The sample probes the repository
   root and `samples` working directories, with `--wad` as an escape hatch.
   A documented resource-path helper that behaves consistently for JIT,
   generated executables and packaged programs would avoid duplicating this
   in each file-backed sample. A compile-time source path alone would not
   solve relocatable distribution.

6. **An optional sprite/text convenience layer could sit above `gfx`.**
   Atlas allocation, pixel-to-UV rectangles, quad emission, packed vertex
   layouts and GPU buffer growth are written explicitly here. That is useful
   teaching material, but a small reusable batch/bitmap-font layer could
   reduce the setup cost for smaller games without requiring widgets or
   hiding `gfx`'s lower-level API. The existing raw texture upload and
   nearest sampler APIs were sufficient; no native patch was needed.

7. **Legacy music is a separate missing facility.** `audio`'s PCM creation,
   pan, pitch, concurrent voices and offline mixer cover sound effects well.
   Playing this WAD's MUS soundtrack would require decoding and synthesis
   outside the current PCM interface. A general decoder/plugin approach
   seems more reusable than adding Doom-specific code to the core API.

Particularly useful existing features were inline fixed records, bounded
arrays, `bytes_of` for GPU uploads, embedded shader compilation, explicit
headless rendering, queued input injection, and offline audio. Those made
it possible to validate most of the game without external asset conversion
or a separate native helper.

## Format references

The implementation was written independently. Field layouts and special
numbers were checked against id Software's published
[map format declarations](https://github.com/id-Software/DOOM/blob/master/linuxdoom-1.10/doomdata.h),
[line specials](https://github.com/id-Software/DOOM/blob/master/linuxdoom-1.10/p_spec.c),
[switch handling](https://github.com/id-Software/DOOM/blob/master/linuxdoom-1.10/p_switch.c),
[doors](https://github.com/id-Software/DOOM/blob/master/linuxdoom-1.10/p_doors.c),
and [lifts](https://github.com/id-Software/DOOM/blob/master/linuxdoom-1.10/p_plats.c).
The supplied WAD remains the source of all game artwork and sound.
