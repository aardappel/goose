// The physics module's library and worlds: Box3D's version, the Goose field
// defaults against Box3D's own, a world's settings read back after creation
// and after changing them, a pile of boxes stepped until it sleeps, the same
// pile stepped with one and with four worker threads to the same result,
// statistics, an explosion, and worlds counted as they come and go.
import std;
import vec;
import math;
import physics;

// A number rounded to thousandths, without the sign of a zero.
fn num(x: f32) -> u8[>..] {
    let k = round(x as f64 * 1000.0) as i64;
    var s: u8[>..] = [];
    if k < 0 { s.push('-'); }
    format_int(s, abs(k) / 1000, 10, 0, ' ');
    s.push('.');
    format_int(s, abs(k) % 1000, 10, 3, '0');
    return s;
}

fn vec3(v: float3) -> u8[>..] { str(num(v.x), " ", num(v.y), " ", num(v.z)) }

// A ground slab and ten boxes in a leaning stack above it.
fn pile(world: physics::World, boxes: physics::Body[>..]&) {
    let ground = physics::create_body(world, physics::BodyDef {
        position: float3 { 0.0, -1.0, 0.0 } });
    physics::create_box_shape(ground, physics::ShapeDef {}, float3 { 20.0, 1.0, 20.0 });
    for i in 10 {
        let b = physics::create_body(world, physics::BodyDef {
            body_type: physics::DYNAMIC,
            position: float3 { 0.1 * i as f32, 0.5 + 1.05 * i as f32, 0.07 * i as f32 },
            rotation: physics::quat_axis_angle(float3 { 0.0, 1.0, 0.0 }, 0.2 * i as f32) });
        physics::create_box_shape(b, physics::ShapeDef {}, float3 { 0.5, 0.5, 0.5 });
        boxes.push(b);
    }
}

