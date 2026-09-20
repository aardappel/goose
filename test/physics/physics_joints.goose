// Joints in the physics module: every kind Box3D has, each made from its
// definition, its own settings changed and read back, and each doing what it
// is for -- a motor turning a hinge, a slider stopping at its limit, a rope
// keeping its length, a weld holding, a wheel spinning, a ball joint kept in
// its cone, a motor joint driving a body, a parallel joint lining up axes, a
// filter joint letting two bodies pass -- plus what every joint has, and a
// joint event when one is pulled harder than its threshold.
import std;
import vec;
import math;
import physics;

// A number rounded to hundredths, without the sign of a zero: joint motion
// only settles to about that.
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

fn run(world: physics::World, steps: i64) {
    for i in steps { physics::step(world, DT, 4); }
}

fn box(world: physics::World, at: float3, half: float3) -> physics::Body {
    let b = physics::create_body(world, physics::BodyDef {
        body_type: physics::DYNAMIC, position: at, enable_sleep: false });
    physics::create_box_shape(b, physics::ShapeDef {}, half);
    return b;
}

fn ball(world: physics::World, at: float3, radius: f32) -> physics::Body {
    let b = physics::create_body(world, physics::BodyDef {
        body_type: physics::DYNAMIC, position: at, enable_sleep: false });
    physics::create_sphere_shape(b, physics::ShapeDef {}, physics::Sphere { center: float3_0,
                                                                            radius: radius });
    return b;
}

