// PCM sound effects, independent of gfx or a window. The optional native
// layer uses SDL3 for playback and the same mixer for offline output.
// All calls belong on the main thread; SDL's audio thread never runs Goose.
namespace audio;
import std;

let MAX_SOUNDS: i32 = 256;
let MAX_VOICES: i32 = 64;

// Zero means no resource. Handles are not reused after release or close.
struct Sound { id: u64 }
struct Voice { id: u64 }

extern "gs_audio_available" fn available() -> bool;
extern "gs_audio_open" fn open_raw(sample_rate: i64, offline: bool) -> bool;

// Open the default device. Rates for output and sounds: 8000..192000 Hz.
// Only one mixer may be open. Failed device creation leaves audio closed.
fn open(sample_rate: i64 = 48000) -> bool { open_raw(sample_rate, false) }

// No device or background thread; render() advances playback explicitly.
fn open_offline(sample_rate: i64 = 48000) -> bool { open_raw(sample_rate, true) }

// Stop playback and release all sounds. Harmless when already closed.
extern "gs_audio_close" fn close();
extern "gs_audio_error" fn error_into(out: u8[:]) -> i64;

// Last error: failed calls return false or a zero handle; successes do not
// clear this message. Invalid arguments and resource exhaustion are recoverable.
fn error() -> u8[>..] {
    var text: u8[>..] = [];
    text.resize(1024, 0);
    let n = error_into(text);
    text.resize(min(n, 1024));
    return text;
}

// Copies finite f32 PCM in [-1, 1] into native-owned storage. The source can
// immediately be changed or freed. Mono or interleaved stereo (left/right).
extern "gs_audio_create_sound" fn create_sound_raw(samples: const f32[:],
                                                  sample_rate: i64, channels: i64) -> Sound;
fn create_sound(samples: const f32[:], sample_rate: i64 = 48000,
                channels: i64 = 1) -> Sound {
    create_sound_raw(samples, sample_rate, channels)
}

// Stops every voice using the sound, then frees its samples. Zero is harmless;
// a stale nonzero handle returns false with an error.
extern "gs_audio_release_sound" fn release_sound(sound: Sound) -> bool;

// Each play starts an independent voice. Limits: 256 sounds, 64 simultaneous
// voices. Completed voices recycle automatically; exhaustion never steals.
// volume: 0..1; pan: -1 (left)..1 (right), 0 centered; pitch: 0.125..8.
// Pan attenuates the opposite channel (stereo balance). Pitch changes speed
// and duration together. A looping voice repeats until stopped.
extern "gs_audio_play" fn play_raw(sound: Sound, volume: f32, pan: f32,
                                   pitch: f32, looping: bool) -> Voice;
fn play(sound: Sound, volume: f32 = 1.0, pan: f32 = 0.0,
        pitch: f32 = 1.0, looping: bool = false) -> Voice {
    play_raw(sound, volume, pan, pitch, looping)
}

// Completed/stopped/stale/zero voices are not playing; stop is a no-op.
// Device buffers can still contain a little already mixed sound after stop.
extern "gs_audio_playing" fn playing(voice: Voice) -> bool;
extern "gs_audio_stop" fn stop(voice: Voice);
extern "gs_audio_stop_all" fn stop_all();

// Update without restarting. A completed voice returns false with an error.
extern "gs_audio_set_voice" fn set_voice(voice: Voice, volume: f32,
                                         pan: f32, pitch: f32) -> bool;

// Offline only: fill an even-length slice with stereo f32 output, advancing
// by len(out)/2 frames. Empty is valid. Samples are clipped to [-1, 1].
// Device playback uses this same linear-interpolation mixer automatically.
extern "gs_audio_render" fn render(out: f32[:]) -> bool;
