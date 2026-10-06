// The real playback mixer, advanced by exact frame counts without a device.
import audio;

fn expect(out: const f32[:], expected: const f32[:]) {
    assert(out == expected);
}

fn main() {
    assert(audio::available());
    assert(audio::open_offline(48000));
    var pcm: f32[>..] = [0.0, 0.5, 1.0, -1.0];
    let sound = audio::create_sound(pcm);
    assert(sound.id != 0);
    pcm[0] = 0.75; // Mutating the original must not change the copied sound.
    pcm.resize(0);  // Native playback owns a copy, not this Goose allocation.
    let voice = audio::play(sound);
    var out: f32[8] = [0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0];
    assert(audio::render(out));
    expect(out, [0.0, 0.0, 0.5, 0.5, 1.0, 1.0, -1.0, -1.0]);
    assert(!audio::playing(voice));
    assert(audio::render(out));
    expect(out, [0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0]);
    print("mono copied, completed, silence");

    // Half-speed linear interpolation; changing parameters does not restart.
    let slow = audio::play(sound, 0.5, -1.0, 0.5);
    assert(audio::render(out));
    expect(out, [0.0, 0.0, 0.125, 0.0, 0.25, 0.0, 0.375, 0.0]);
    assert(audio::set_voice(slow, 1.0, 1.0, 1.0));
    assert(audio::render(out));
    expect(out, [0.0, 1.0, 0.0, -1.0, 0.0, 0.0, 0.0, 0.0]);
    print("gain, pan, interpolation, live update");

    let stereo = audio::create_sound([0.5, -0.5, 1.0, -1.0], 24000, 2);
    let looping = audio::play(stereo, 1.0, 0.0, 1.0, true);
    assert(audio::render(out));
    expect(out, [0.5, -0.5, 0.75, -0.75, 1.0, -1.0, 0.75, -0.75]);
    assert(audio::playing(looping));
    audio::stop(looping);
    assert(!audio::playing(looping));
    print("stereo, sample rates, loop boundary, stop");

    let loud = audio::create_sound([0.75, -0.75]);
    let first = audio::play(loud);
    let second = audio::play(loud);
    assert(first.id != second.id);
    assert(audio::render(out));
    expect(out, [1.0, 1.0, -1.0, -1.0, 0.0, 0.0, 0.0, 0.0]);
    let retained = audio::play(loud, 1.0, 0.0, 1.0, true);
    assert(audio::release_sound(loud));
    assert(!audio::playing(retained));
    assert(!audio::release_sound(loud));
    assert(audio::play(loud).id == 0);
    audio::stop(retained);
    print("overlap, clipping, release stops voices, stale handles");

    let old = audio::play(sound, 1.0, 0.0, 1.0, true);
    audio::close();
    audio::close();
    assert(!audio::playing(old));
    assert(audio::open_offline());
    let fresh = audio::create_sound([1.0]);
    assert(fresh.id != sound.id);
    assert(audio::play(sound).id == 0);
    let fresh_voice = audio::play(fresh, 1.0, 0.0, 1.0, true);
    audio::stop(old);
    assert(audio::playing(fresh_voice));
    var long_output: f32[>..] = [];
    long_output.resize(1200, 0.0);
    assert(audio::render(long_output)); // Cross multiple native block boundaries.
    for sample in long_output { assert(sample == 1.0); }
    audio::stop_all();
    assert(!audio::playing(fresh_voice));
    audio::close();
    print("close/open invalidates handles, stop all");
}
