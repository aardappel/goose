// Recording and replay in the physics module: a world recorded while a pile
// of boxes falls, the recording's bytes, saved to a file and loaded back,
// replayed in a world of its own to check it comes out the same, and a
// player stepping through it, seeking, restarting, and reading the replayed
// bodies, which end where the recorded ones did.
import std;
import vec;
import math;
import os;
import physics;

fn num(x: f32) -> u8[>..] {
    let k = round(x as f64 * 1000.0) as i64;
    var s: u8[>..] = [];
    if k < 0 { s.push('-'); }
    format_int(s, abs(k) / 1000, 10, 0, ' ');
    s.push('.');
    format_int(s, abs(k) % 1000, 10, 3, '0');
    return s;
}

let DT: f32 = 1.0 / 60.0;

fn main() {
    let world = physics::create_world(physics::WorldDef { workers: 1 });
    let recording = physics::create_recording();
    print("recording valid ", physics::is_valid(recording), ", empty ",
          physics::size(recording) == 0);
    physics::start_recording(world, recording);
    let ground = physics::create_body(world, physics::BodyDef {
        position: float3 { 0.0, -1.0, 0.0 } });
    physics::create_box_shape(ground, physics::ShapeDef {}, float3 { 20.0, 1.0, 20.0 });
    var boxes: physics::Body[>..] = [];
    for i in 6 {
        let b = physics::create_body(world, physics::BodyDef {
            body_type: physics::DYNAMIC,
            position: float3 { 0.3 * i as f32, 0.5 + 1.1 * i as f32, 0.0 } });
        physics::create_box_shape(b, physics::ShapeDef {}, float3 { 0.5, 0.5, 0.5 });
        boxes.push(b);
    }
    for i in 90 { physics::step(world, DT, 4); }
    physics::stop_recording(world);
    let size = physics::size(recording);
    let bytes = physics::bytes(recording);
    print("recorded ", size > 0, " bytes, read back whole: ", bytes.len == size);

    // Through a file and back.
    let path = "physics_recording_tmp.b3r";
    print("saved ", physics::save(recording, path), ", on disk ", file_exists(path));
    let loaded = physics::load_recording(path);
    print("loaded the same size: ", physics::size(loaded) == size, ", same bytes ",
          physics::bytes(loaded) == bytes);
    delete_file(path);
    let missing = physics::load_recording("no_such_recording.b3r");
    print("a missing file: handle ", missing.id, ", why: ", physics::error());

    // Replayed in a world of its own, with one worker and with two.
    print("replays the same: ", physics::validate_replay(loaded, 1), " ",
          physics::validate_replay(loaded, 2));

    let player = physics::create_player(loaded, 1);
    let info = physics::info(player);
    print("player: ", physics::frame_count(player), " frames of ", num(info.time_step), " s in ",
          info.sub_step_count, " sub-steps, at frame ", physics::frame(player), " with ",
          physics::body_count(player), " bodies made");
    // Half a step makes the first frame's bodies, where they start.
    physics::sub_step(player);
    print("half a step: ", physics::body_count(player), " bodies made, before the step ",
          physics::at_pre_step(player), ", the top box at ",
          num(physics::position(physics::body(player, 6)).y));
    var frames = 0;
    while physics::step(player) { frames++; }
    print("stepped ", frames, " frames to the end ", physics::at_end(player), "; diverged ",
          physics::diverged(player), " (frame ", physics::diverge_frame(player), ")");
    // Recorded body k is the one made k-th: the ground, then the boxes.
    var same = true;
    for i in 6 {
        let replayed = physics::body(player, i + 1);
        same = same && physics::transform(replayed) == physics::transform(boxes[i]);
    }
    print("the replayed boxes end exactly where the recorded ones did: ", same);
    let pw = physics::world(player);
    print("the replay world is a world of its own: ", physics::is_valid(pw) && pw != world);
    physics::seek(player, 30);
    print("seeking back to frame 30: at ", physics::frame(player));
    physics::restart(player);
    print("restarted: at ", physics::frame(player), " with ", physics::body_count(player),
          " bodies made");
    physics::set_worker_count(player, 2);
    frames = 0;
    while physics::step(player) { frames++; }
    print("again with two workers: ", frames, " frames, diverged ", physics::diverged(player));

    physics::destroy(player);
    print("player gone: ", physics::is_valid(player), "; its world too: ", physics::is_valid(pw));
    physics::destroy(loaded);
    physics::destroy(recording);
    print("recordings gone: ", physics::is_valid(recording), " ", physics::is_valid(loaded));
    physics::destroy(world);
}
