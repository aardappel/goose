import audio;

fn main() {
    assert(!audio::open_offline(0));
    assert(audio::create_sound([0.0]).id == 0);
    assert(audio::open_offline());
    assert(!audio::open_offline());
    assert(audio::create_sound([0.0], 7999).id == 0);
    assert(audio::create_sound([0.0], 192001).id == 0);
    assert(audio::create_sound([0.0], 48000, 3).id == 0);
    assert(audio::create_sound([0.0], 48000, 2).id == 0);
    assert(audio::create_sound([1.1]).id == 0);
    let empty: f32[>..] = [];
    assert(audio::create_sound(empty).id == 0);
    assert(audio::release_sound(audio::Sound { 0 }));
    let sound = audio::create_sound([0.25]);
    assert(audio::play(sound, -1.0).id == 0);
    assert(audio::play(sound, 1.0, 2.0).id == 0);
    assert(audio::play(sound, 1.0, 0.0, 0.0).id == 0);
    assert(audio::error().len > 0);
    var odd: f32[3] = [0.0, 0.0, 0.0];
    assert(!audio::render(odd));
    var zero: f32[>..] = [];
    assert(audio::render(zero));
    print("invalid rates, PCM, parameters, and output rejected");

    var voices: audio::Voice[>..] = [];
    for _ in 0..audio::MAX_VOICES {
        let voice = audio::play(sound, 1.0, 0.0, 1.0, true);
        assert(voice.id != 0);
        voices.push(voice);
    }
    assert(audio::play(sound).id == 0);
    audio::stop(voices[0]);
    let replacement = audio::play(sound, 1.0, 0.0, 1.0, true);
    assert(replacement.id != 0 && replacement.id != voices[0].id);
    audio::stop(voices[0]);
    assert(audio::playing(replacement));
    audio::stop_all();
    assert(!audio::set_voice(replacement, 1.0, 0.0, 1.0));
    print("voice capacity and generation safety");

    var sounds: audio::Sound[>..] = [sound];
    for _ in 1..audio::MAX_SOUNDS {
        let s = audio::create_sound([0.0]);
        assert(s.id != 0);
        sounds.push(s);
    }
    assert(audio::create_sound([0.0]).id == 0);
    assert(audio::release_sound(sound));
    let replacement_sound = audio::create_sound([0.0]);
    assert(replacement_sound.id != 0 && replacement_sound.id != sound.id);
    audio::close();
    assert(!audio::render(odd));
    print("sound capacity and recovery");
}