fn main() {
    let world = physics::create_world(physics::WorldDef { workers: 1 });
    let ground = physics::create_body(world, physics::BodyDef {});
    physics::create_box_shape(ground, physics::ShapeDef {}, float3 { 50.0, 0.5, 50.0 },
                              physics::Transform { p: float3 { 0.0, -0.5, 0.0 },
                                                   q: physics::quat_identity });

    // --- revolute: a bar on a hinge about z, turned by its motor -------------
    let bar = box(world, float3 { 1.0, 5.0, 0.0 }, float3 { 1.0, 0.1, 0.1 });
    let hinge = physics::create_revolute_joint(world, physics::RevoluteJointDef {
        base: physics::joint_def(ground, bar, float3 { 0.0, 5.0, 0.0 }), enable_motor: true,
        max_motor_torque: 10000.0, motor_speed: 1.0 });
    print("revolute: kind ", physics::joint_type(hinge.joint) == physics::JOINT_REVOLUTE,
          ", motor ", physics::motor_enabled(hinge), " at ", num(physics::motor_speed(hinge)),
          " with up to ", num(physics::max_motor_torque(hinge)));
    run(world, 60);
    print("after a second the angle is ", num(physics::angle(hinge)), ", the motor pushing ",
          physics::motor_torque(hinge) != 0.0);
    physics::set_motor_speed(hinge, 0.0);
    physics::set_max_motor_torque(hinge, 5000.0);
    physics::enable_limit(hinge, true);
    physics::set_limits(hinge, -0.25, 0.5);
    physics::enable_spring(hinge, true);
    physics::set_spring_hertz(hinge, 2.0);
    physics::set_spring_damping_ratio(hinge, 0.5);
    physics::set_target_angle(hinge, 0.25);
    print("limits ", physics::limit_enabled(hinge), " ", num(physics::lower_limit(hinge)), " to ",
          num(physics::upper_limit(hinge)), "; spring ", physics::spring_enabled(hinge), " ",
          num(physics::spring_hertz(hinge)), " hertz, ratio ",
          num(physics::spring_damping_ratio(hinge)), ", target ",
          num(physics::target_angle(hinge)), "; motor torque up to ",
          num(physics::max_motor_torque(hinge)));
    physics::enable_motor(hinge, false);
    run(world, 120);
    let a = physics::angle(hinge);
    print("with the motor off it sits within its limits: ", a >= -0.26 && a <= 0.51);

    // --- prismatic: a slider along x driven to its upper limit ---------------
    let sled = box(world, float3 { 10.0, 1.0, 0.0 }, float3 { 0.25, 0.25, 0.25 });
    let rail = physics::create_prismatic_joint(world, physics::PrismaticJointDef {
        base: physics::joint_def(ground, sled, float3 { 10.0, 1.0, 0.0 }), enable_limit: true,
        lower_translation: -1.0, upper_translation: 1.0, enable_motor: true,
        max_motor_force: 1000.0, motor_speed: 2.0 });
    run(world, 60);
    print("prismatic: at ", num(physics::translation(rail)), " of limits ",
          num(physics::lower_limit(rail)), " to ", num(physics::upper_limit(rail)), ", speed ",
          num(physics::speed(rail)), ", motor force ", physics::motor_force(rail) > 0.0);
    physics::set_motor_speed(rail, -3.0);
    physics::set_max_motor_force(rail, 500.0);
    physics::set_limits(rail, -0.5, 0.5);
    print("motor ", physics::motor_enabled(rail), " at ", num(physics::motor_speed(rail)),
          " up to ", num(physics::max_motor_force(rail)), "; limits ", physics::limit_enabled(rail),
          " ", num(physics::lower_limit(rail)), " to ", num(physics::upper_limit(rail)));
    run(world, 60);
    print("driven back to ", num(physics::translation(rail)));
    physics::enable_motor(rail, false);
    physics::enable_limit(rail, false);
    physics::enable_spring(rail, true);
    physics::set_spring_hertz(rail, 4.0);
    physics::set_spring_damping_ratio(rail, 1.0);
    physics::set_target_translation(rail, 0.25);
    print("spring ", physics::spring_enabled(rail), " ", num(physics::spring_hertz(rail)), " ",
          num(physics::spring_damping_ratio(rail)), " to ", num(physics::target_translation(rail)));
    run(world, 180);
    print("the spring pulls it to its target: ", num(physics::translation(rail)));

    // --- distance: a ball on a rope, and a spring ----------------------------
    let bob = ball(world, float3 { 20.0, 3.0, 0.0 }, 0.25);
    let rope = physics::create_distance_joint(world, physics::DistanceJointDef {
        base: physics::JointDef { body_a: ground, body_b: bob,
                                  frame_a: physics::Transform { p: float3 { 22.0, 6.0, 0.0 },
                                                                q: physics::quat_identity } },
        length: 3.0 });
    run(world, 90);
    print("distance: rest length ", num(physics::rest_length(rope)), ", now ",
          num(physics::current_length(rope)));
    physics::set_rest_length(rope, 2.0);
    physics::enable_spring(rope, true);
    physics::set_spring_hertz(rope, 3.0);
    physics::set_spring_damping_ratio(rope, 0.7);
    physics::set_spring_force_range(rope, -500.0, 500.0);
    physics::enable_limit(rope, true);
    physics::set_length_range(rope, 1.5, 2.5);
    physics::enable_motor(rope, true);
    physics::set_motor_speed(rope, 0.5);
    physics::set_max_motor_force(rope, 10.0);
    let lower, upper = physics::spring_force_range(rope);
    print("spring ", physics::spring_enabled(rope), " ", num(physics::spring_hertz(rope)), " ",
          num(physics::spring_damping_ratio(rope)), " within ", num(lower), " to ", num(upper),
          "; limit ", physics::limit_enabled(rope), " ", num(physics::min_length(rope)), " to ",
          num(physics::max_length(rope)), "; motor ", physics::motor_enabled(rope), " ",
          num(physics::motor_speed(rope)), " up to ", num(physics::max_motor_force(rope)));
    run(world, 120);
    let len = physics::current_length(rope);
    print("a spring within its limits: ", len >= 1.49 && len <= 2.51, ", the motor pushing ",
          physics::motor_force(rope) != 0.0);

    // --- weld: two boxes glued, falling as one -------------------------------
    let left = box(world, float3 { -10.0, 4.0, 0.0 }, float3 { 0.5, 0.5, 0.5 });
    let right = box(world, float3 { -9.0, 4.0, 0.0 }, float3 { 0.5, 0.5, 0.5 });
    let glue = physics::create_weld_joint(world, physics::WeldJointDef {
        base: physics::joint_def(left, right, float3 { -9.5, 4.0, 0.0 }) });
    physics::set_linear_hertz(glue, 0.0);
    physics::set_angular_hertz(glue, 0.0);
    physics::set_linear_damping_ratio(glue, 1.0);
    physics::set_angular_damping_ratio(glue, 1.0);
    print("weld: hertz ", num(physics::linear_hertz(glue)), " ", num(physics::angular_hertz(glue)),
          ", ratios ", num(physics::linear_damping_ratio(glue)), " ",
          num(physics::angular_damping_ratio(glue)));
    run(world, 120);
    print("landed together: apart by ",
          num(length(physics::position(right) - physics::position(left))), ", both at height ",
          num(physics::position(left).y), " ", num(physics::position(right).y));

    // --- wheel: a wheel on an axle, spun by its motor ------------------------
    let axle = physics::create_body(world, physics::BodyDef {
        position: float3 { 0.0, 3.0, 10.0 } });
    let wheel = physics::create_body(world, physics::BodyDef {
        body_type: physics::DYNAMIC, position: float3 { 0.0, 3.0, 10.0 }, enable_sleep: false,
        allow_fast_rotation: true });
    physics::create_sphere_shape(wheel, physics::ShapeDef {}, physics::Sphere { center: float3_0,
                                                                                radius: 0.4 });
    // Frame b's x is the spin axis; frame a's z the suspension.
    let spin_frame = physics::Transform {
        p: float3 { 0.0, 3.0, 10.0 },
        q: physics::quat_axis_angle(float3 { 1.0, 0.0, 0.0 }, -1.5707964) };
    let wj = physics::create_wheel_joint(world, physics::WheelJointDef {
        base: physics::joint_def(axle, wheel, spin_frame), enable_suspension_limit: true,
        lower_suspension_limit: -0.1, upper_suspension_limit: 0.1, enable_spin_motor: true,
        max_spin_torque: 1000.0, spin_speed: 5.0 });
    run(world, 60);
    print("wheel: spinning at ", num(physics::spin_speed(wj)), " for a motor at ",
          num(physics::spin_motor_speed(wj)), ", torque ", physics::spin_torque(wj) != 0.0);
    physics::set_spin_motor_speed(wj, 2.0);
    physics::set_max_spin_torque(wj, 800.0);
    physics::enable_suspension(wj, true);
    physics::set_suspension_hertz(wj, 5.0);
    physics::set_suspension_damping_ratio(wj, 0.9);
    physics::enable_suspension_limit(wj, true);
    physics::set_suspension_limits(wj, -0.2, 0.2);
    physics::enable_steering(wj, true);
    physics::set_steering_hertz(wj, 6.0);
    physics::set_steering_damping_ratio(wj, 0.8);
    physics::set_max_steering_torque(wj, 300.0);
    physics::enable_steering_limit(wj, true);
    physics::set_steering_limits(wj, -0.5, 0.5);
    physics::set_target_steering_angle(wj, 0.3);
    print("motor ", physics::spin_motor_enabled(wj), " ", num(physics::spin_motor_speed(wj)),
          " up to ", num(physics::max_spin_torque(wj)), "; suspension ",
          physics::suspension_enabled(wj), " ", num(physics::suspension_hertz(wj)), " ",
          num(physics::suspension_damping_ratio(wj)), ", limit ",
          physics::suspension_limit_enabled(wj), " ", num(physics::lower_suspension_limit(wj)),
          " to ", num(physics::upper_suspension_limit(wj)));
    print("steering ", physics::steering_enabled(wj), " ", num(physics::steering_hertz(wj)), " ",
          num(physics::steering_damping_ratio(wj)), " up to ",
          num(physics::max_steering_torque(wj)), ", limit ", physics::steering_limit_enabled(wj),
          " ", num(physics::lower_steering_limit(wj)), " to ",
          num(physics::upper_steering_limit(wj)), ", target ",
          num(physics::target_steering_angle(wj)));
    run(world, 60);
    print("spinning at ", num(physics::spin_speed(wj)), ", steered to ",
          num(physics::steering_angle(wj)), " by a torque ", physics::steering_torque(wj) != 0.0);
    physics::enable_spin_motor(wj, false);
    physics::enable_steering(wj, false);
    print("motor ", physics::spin_motor_enabled(wj), ", steering ", physics::steering_enabled(wj));

    // --- spherical: a ball on a stick, kept in a cone ------------------------
    let stick = box(world, float3 { 0.0, 7.0, -10.0 }, float3 { 0.1, 1.0, 0.1 });
    let socket = physics::create_spherical_joint(world, physics::SphericalJointDef {
        base: physics::joint_def(ground, stick, physics::Transform {
            p: float3 { 0.0, 8.0, -10.0 },
            q: physics::quat_axis_angle(float3 { 1.0, 0.0, 0.0 }, 1.5707964) }),
        enable_cone_limit: true, cone_angle: 0.5 });
    physics::apply_linear_impulse_to_center(stick, float3 { 50.0, 0.0, 0.0 }, true);
    var widest: f32 = 0.0;
    for i in 90 {
        physics::step(world, DT, 4);
        widest = max(widest, physics::cone_angle(socket));
    }
    print("spherical: cone limit ", physics::cone_limit_enabled(socket), " ",
          num(physics::cone_limit(socket)), ", widest swing within it: ", widest <= 0.52);
    physics::enable_cone_limit(socket, false);
    physics::enable_cone_limit(socket, true);
    physics::set_cone_limit(socket, 0.75);
    physics::enable_twist_limit(socket, true);
    physics::set_twist_limits(socket, -0.3, 0.4);
    physics::enable_spring(socket, true);
    physics::set_spring_hertz(socket, 1.5);
    physics::set_spring_damping_ratio(socket, 0.25);
    physics::set_target_rotation(socket, physics::quat_axis_angle(float3 { 0.0, 1.0, 0.0 }, 0.2));
    physics::enable_motor(socket, true);
    physics::set_motor_velocity(socket, float3 { 0.0, 0.5, 0.0 });
    physics::set_max_motor_torque(socket, 50.0);
    let target = physics::target_rotation(socket);
    print("cone ", num(physics::cone_limit(socket)), "; twist ",
          physics::twist_limit_enabled(socket), " ", num(physics::lower_twist_limit(socket)),
          " to ", num(physics::upper_twist_limit(socket)), "; spring ",
          physics::spring_enabled(socket), " ", num(physics::spring_hertz(socket)), " ",
          num(physics::spring_damping_ratio(socket)), " to ", num(target.y), " ", num(target.w),
          "; motor ", physics::motor_enabled(socket), " ", vec3(physics::motor_velocity(socket)),
          " up to ", num(physics::max_motor_torque(socket)));
    run(world, 30);
    let tw = physics::twist_angle(socket);
    print("twist within its limits: ", tw >= -0.31 && tw <= 0.41, ", the motor pushing ",
          length(physics::motor_torque(socket)) > 0.0);

    // --- motor joint: driving a body over the ground -------------------------
    let puck = box(world, float3 { 30.0, 0.25, 0.0 }, float3 { 0.25, 0.25, 0.25 });
    let driver = physics::create_motor_joint(world, physics::MotorJointDef {
        base: physics::joint_def(ground, puck, float3 { 30.0, 0.25, 0.0 }),
        linear_velocity: float3 { 1.0, 0.0, 0.0 }, max_velocity_force: 1000.0,
        angular_velocity: float3_0, max_velocity_torque: 100.0 });
    run(world, 60);
    print("motor joint: the puck moved ", num(physics::position(puck).x - 30.0), " in a second at ",
          vec3(physics::linear_velocity(driver)));
    physics::set_linear_velocity(driver, float3 { 0.0, 0.0, 1.0 });
    physics::set_angular_velocity(driver, float3 { 0.0, 1.0, 0.0 });
    physics::set_max_velocity_force(driver, 800.0);
    physics::set_max_velocity_torque(driver, 80.0);
    physics::set_linear_hertz(driver, 1.0);
    physics::set_linear_damping_ratio(driver, 0.5);
    physics::set_angular_hertz(driver, 2.0);
    physics::set_angular_damping_ratio(driver, 0.6);
    physics::set_max_spring_force(driver, 90.0);
    physics::set_max_spring_torque(driver, 9.0);
    print("velocity ", vec3(physics::linear_velocity(driver)), ", spin ",
          vec3(physics::angular_velocity(driver)), ", limits ",
          num(physics::max_velocity_force(driver)), " ", num(physics::max_velocity_torque(driver)),
          "; springs ", num(physics::linear_hertz(driver)), " ",
          num(physics::linear_damping_ratio(driver)), " ", num(physics::angular_hertz(driver)),
          " ", num(physics::angular_damping_ratio(driver)), " up to ",
          num(physics::max_spring_force(driver)), " ", num(physics::max_spring_torque(driver)));

    // --- parallel: a tilted body pulled upright ------------------------------
    let post = physics::create_body(world, physics::BodyDef {
        body_type: physics::DYNAMIC, position: float3 { -30.0, 3.0, 0.0 },
        rotation: physics::quat_axis_angle(float3 { 1.0, 0.0, 0.0 }, 0.5), gravity_scale: 0.0,
        enable_sleep: false });
    physics::create_box_shape(post, physics::ShapeDef {}, float3 { 0.2, 0.2, 1.0 });
    let level = physics::create_parallel_joint(world, physics::ParallelJointDef {
        base: physics::JointDef { body_a: ground, body_b: post,
                                  frame_a: physics::Transform { p: float3 { -30.0, 3.0, 0.0 },
                                                                q: physics::quat_identity } },
        hertz: 2.0, damping_ratio: 1.0 });
    run(world, 180);
    print("parallel: its z axis now ", vec3(physics::rotate(physics::rotation(post),
                                                            float3 { 0.0, 0.0, 1.0 })));
    physics::set_spring_hertz(level, 3.0);
    physics::set_spring_damping_ratio(level, 0.8);
    physics::set_max_torque(level, 70.0);
    print("spring ", num(physics::spring_hertz(level)), " ",
          num(physics::spring_damping_ratio(level)), " up to ", num(physics::max_torque(level)));

    // --- filter: two boxes in the same place that do not push apart ----------
    let ghost_a = box(world, float3 { 0.0, 0.5, 30.0 }, float3 { 0.5, 0.5, 0.5 });
    let ghost_b = box(world, float3 { 0.0, 0.5, 30.0 }, float3 { 0.5, 0.5, 0.5 });
    let pass = physics::create_filter_joint(world, physics::FilterJointDef {
        base: physics::JointDef { body_a: ghost_a, body_b: ghost_b } });
    run(world, 30);
    print("filter: still overlapping, ",
          num(length(physics::position(ghost_a) - physics::position(ghost_b))), " apart");

    // --- what every joint has ------------------------------------------------
    let j = hinge.joint;
    print("every joint: bodies ", physics::body_a(j) == ground, " ", physics::body_b(j) == bar,
          ", world ", physics::world(j) == world, ", awake ", physics::is_awake(j));
    physics::set_collide_connected(j, true);
    physics::set_user_data(j, 1234);
    physics::set_constraint_tuning(j, 50.0, 3.0);
    physics::set_force_threshold(j, 1000000.0);
    physics::set_torque_threshold(j, 2000000.0);
    let hz, ratio = physics::constraint_tuning(j);
    print("collide connected ", physics::collide_connected(j), ", user data ",
          physics::user_data(j), ", tuning ", num(hz), " ", num(ratio), ", thresholds ",
          num(physics::force_threshold(j)), " ", num(physics::torque_threshold(j)));
    let fa = physics::local_frame_a(j);
    print("frame a at ", vec3(fa.p), ", frame b at ", vec3(physics::local_frame_b(j).p));
    physics::set_local_frame_b(j, physics::Transform { p: float3 { -1.5, 0.0, 0.0 },
                                                       q: physics::quat_identity });
    physics::set_local_frame_a(j, fa);
    print("frame b moved to ", vec3(physics::local_frame_b(j).p));
    physics::wake_bodies(j);
    run(world, 30);
    print("holding the bar: force ", length(physics::constraint_force(j)) > 0.0, ", apart by ",
          num(physics::linear_separation(j)), ", twisted by ", num(physics::angular_separation(j)),
          ", torque ", length(physics::constraint_torque(j)) > 0.0);
    let kinds: i32[8] = [physics::JOINT_PRISMATIC, physics::JOINT_DISTANCE, physics::JOINT_WELD,
                         physics::JOINT_WHEEL, physics::JOINT_SPHERICAL, physics::JOINT_MOTOR,
                         physics::JOINT_PARALLEL, physics::JOINT_FILTER];
    let joints: physics::Joint[8] = [rail.joint, rope.joint, glue.joint, wj.joint, socket.joint,
                                     driver.joint, level.joint, pass.joint];
    var kinds_right = true;
    for i in 8 { kinds_right = kinds_right && physics::joint_type(joints[i]) == kinds[i]; }
    print("the other kinds report theirs: ", kinds_right);

    // A joint pulled harder than its force threshold reports it after the step.
    let weight = ball(world, float3 { 40.0, 3.0, 0.0 }, 0.5);
    let hook = physics::create_distance_joint(world, physics::DistanceJointDef {
        base: physics::JointDef { body_a: ground, body_b: weight,
                                  frame_a: physics::Transform { p: float3 { 40.0, 5.0, 0.0 },
                                                                q: physics::quat_identity },
                                  force_threshold: 10.0, user_data: 55 },
        length: 2.0 });
    run(world, 5);
    let events = physics::joint_events(world);
    var ours = false;
    for e in events { ours = ours || (e.joint == hook.joint && e.user_data == 55); }
    print("joint events: ", ours);

    physics::destroy(hook.joint);
    physics::destroy(glue.joint, false);
    print("destroyed: ", physics::is_valid(hook.joint), " ", physics::is_valid(glue.joint),
          "; others ", physics::is_valid(hinge.joint));
    physics::destroy(bar);
    print("with its body gone the hinge is gone too: ", physics::is_valid(hinge.joint));
    physics::destroy(world);
}