fn main() {
    // Units are set before anything is made: meters, as by default.
    physics::set_length_units_per_meter(1.0);
    let v = physics::version();
    print("Box3D ", v.major, ".", v.minor, ".", v.revision, "; available ", physics::available(),
          "; ", num(physics::length_units_per_meter()), " length units per meter; misuses ",
          physics::misuse_count());
    physics::check();

    // Every definition's Goose defaults are Box3D's.
    print("defaults: world ", physics::default_world_def() == physics::WorldDef {},
          ", body ", physics::default_body_def() == physics::BodyDef {},
          ", shape ", physics::default_shape_def() == physics::ShapeDef {},
          ", material ", physics::default_surface_material() == physics::SurfaceMaterial {},
          ", filter ", physics::default_filter() == physics::Filter {},
          ", query filter ", physics::default_query_filter() == physics::QueryFilter {},
          ", explosion ", physics::default_explosion_def() == physics::ExplosionDef {});
    print("joint defaults: distance ",
          physics::default_distance_joint_def() == physics::DistanceJointDef {},
          ", filter ", physics::default_filter_joint_def() == physics::FilterJointDef {},
          ", motor ", physics::default_motor_joint_def() == physics::MotorJointDef {},
          ", parallel ", physics::default_parallel_joint_def() == physics::ParallelJointDef {},
          ", prismatic ", physics::default_prismatic_joint_def() == physics::PrismaticJointDef {},
          ", revolute ", physics::default_revolute_joint_def() == physics::RevoluteJointDef {},
          ", spherical ", physics::default_spherical_joint_def() == physics::SphericalJointDef {},
          ", weld ", physics::default_weld_joint_def() == physics::WeldJointDef {},
          ", wheel ", physics::default_wheel_joint_def() == physics::WheelJointDef {});

    let worlds = physics::world_count();
    let world = physics::create_world(physics::WorldDef {
        gravity: float3 { 0.0, -9.8, 0.0 }, restitution_threshold: 0.5, hit_event_threshold: 2.0,
        contact_hertz: 25.0, contact_damping_ratio: 5.0, contact_speed: 2.0,
        maximum_linear_speed: 100.0, enable_sleep: true, enable_continuous: false, workers: 1,
        friction_mixing: physics::MIX_MAX, restitution_mixing: physics::MIX_MIN, user_data: 42,
        capacity: physics::Capacity { dynamic_bodies: 64 } });
    print("valid ", physics::is_valid(world), "; worlds ", physics::world_count() - worlds,
          " more");
    print("gravity ", vec3(physics::gravity(world)), "; restitution threshold ",
          num(physics::restitution_threshold(world)), "; hit threshold ",
          num(physics::hit_event_threshold(world)), "; top speed ",
          num(physics::maximum_linear_speed(world)));
    print("sleeping ", physics::sleeping_enabled(world), ", continuous ",
          physics::continuous_enabled(world), ", warm starting ",
          physics::warm_starting_enabled(world), ", user data ", physics::user_data(world),
          ", workers ", physics::worker_count(world));

    physics::set_gravity(world, float3 { 0.0, -10.0, 0.0 });
    physics::set_restitution_threshold(world, 1.5);
    physics::set_hit_event_threshold(world, 3.0);
    physics::set_maximum_linear_speed(world, 200.0);
    physics::enable_continuous(world, true);
    physics::enable_sleeping(world, false);
    print("sleeping off: ", !physics::sleeping_enabled(world));
    physics::enable_sleeping(world, true);
    physics::enable_warm_starting(world, false);
    physics::enable_warm_starting(world, true);
    physics::set_user_data(world, 7);
    physics::set_contact_tuning(world, 30.0, 10.0, 3.0);
    physics::set_contact_recycle_distance(world, 0.02);
    physics::set_friction_mixing(world, physics::MIX_AVERAGE);
    physics::set_restitution_mixing(world, physics::MIX_MAX);
    physics::set_worker_count(world, 2);
    physics::set_worker_count(world, 1);
    print("now gravity ", vec3(physics::gravity(world)), "; thresholds ",
          num(physics::restitution_threshold(world)), " and ",
          num(physics::hit_event_threshold(world)), "; top speed ",
          num(physics::maximum_linear_speed(world)), "; continuous ",
          physics::continuous_enabled(world), "; user data ", physics::user_data(world),
          "; recycle distance ", num(physics::contact_recycle_distance(world)));

    // The pile settles and falls asleep.
    var boxes: physics::Body[>..] = [];
    pile(world, boxes);
    print("awake at the start: ", physics::awake_body_count(world));
    var steps = 0;
    while physics::awake_body_count(world) > 0 && steps < 1000 {
        physics::step(world, 1.0 / 60.0, 4);
        steps++;
    }
    print("asleep within 1000 steps: ", physics::awake_body_count(world) == 0);
    var lowest: f32 = 100.0;
    for b in boxes { lowest = min(lowest, physics::position(b).y); }
    print("lowest box center above the ground: ", lowest > 0.45);
    let c = physics::counters(world);
    print("counters: ", c.body_count, " bodies, ", c.shape_count, " shapes, ", c.joint_count,
          " joints, contacts ", c.contact_count > 0, ", islands ", c.island_count >= 1);
    let p = physics::profile(world);
    print("profile times not negative: ", p.step >= 0.0 && p.collide >= 0.0 && p.solve >= 0.0);
    let cap = physics::max_capacity(world);
    print("capacity seen: ", cap.dynamic_bodies >= 10, " ", cap.static_bodies >= 1);
    let box = physics::bounds(world);
    print("bounds hold the ground: ", box.lower.x <= -20.0 && box.upper.x >= 20.0);

    // An explosion at the middle of the ground wakes the pile and throws it
    // outward.
    let before = physics::position(boxes[0]);
    physics::explode(world, physics::ExplosionDef { position: float3 { 0.0, 0.0, 0.0 },
                                                    radius: 10.0, falloff: 2.0,
                                                    impulse_per_area: 20.0 });
    physics::step(world, 1.0 / 60.0, 4);
    print("awake after the explosion: ", physics::awake_body_count(world) > 0, "; the bottom box ",
          "moved: ", physics::position(boxes[0]) != before);

    // The same pile with one worker and with four comes out the same.
    let serial = physics::create_world(physics::WorldDef { workers: 1 });
    let parallel = physics::create_world(physics::WorldDef { workers: 4 });
    print("workers ", physics::worker_count(serial), " and ", physics::worker_count(parallel));
    var a: physics::Body[>..] = [];
    var b: physics::Body[>..] = [];
    pile(serial, a);
    pile(parallel, b);
    for i in 120 {
        physics::step(serial, 1.0 / 60.0, 4);
        physics::step(parallel, 1.0 / 60.0, 4);
    }
    var ta: physics::Transform[>..] = [];
    var tb: physics::Transform[>..] = [];
    ta.resize(a.len, physics::transform_identity);
    tb.resize(b.len, physics::transform_identity);
    physics::transforms(a, ta);
    physics::transforms(b, tb);
    var same = true;
    for i in a.len { same = same && ta[i] == tb[i] && ta[i] == physics::transform(a[i]); }
    print("the same with one worker and with four: ", same);
    print("worlds while three exist: ", physics::world_count() - worlds, "; at most ",
          physics::max_world_count() >= 3);

    let bytes = physics::byte_count();
    physics::destroy(serial);
    physics::destroy(parallel);
    physics::destroy(world);
    print("destroyed: valid ", physics::is_valid(world), ", body valid ",
          physics::is_valid(boxes[0]), ", worlds ", physics::world_count() - worlds,
          " more, Box3D's allocations down ", physics::byte_count() < bytes);
}
