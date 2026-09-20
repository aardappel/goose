// Bodies in the physics module: a definition's every field read back, names
// and user data, frames and points, velocities, forces, torques and
// impulses on a body that gravity does not pull, mass properties overridden
// and restored, sleeping, disabling, motion locks, a kinematic body moved to
// a target, changing a body's type, what is attached to a body, its bounds
// and extents, and queries against one body alone.
import std;
import vec;
import math;
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

fn vec3(v: float3) -> u8[>..] { str(num(v.x), " ", num(v.y), " ", num(v.z)) }
fn quat(q: physics::Quat) -> u8[>..] { str(num(q.x), " ", num(q.y), " ", num(q.z), " ", num(q.w)) }

let DT: f32 = 1.0 / 60.0;

fn main() {
    let world = physics::create_world(physics::WorldDef { workers: 1 });
    let ground = physics::create_body(world, physics::BodyDef {
        position: float3 { 0.0, -1.0, 0.0 } });
    physics::create_box_shape(ground, physics::ShapeDef {}, float3 { 20.0, 1.0, 20.0 });

    // Every field of a definition, back from the body.
    let spin = physics::quat_axis_angle(float3 { 0.0, 0.0, 1.0 }, 0.5);
    let b = physics::create_body(world, physics::BodyDef {
        body_type: physics::DYNAMIC, position: float3 { 1.0, 5.0, -2.0 }, rotation: spin,
        linear_velocity: float3 { 0.5, 0.0, 0.0 }, angular_velocity: float3 { 0.0, 0.25, 0.0 },
        linear_damping: 0.1, angular_damping: 0.2, gravity_scale: 0.0, sleep_threshold: 0.04,
        safety_factor: 0.25, motion_locks: physics::MotionLocks { angular_x: true },
        enable_sleep: false, is_awake: true, is_bullet: true, is_enabled: true,
        allow_fast_rotation: true, enable_contact_recycling: false, user_data: 99 });
    print("type dynamic ", physics::body_type(b) == physics::DYNAMIC, "; user data ",
          physics::user_data(b), "; world ", physics::world(b) == world);
    print("position ", vec3(physics::position(b)), "; rotation ", quat(physics::rotation(b)));
    print("velocity ", vec3(physics::linear_velocity(b)), "; spin ",
          vec3(physics::angular_velocity(b)));
    print("damping ", num(physics::linear_damping(b)), " ", num(physics::angular_damping(b)),
          "; gravity scale ", num(physics::gravity_scale(b)), "; sleep threshold ",
          num(physics::sleep_threshold(b)), "; safety factor ", num(physics::safety_factor(b)));
    let locks = physics::motion_locks(b);
    print("locks ", locks.linear_x, " ", locks.angular_x, "; sleep ", physics::sleep_enabled(b),
          "; awake ", physics::is_awake(b), "; bullet ", physics::is_bullet(b), "; enabled ",
          physics::is_enabled(b), "; fast rotation ", physics::fast_rotation_allowed(b),
          "; recycling ", physics::contact_recycling_enabled(b));
    physics::set_name(b, "crate");
    physics::set_user_data(b, 100);
    print("name ", physics::name(b), ", ground name '", physics::name(ground), "'; user data ",
          physics::user_data(b));

    // Mass from two shapes of density 1: a unit cube and a sphere beside it.
    let cube = physics::create_box_shape(b, physics::ShapeDef { density: 1.0 },
                                         float3 { 0.5, 0.5, 0.5 });
    let ball = physics::create_sphere_shape(b, physics::ShapeDef { density: 1.0 },
                                            physics::Sphere { center: float3 { 1.0, 0.0, 0.0 },
                                                              radius: 0.5 });
    let shapes = physics::shapes(b);
    print("shapes ", physics::shape_count(b), ": ", shapes.len == 2 && contains(shapes, cube) &&
          contains(shapes, ball));
    print("mass ", num(physics::mass(b)), ", inverse ", num(physics::inverse_mass(b)),
          ", center ", vec3(physics::local_center(b)));
    let inertia = physics::rotational_inertia(b);
    print("inertia diagonal ", num(inertia.cx.x), " ", num(inertia.cy.y), " ", num(inertia.cz.z));
    let md = physics::mass_data(b);
    print("mass data agrees: ",
          md.mass == physics::mass(b) && md.center == physics::local_center(b));
    physics::set_mass_data(b, physics::MassData {
        mass: 5.0, center: float3_0,
        inertia: physics::Mat3 { cx: float3 { 1.0, 0.0, 0.0 }, cy: float3 { 0.0, 1.0, 0.0 },
                                 cz: float3 { 0.0, 0.0, 1.0 } } });
    print("overridden: mass ", num(physics::mass(b)), ", center ", vec3(physics::world_center(b)));
    physics::apply_mass_from_shapes(b);
    print("from the shapes again: mass ", num(physics::mass(b)));
    let wi = physics::world_inverse_inertia(b);
    print("world inverse inertia positive: ", wi.cx.x > 0.0 && wi.cy.y > 0.0 && wi.cz.z > 0.0);

    // Frames: a teleport, then points and vectors both ways.
    physics::set_transform(b, float3 { 0.0, 10.0, 0.0 },
                           physics::quat_axis_angle(float3 { 0.0, 1.0, 0.0 }, 1.5707963));
    let t = physics::transform(b);
    print("teleported to ", vec3(t.p), " turned ", quat(t.q));
    let wp = physics::world_point(b, float3 { 1.0, 0.0, 0.0 });
    print("local x in the world ", vec3(wp), "; back ", vec3(physics::local_point(b, wp)));
    print("vector ", vec3(physics::world_vector(b, float3 { 0.0, 0.0, 1.0 })), "; back ",
          vec3(physics::local_vector(b, physics::world_vector(b, float3 { 0.0, 0.0, 1.0 }))));
    print("helpers agree: ", vec3(physics::transform_point(t, float3 { 1.0, 0.0, 0.0 })));

    // Velocities, and those of points on the body.
    physics::set_linear_velocity(b, float3 { 1.0, 0.0, 0.0 });
    physics::set_angular_velocity(b, float3 { 0.0, 2.0, 0.0 });
    print("velocity ", vec3(physics::linear_velocity(b)), "; spin ",
          vec3(physics::angular_velocity(b)));
    let c = physics::world_center(b);
    // The origin is off the center of mass: v + w x (origin - center).
    let r = physics::position(b) - c;
    let expected = float3 { 1.0, 0.0, 0.0 } + cross(float3 { 0.0, 2.0, 0.0 }, r);
    print("velocity of a point 1 in z of the center ",
          vec3(physics::world_point_velocity(b, c + float3 { 0.0, 0.0, 1.0 })),
          "; of the body's origin as expected: ",
          length(physics::local_point_velocity(b, float3_0) - expected) < 0.0001);

    // Impulses change the velocity at once; forces over the next step.
    physics::set_linear_damping(b, 0.0);
    physics::set_angular_damping(b, 0.0);
    physics::set_linear_velocity(b, float3_0);
    physics::set_angular_velocity(b, float3_0);
    let m = physics::mass(b);
    physics::apply_linear_impulse_to_center(b, float3 { m, 0.0, 0.0 }, true);
    print("impulse of the mass: velocity ", vec3(physics::linear_velocity(b)));
    physics::set_linear_velocity(b, float3_0);
    physics::apply_force_to_center(b, float3 { 0.0, 60.0 * m, 0.0 }, true);
    physics::step(world, DT, 4);
    print("force of 60 g for 1/60 s: velocity ", vec3(physics::linear_velocity(b)));
    physics::set_linear_velocity(b, float3_0);
    physics::apply_angular_impulse(b, float3 { 0.0, 1.0, 0.0 }, true);
    print("an angular impulse spins it about y: ", physics::angular_velocity(b).y > 0.0);
    physics::set_angular_velocity(b, float3_0);
    physics::apply_torque(b, float3 { 0.0, 0.0, 10.0 }, true);
    physics::step(world, DT, 4);
    print("a torque about z spins it about z: ", physics::angular_velocity(b).z > 0.0);
    physics::set_angular_velocity(b, float3_0);
    let center = physics::world_center(b);
    physics::apply_linear_impulse(b, float3 { 0.0, 0.0, 1.0 }, center + float3 { 1.0, 0.0, 0.0 },
                                  true);
    print("an impulse off the center also spins: ", physics::angular_velocity(b).y != 0.0,
          ", moves ", physics::linear_velocity(b).z > 0.0);
    physics::set_linear_velocity(b, float3_0);
    physics::set_angular_velocity(b, float3_0);
    // Above the center and along x, about z: rotation about x is locked.
    physics::apply_force(b, float3 { 100.0, 0.0, 0.0 }, center + float3 { 0.0, 1.0, 0.0 }, true);
    physics::step(world, DT, 4);
    print("a force off the center also spins: ", physics::angular_velocity(b).z < 0.0);

    // Settings changed and read back.
    physics::set_gravity_scale(b, 2.0);
    physics::set_sleep_threshold(b, 0.1);
    physics::set_safety_factor(b, 0.1);
    physics::set_linear_damping(b, 0.5);
    physics::set_angular_damping(b, 0.6);
    physics::set_bullet(b, false);
    physics::allow_fast_rotation(b, false);
    physics::enable_contact_recycling(b, true);
    physics::enable_sleep(b, true);
    physics::enable_hit_events(b, true);
    print("gravity scale ", num(physics::gravity_scale(b)), "; threshold ",
          num(physics::sleep_threshold(b)), "; safety ", num(physics::safety_factor(b)),
          "; damping ", num(physics::linear_damping(b)), " ", num(physics::angular_damping(b)),
          "; bullet ", physics::is_bullet(b), "; fast rotation ", physics::fast_rotation_allowed(b),
          "; recycling ", physics::contact_recycling_enabled(b), "; sleep ",
          physics::sleep_enabled(b), "; hit events ", physics::hit_events_enabled(cube));
    physics::set_awake(b, false);
    print("put to sleep: awake ", physics::is_awake(b));
    physics::set_awake(b, true);
    physics::disable(b);
    print("disabled: enabled ", physics::is_enabled(b));
    physics::enable(b);
    print("enabled again: ", physics::is_enabled(b));

    // Locked motion: an impulse along x moves a body locked in x not at all.
    let slider = physics::create_body(world, physics::BodyDef {
        body_type: physics::DYNAMIC, position: float3 { 5.0, 5.0, 5.0 }, gravity_scale: 0.0 });
    physics::create_box_shape(slider, physics::ShapeDef {}, float3 { 0.5, 0.5, 0.5 });
    physics::set_motion_locks(slider, physics::MotionLocks { linear_x: true, angular_x: true,
                                                             angular_y: true, angular_z: true });
    physics::apply_linear_impulse_to_center(slider, float3 { 1000.0, 0.0, 1000.0 }, true);
    for i in 10 { physics::step(world, DT, 4); }
    let sp = physics::position(slider);
    print("locked in x: x ", num(sp.x), ", moved in z ", sp.z > 5.0, "; locks read back ",
          physics::motion_locks(slider).linear_x);

    // A kinematic body sent to a target over one step.
    let lift = physics::create_body(world, physics::BodyDef {
        body_type: physics::KINEMATIC, position: float3 { -5.0, 1.0, 0.0 } });
    physics::create_box_shape(lift, physics::ShapeDef {}, float3 { 1.0, 0.1, 1.0 });
    physics::set_target_transform(lift, physics::Transform { p: float3 { -5.0, 2.0, 0.0 },
                                                             q: physics::quat_identity }, DT, true);
    print("kinematic velocity to the target ", vec3(physics::linear_velocity(lift)));
    physics::step(world, DT, 4);
    print("there after a step: ", vec3(physics::position(lift)));

    // A body's type can change.
    physics::set_type(lift, physics::STATIC);
    print("now static: ", physics::body_type(lift) == physics::STATIC, ", mass ",
          num(physics::mass(lift)));
    physics::set_type(lift, physics::DYNAMIC);
    print("now dynamic: mass ", num(physics::mass(lift)));

    // Resting on the ground: its contacts, bounds and extents.
    let crate = physics::create_body(world, physics::BodyDef {
        body_type: physics::DYNAMIC, position: float3 { 0.0, 0.5, 8.0 } });
    physics::create_box_shape(crate, physics::ShapeDef {}, float3 { 0.5, 0.5, 0.5 });
    for i in 30 { physics::step(world, DT, 4); }
    let touching = physics::contacts(crate);
    var points = 0;
    for mf in touching { points += mf.point_count; }
    print("resting: manifolds ", touching.len, ", points ", points, ", normal ",
          vec3(touching[0].normal), ", pushing ", touching[0].points[0].normal_impulse > 0.0);
    print("contact still valid: ", physics::is_valid(touching[0].contact), "; between the crate ",
          "and the ground: ", (physics::body(touching[0].shape_a) == crate) !=
          (physics::body(touching[0].shape_b) == crate));
    let box = physics::aabb(crate);
    print("bounds ", vec3(box.lower), " to ", vec3(box.upper));
    print("min extent ", num(physics::min_extent(crate)), "; max extent ",
          vec3(physics::max_extent(crate)), "; from the origin ",
          vec3(physics::max_extent_origin(crate)));
    let near, dist = physics::closest_point(crate, float3 { 3.0, 0.5, 8.0 });
    print("closest to (3, 0.5, 8): ", vec3(near), " at ", num(dist));

    // Joints attached to a body.
    let weld = physics::create_weld_joint(world, physics::WeldJointDef {
        base: physics::joint_def(crate, slider, float3 { 0.0, 3.0, 8.0 }) });
    print("joints ", physics::joint_count(crate), ": ", physics::joints(crate)[0] == weld.joint,
          "; the ground has ", physics::joint_count(ground));
    physics::destroy(weld.joint);
    print("joint gone: ", physics::joint_count(crate));

    // Queries against one body, placed where the query says.
    let at = physics::Transform { p: float3 { 0.0, 0.0, 0.0 }, q: physics::quat_identity };
    let hit = physics::cast_ray(crate, float3 { -5.0, 0.0, 0.0 }, float3 { 10.0, 0.0, 0.0 },
                                physics::QueryFilter {}, 1.0, at);
    print("ray at the crate: hit ", hit.hit, " at ", vec3(hit.point), " normal ", vec3(hit.normal),
          " fraction ", num(hit.fraction));
    let probe: float3[1] = [float3_0];
    let swept = physics::cast_shape(crate, float3 { 0.0, 5.0, 0.0 }, probe, 0.25,
                                    float3 { 0.0, -10.0, 0.0 }, physics::QueryFilter {}, 1.0, false,
                                    at);
    print("sphere cast down at it: hit ", swept.hit, " at ", vec3(swept.point), " fraction ",
          num(swept.fraction));
    print("overlaps a sphere at its corner: ",
          physics::overlap_shape(crate, float3 { 0.6, 0.6, 0.0 }, probe, 0.25,
                                 physics::QueryFilter {}, at),
          "; one far away: ", physics::overlap_shape(crate, float3 { 3.0, 0.0, 0.0 }, probe, 0.25,
                                                    physics::QueryFilter {}, at));
    let mover = physics::Capsule { center1: float3 { 0.0, -0.5, 0.0 },
                                   center2: float3 { 0.0, 0.5, 0.0 }, radius: 0.3 };
    let planes = physics::collide_mover(crate, float3 { 0.75, 0.0, 0.0 }, mover,
                                        physics::QueryFilter {}, at);
    print("a mover against its side: planes ", planes.len, ", normal ",
          vec3(planes[0].plane.normal));
    let toi = physics::time_of_impact_mover(crate, float3 { -3.0, 0.0, 0.0 }, mover,
                                            float3 { 4.0, 0.0, 0.0 }, physics::QueryFilter {}, at,
                                            at);
    print("a mover sweeping into it stops at fraction ", num(toi.fraction), ", normal ",
          vec3(toi.normal));

    physics::destroy(crate);
    print("destroyed: body ", physics::is_valid(crate), ", its contact ",
          physics::is_valid(touching[0].contact));
    physics::destroy(world);
}
