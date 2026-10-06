# PCM audio

`stdlib/audio.goose` exposes a small sound-effects API. It owns no PCM in Goose:
`create_sound` validates and copies a slice to the native layer, which keeps it
until `release_sound` or `close`. Releasing a sound stops every voice referring
to it. This makes source-array lifetimes independent of asynchronous playback.

The implementation is `src/audio/audio.c`; `audio_api.h` is its packed extern
boundary and JIT symbol list. `test/api_check.py audio` checks the C and Goose
declarations. The compiler tracks native audio calls and rejects them from
`thread_fn`, as it does for gfx, physics and ui. The native callback is C only.

## Mixing and ownership

The mixer has fixed tables of 256 sounds and 64 voices. IDs increase monotonically
and never reset, including across close/open. Exhaustion is explicit: sound or
voice creation returns zero. A completed voice frees its table slot, without
freeing PCM on the audio thread. A stale voice is harmless to stop or query.
There is no voice stealing, callback into user code, or unbounded callback work.

Each voice has a fractional source position, volume, stereo balance, pitch and
looping flag. The step is `source_rate / output_rate * pitch`. Linear
interpolation blends adjacent source frames; a loop blends the end into its
first frame, while a one-shot holds its final sample through the final fractional
frame. Mono duplicates into stereo. Pan attenuates the opposite side. All voices
sum into stereo f32 and the result clips to `[-1, 1]`. This is deliberately a
sound-effects mixer, not a band-limited music resampler or an effects engine.

`open` initializes only SDL's audio subsystem and opens a stereo f32 stream to
the default playback device. SDL performs the conversion to the physical device
format. Its get callback fills requests in blocks of at most 256 frames. The
stream lock protects tables and error text. Main-thread PCM allocation/copy and
release happen outside that lock; only table publication/removal occurs inside.
Closing destroys the stream (synchronizing with its callback), then frees PCM
and decrements only SDL's audio subsystem reference. gfx can close independently.

`open_offline` starts no SDL subsystem, device or thread. `render` advances the
same mixer synchronously, giving tests exact samples and exact completion
boundaries. It also permits a program to generate PCM for its own file writer.

## Builds and validation

`GOOSE_AUDIO` defaults on when the SDL submodule is present. `cmake/sdl.cmake`
owns the shared SDL target and collects its platform link inputs for both audio
and gfx. Audio remains available with `GOOSE_GFX=OFF` and does not depend on the
gfx Linux video-header check. `--audio-link msvc|cc` returns the response file;
`GOOSE_AUDIO_LINK` can override its directory for a moved installation.

The normal test runner includes `test/audio/audio_mixer.goose` and
`audio_errors.goose` in JIT and generated-C runs. They exercise PCM ownership,
stereo, rate conversion, interpolation, gain, pan, pitch, loop boundaries,
overlap, clipping, exhaustion, stale handles, close/open, and bad input.
`audio_err_thread.goose` tests the main-thread restriction.

The explicit `goose_audio_test` CMake target uses SDL's dummy driver to exercise
the actual callback, device-open failure and recovery, nonfinite inputs,
packed unaligned slices, concurrent table access, release during playback, and close/reopen. Run, for
example, `cmake --build build --config Release --target goose_audio_test`, then
`build/Release/goose_audio_test.exe` on Windows (or `build/goose_audio_test` with
a single-config generator). It needs no window, GPU, speakers, or audio session.
Physical-device behavior still depends on the platform backend; dummy and offline
tests do not establish audible quality, device-loss recovery, or real latency.
