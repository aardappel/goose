// Events in the physics module, each read after the step it happened in: a
// ball dropped on the ground begins touching it with a hit fast enough to
// report, its contact's manifolds read through the contact, and stops
// touching when lifted away; a box falls through a sensor, which reports it
// entering, holding it and leaving; moving bodies report their moves and
// when they fall asleep.
import std;
import vec;
import math;
import physics;

fn num(x: f32) -> u8[>..] {
    let k = round(x as f64 * 100.0) as i64;
    var s: u8[>..] = [];
    if k < 0 { s.push('-'); }
    format_int(s, abs(k) / 100, 10, 0, ' ');
    s.push('.');
    format_int(s, abs(k) % 100, 10, 2, '0');
    return s;
}

fn vec3(v: float3) -> u8[>..] { str(num(v.x), " ", num(v.y), " ", num(v.z)) }

let DT: f32 = 1.0 / 60.0;

// Whether an event is between these two shapes, either way round.
fn between(a: physics::Shape, b: physics::Shape, x: physics::Shape, y: physics::Shape) -> bool {
    (a == x && b == y) || (a == y && b == x)
}

fn main() {
    let world = physics::create_world(physics::WorldDef { hit_event_threshold: 1.0, workers: 1 });
    let ground = physics::create_body(world, physics::BodyDef {});
    let floor = physics::create_box_shape(ground, physics::ShapeDef {}, float3 { 10.0, 0.5, 10.0 },
                                          physics::Transform { p: float3 { 0.0, -0.5, 0.0 },
                                                               q: physics::quat_identity });

    // --- contacts ------------------------------------------------------------
    let ball = physics::create_body(world, physics::BodyDef { body_type: physics::DYNAMIC,
                                                              position: float3 { 0.0, 3.0, 0.0 },
                                                              user_data: 8 });
    let round_shape = physics::create_sphere_shape(ball, physics::ShapeDef {
        material: physics::SurfaceMaterial { user_material_id: 3 }, enable_contact_events: true,
        enable_hit_events: true }, physics::Sphere { center: float3_0, radius: 0.5 });
    var began = 0;
    var hits = 0;
    var steps = 0;
    var contact = physics::Contact { 0, 0, 0, 0 };
    var speed: f32 = 0.0;
    var hit_point = float3_0;
    var material: u64 = 0;
    while began == 0 && steps < 120 {
        physics::step(world, DT, 4);
        steps++;
        for e in physics::contact_begin_events(world) {
            if between(e.shape_a, e.shape_b, round_shape, floor) {
                began++;
                contact = e.contact;
            }
        }
        for h in physics::contact_hit_events(world) {
            if between(h.shape_a, h.shape_b, round_shape, floor) {
                hits++;
                speed = h.approach_speed;
                hit_point = h.point;
                material = if h.shape_a == round_shape { h.material_a } else { h.material_b };
            }
        }
    }
    print("the ball touched the ground within a second: ", began == 1, "; hit ", hits == 1,
          " faster than 5 m/s ", speed > 5.0, " at ", vec3(hit_point), ", material ", material);
    print("its contact valid: ", physics::is_valid(contact));
    // Speculative: a contact begins a little before the shapes meet. Once
    // resting, it has its point.
    for i in 30 { physics::step(world, DT, 4); }
    let mf = physics::manifolds(contact);
    print("manifolds: ", mf.len, " with ", mf[0].point_count, " point, normal ", vec3(mf[0].normal),
          ", separation ", num(mf[0].points[0].separation), ", pushing ",
          mf[0].points[0].normal_impulse > 0.0, ", same shapes ",
          between(mf[0].shape_a, mf[0].shape_b, round_shape, floor));
    let from_shape = physics::contacts(round_shape);
    print("the ball's shape has ", from_shape.len, " contact, the same ",
          from_shape[0].contact == contact);

    // Lifted away, the contact ends.
    physics::set_transform(ball, float3 { 0.0, 5.0, 0.0 }, physics::quat_identity);
    var ended = false;
    for i in 3 {
        physics::step(world, DT, 4);
        for e in physics::contact_end_events(world) {
            ended = ended || between(e.shape_a, e.shape_b, round_shape, floor);
        }
    }
    print("lifted: the contact ended ", ended, ", now valid ", physics::is_valid(contact));

    // --- sensors -------------------------------------------------------------
    let gate = physics::create_body(world, physics::BodyDef { position: float3 { 5.0, 3.0, 0.0 } });
    let sensor = physics::create_box_shape(gate, physics::ShapeDef { is_sensor: true,
                                                                     enable_sensor_events: true },
                                           float3 { 1.0, 1.0, 1.0 });
    let crate = physics::create_body(world, physics::BodyDef {
        body_type: physics::DYNAMIC, position: float3 { 5.0, 6.0, 0.0 } });
    let crate_shape = physics::create_box_shape(crate,
                                                physics::ShapeDef { enable_sensor_events: true },
                                                float3 { 0.25, 0.25, 0.25 });
    print("sensor ", physics::is_sensor(sensor), ", events ",
          physics::sensor_events_enabled(sensor));
    var entered = -1;
    var left = -1;
    var held = false;
    for i in 90 {
        physics::step(world, DT, 4);
        for e in physics::sensor_begin_events(world) {
            if e.sensor == sensor && e.visitor == crate_shape { entered = i; }
        }
        for e in physics::sensor_end_events(world) {
            if e.sensor == sensor && e.visitor == crate_shape { left = i; }
        }
        if entered >= 0 && left < 0 {
            let inside = physics::sensor_overlaps(sensor);
            held = held || (inside.len == 1 && inside[0] == crate_shape);
        }
    }
    print("the crate fell through the sensor: entered ", entered >= 0, ", held ", held,
          ", left after ", left > entered, "; now at ", num(physics::position(crate).y));
    print("the sensor holds nothing now: ", physics::sensor_overlaps(sensor).len == 0);

    // --- moves ---------------------------------------------------------------
    // Dropped again from a little higher: it falls, lands, rests and sleeps.
    physics::set_transform(ball, float3 { -5.0, 2.0, 0.0 }, physics::quat_identity);
    physics::set_awake(ball, true);
    var moved = false;
    var asleep = false;
    var user: u64 = 0;
    var last = physics::transform_identity;
    for i in 200 {
        physics::step(world, DT, 4);
        for e in physics::body_move_events(world) {
            if e.body == ball {
                moved = true;
                user = e.user_data;
                last = e.transform;
                asleep = asleep || e.fell_asleep;
            }
        }
    }
    print("the ball's moves reported: ", moved, ", user data ", user, ", fell asleep ", asleep,
          ", last at ", vec3(last.p), "; awake now ", physics::is_awake(ball));
    physics::destroy(world);
}
