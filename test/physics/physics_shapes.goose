// Shapes and geometry in the physics module: every kind of shape Box3D has
// on bodies -- spheres, capsules, boxes, convex hulls from points and made
// ready (cylinders, cones, rocks), transformed hulls, triangle meshes, height
// fields and baked compounds -- what a shape holds and how it changes,
// collision filters at work, and the geometry handles' lifetimes: a mesh
// destroyed while a shape still uses it stays until the shape goes.
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

let DT: f32 = 1.0 / 60.0;

fn kind(t: i32) -> u8[>..] {
    str(match t {
        0 => "capsule",
        1 => "compound",
        2 => "height field",
        3 => "hull",
        4 => "mesh",
        5 => "sphere",
        _ => "?",
    })
}

fn main() {
    let world = physics::create_world(physics::WorldDef { workers: 1 });
    let ground = physics::create_body(world, physics::BodyDef {});
    let thing = physics::create_body(world, physics::BodyDef { body_type: physics::DYNAMIC,
                                                               position: float3 { 0.0, 10.0, 0.0 },
                                                               gravity_scale: 0.0 });
    let def = physics::ShapeDef { density: 1.0 };

    // --- convex shapes -------------------------------------------------------
    let ball = physics::create_sphere_shape(thing, def,
                                            physics::Sphere { center: float3_0, radius: 0.5 });
    let pill = physics::create_capsule_shape(thing, def, physics::Capsule {
        center1: float3 { -1.0, 0.0, 0.0 }, center2: float3 { 1.0, 0.0, 0.0 }, radius: 0.25 });
    let crate = physics::create_box_shape(thing, def, float3 { 0.5, 0.25, 1.0 });
    let tilted = physics::create_box_shape(thing, def, float3 { 0.25, 0.25, 0.25 },
        physics::Transform { p: float3 { 0.0, 2.0, 0.0 },
                             q: physics::quat_axis_angle(float3 { 0.0, 0.0, 1.0 }, 0.785398) });
    let corners: float3[5] = [float3 { 0.0, 0.0, 0.0 }, float3 { 1.0, 0.0, 0.0 },
                              float3 { 0.0, 1.0, 0.0 }, float3 { 0.0, 0.0, 1.0 },
                              float3 { 0.1, 0.1, 0.1 }];
    let tetra = physics::create_hull(corners);
    let flat: float3[4] = [float3 { 0.0, 0.0, 0.0 }, float3 { 1.0, 0.0, 0.0 },
                           float3 { 0.0, 1.0, 0.0 }, float3 { 1.0, 1.0, 0.0 }];
    let none = physics::create_hull(flat);
    print("a hull of points in a plane: handle ", none.id, ", valid ", physics::is_valid(none),
          ", why: ", physics::error());
    let ti = physics::info(tetra);
    print("tetrahedron: ", ti.vertex_count, " vertices, ", ti.half_edge_count / 2, " edges, ",
          ti.face_count, " faces, volume ", num(ti.volume), ", area ", num(ti.surface_area),
          ", center ", vec3(ti.center));
    print("its vertices ", physics::vertices(tetra).len, ", triangles ",
          physics::triangles(tetra).len);
    let tetra_shape = physics::create_hull_shape(thing, def, tetra);
    let mirrored = physics::create_transformed_hull_shape(thing, def, tetra,
        physics::Transform { p: float3 { 0.0, -2.0, 0.0 }, q: physics::quat_identity },
        float3 { -1.0, 2.0, 1.0 });
    let cylinder = physics::create_cylinder_hull(1.0, 0.5, 0.0, 12);
    let cone = physics::create_cone_hull(1.0, 0.5, 0.25, 12);
    let rock = physics::create_rock_hull(0.5);
    let box_hull = physics::create_box_hull(float3 { 1.0, 1.0, 1.0 });
    let bi = physics::info(box_hull);
    print("box hull: ", bi.vertex_count, " vertices, ", bi.face_count, " faces, ",
          physics::triangles(box_hull).len, " triangles, volume ", num(bi.volume), ", bounds ",
          vec3(bi.aabb.lower), " to ", vec3(bi.aabb.upper));
    print("cylinder ", physics::info(cylinder).vertex_count, " vertices; cone ",
          physics::info(cone).vertex_count, "; rock ", physics::info(rock).vertex_count > 8);
    let stretched = physics::transformed(box_hull, physics::transform_identity,
                                         float3 { 2.0, 1.0, 1.0 });
    print("stretched box volume ", num(physics::info(stretched).volume));
    for h in [cylinder, cone, rock] {
        physics::create_transformed_hull_shape(thing, def, h,
            physics::Transform { p: float3 { 3.0, 0.0, 0.0 }, q: physics::quat_identity },
            float3_1);
    }
    // A shape keeps its own copy of a hull: the handle can go.
    physics::destroy(tetra);
    print("tetra handle gone: ", physics::is_valid(tetra), "; its shape stays: ",
          physics::is_valid(tetra_shape), ", a ", kind(physics::shape_type(tetra_shape)));
    print("shapes on the body ", physics::shape_count(thing), ": ", kind(physics::shape_type(ball)),
          ", ", kind(physics::shape_type(pill)), ", ", kind(physics::shape_type(crate)), ", ",
          kind(physics::shape_type(tilted)), ", ", kind(physics::shape_type(mirrored)));

    // --- what a shape holds --------------------------------------------------
    print("belongs to its body ", physics::body(ball) == thing, " and world ",
          physics::world(ball) == world, "; sensor ", physics::is_sensor(ball));
    physics::set_name(ball, "ball");
    physics::set_user_data(ball, 5);
    print("name ", physics::name(ball), ", user data ", physics::user_data(ball));
    let mass_before = physics::mass(thing);
    physics::set_density(ball, 2.0, true);
    print("density ", num(physics::density(ball)), "; the body heavier by ",
          num(physics::mass(thing) - mass_before));
    physics::set_friction(ball, 0.25);
    physics::set_restitution(ball, 0.75);
    print("friction ", num(physics::friction(ball)), ", restitution ",
          num(physics::restitution(ball)));
    physics::set_surface_material(pill, physics::SurfaceMaterial {
        friction: 0.1, restitution: 0.2, rolling_resistance: 0.3,
        tangent_velocity: float3 { 1.0, 0.0, 0.0 }, user_material_id: 77, custom_color: 0xFF0000 });
    let sm = physics::surface_material(pill);
    print("material ", num(sm.friction), " ", num(sm.restitution), " ", num(sm.rolling_resistance),
          " ", vec3(sm.tangent_velocity), " ", sm.user_material_id, " ", sm.custom_color);
    physics::set_shape_filter(crate, physics::Filter { category_bits: 2, mask_bits: 5,
                                                       group_index: -3 }, true);
    let f = physics::shape_filter(crate);
    print("filter ", f.category_bits, " ", f.mask_bits, " ", f.group_index);
    physics::enable_sensor_events(crate, true);
    physics::enable_contact_events(crate, true);
    physics::enable_hit_events(crate, true);
    print("events: sensor ", physics::sensor_events_enabled(crate), ", contact ",
          physics::contact_events_enabled(crate), ", hit ", physics::hit_events_enabled(crate));
    physics::enable_contact_events(crate, false);
    print("contact events off: ", physics::contact_events_enabled(crate));

    // The geometry back, for the right kind of shape.
    let s = physics::sphere(ball);
    let c = physics::capsule(pill);
    print("sphere radius ", num(s.radius), "; capsule ", vec3(c.center1), " to ", vec3(c.center2),
          " radius ", num(c.radius));
    let copy = physics::hull(crate);
    print("the box's hull copied: ", physics::info(copy).vertex_count, " vertices");
    physics::destroy(copy);
    let aabb = physics::aabb(crate);
    print("box bounds ", vec3(aabb.lower), " to ", vec3(aabb.upper));
    let md = physics::mass_data(crate);
    print("box mass ", num(md.mass), " center ", vec3(md.center));
    print("closest point of the ball to (0, 10, 5): ",
          vec3(physics::closest_point(ball, float3 { 0.0, 10.0, 5.0 })));
    let rc = physics::ray_cast(ball, float3 { 0.0, 10.0, -5.0 }, float3 { 0.0, 0.0, 10.0 });
    print("ray at the ball: ", rc.hit, " at ", vec3(rc.point), ", fraction ", num(rc.fraction));

    // A shape can become another kind.
    physics::set_capsule(ball, physics::Capsule { center1: float3 { 0.0, -0.5, 0.0 },
                                                  center2: float3 { 0.0, 0.5, 0.0 }, radius: 0.5 });
    print("the ball now a ", kind(physics::shape_type(ball)));
    physics::set_hull(ball, box_hull);
    print("then a ", kind(physics::shape_type(ball)));
    physics::set_sphere(ball, physics::Sphere { center: float3_0, radius: 0.75 });
    print("and a ", kind(physics::shape_type(ball)), " again, radius ",
          num(physics::sphere(ball).radius));
    physics::apply_mass_from_shapes(thing);

    // Wind pushes a body along.
    physics::apply_wind(pill, float3 { 10.0, 0.0, 0.0 }, 1.0, 0.0, 10.0, true);
    physics::step(world, DT, 4);
    print("wind moves the body: ", physics::linear_velocity(thing).x > 0.0);

    // --- triangle meshes -----------------------------------------------------
    // Two triangles of different materials, in the xz plane.
    let quad: float3[4] = [float3 { -10.0, 0.0, -10.0 }, float3 { 10.0, 0.0, -10.0 },
                           float3 { 10.0, 0.0, 10.0 }, float3 { -10.0, 0.0, 10.0 }];
    let tris: i32[6] = [0, 2, 1, 0, 3, 2];
    let mats: u8[2] = [0, 1];
    let floor_mesh = physics::create_mesh(quad, tris, mats,
                                          physics::MeshDef { identify_edges: true });
    let mi = physics::info(floor_mesh);
    print("floor mesh: ", mi.vertex_count, " vertices, ", mi.triangle_count, " triangles, ",
          mi.material_count, " materials, area ", num(mi.surface_area), ", bounds ",
          vec3(mi.bounds.lower), " to ", vec3(mi.bounds.upper));
    print("its vertices ", physics::vertices(floor_mesh).len, ", triangles ",
          physics::triangles(floor_mesh).len);
    let materials: physics::SurfaceMaterial[2] = [physics::SurfaceMaterial { friction: 0.5 },
                                                  physics::SurfaceMaterial { friction: 0.9 }];
    let floor = physics::create_mesh_shape(ground, physics::ShapeDef {}, floor_mesh, float3_1,
                                           materials);
    print("mesh shape: ", kind(physics::shape_type(floor)), ", same mesh ",
          physics::mesh(floor) == floor_mesh, ", scale ", vec3(physics::mesh_scale(floor)),
          ", materials ", physics::mesh_material_count(floor));
    physics::set_mesh_material(floor, physics::SurfaceMaterial { friction: 0.3 }, 1);
    print("material 1 friction ", num(physics::mesh_material(floor, 1).friction), ", material 0 ",
          num(physics::mesh_material(floor, 0).friction));
    let grid = physics::create_grid_mesh(4, 3, 1.0, 2, true);
    let wave = physics::create_wave_mesh(8, 8, 0.5, 0.25, 1.0, 1.0);
    let torus = physics::create_torus_mesh(16, 8, 2.0, 0.5);
    let box_mesh = physics::create_box_mesh(float3_0, float3 { 1.0, 1.0, 1.0 }, false);
    let hollow = physics::create_hollow_box_mesh(float3_0, float3 { 2.0, 2.0, 2.0 });
    let platform = physics::create_platform_mesh(float3_0, 1.0, 2.0, 4.0);
    print("made meshes: grid ", physics::info(grid).triangle_count, " triangles, wave ",
          physics::info(wave).triangle_count, ", torus ", physics::info(torus).triangle_count,
          ", box ", physics::info(box_mesh).triangle_count, ", hollow box ",
          physics::info(hollow).triangle_count, ", platform ",
          physics::info(platform).triangle_count > 0);
    let bowl = physics::create_body(world, physics::BodyDef {
        position: float3 { 30.0, 0.0, 0.0 } });
    let bowl_shape = physics::create_mesh_shape(bowl, physics::ShapeDef {}, hollow);
    physics::set_mesh(bowl_shape, platform, float3 { 2.0, 1.0, 2.0 });
    print("the bowl's mesh changed: ", physics::mesh(bowl_shape) == platform, ", scale ",
          vec3(physics::mesh_scale(bowl_shape)));

    // --- height fields -------------------------------------------------------
    var heights: f32[>..] = [];
    for z in 5 {
        for x in 5 { heights.push(0.25 * ((x + z) % 3) as f32); }
    }
    var cells: u8[>..] = [];
    for i in 16 {
        cells.push(if i == 5 { physics::HEIGHT_FIELD_HOLE as u8 } else { (i % 2) as u8 });
    }
    let terrain = physics::create_height_field(heights, cells, physics::HeightFieldDef {
        scale: float3 { 2.0, 1.0, 2.0 }, count_x: 5, count_z: 5 });
    let hi = physics::info(terrain);
    print("height field: ", hi.columns, " x ", hi.rows, ", heights ", num(hi.min_height), " to ",
          num(hi.max_height), ", scale ", vec3(hi.scale));
    let hill = physics::create_body(world, physics::BodyDef {
        position: float3 { -30.0, 0.0, 0.0 } });
    let hill_shape = physics::create_height_field_shape(hill, physics::ShapeDef {}, terrain);
    print("height field shape: ", kind(physics::shape_type(hill_shape)), ", same field ",
          physics::height_field(hill_shape) == terrain);
    let flat_field = physics::create_grid_height_field(9, 9, float3 { 1.0, 1.0, 1.0 }, true);
    let wavy = physics::create_wave_height_field(9, 9, float3 { 1.0, 0.5, 1.0 }, 1.0, 1.0, false);
    print("made height fields: ", physics::info(flat_field).columns, " and ",
          physics::info(wavy).rows, " rows");

    // --- a baked compound ----------------------------------------------------
    let spheres: physics::CompoundSphere[2] = [
        physics::CompoundSphere { sphere: physics::Sphere { center: float3 { 0.0, 1.0, 0.0 },
                                                            radius: 0.5 } },
        physics::CompoundSphere { sphere: physics::Sphere { center: float3 { 2.0, 1.0, 0.0 },
                                                            radius: 0.5 } }];
    let capsules: physics::CompoundCapsule[1] = [
        physics::CompoundCapsule { capsule: physics::Capsule {
            center1: float3 { 0.0, 0.0, 0.0 }, center2: float3 { 2.0, 0.0, 0.0 }, radius: 0.2 } }];
    let hulls: physics::CompoundHull[1] = [physics::CompoundHull { hull: box_hull,
        transform: physics::Transform { p: float3 { 5.0, 0.0, 0.0 }, q: physics::quat_identity } }];
    let meshes: physics::CompoundMesh[1] = [physics::CompoundMesh { mesh: box_mesh }];
    let baked = physics::create_compound(spheres, capsules, hulls, meshes);
    let ci = physics::info(baked);
    print("compound: ", ci.sphere_count, " spheres, ", ci.capsule_count, " capsules, ",
          ci.hull_count, " hulls, ", ci.mesh_count, " meshes");
    let fort = physics::create_body(world, physics::BodyDef {
        position: float3 { 0.0, 0.0, 30.0 } });
    let fort_shape = physics::create_compound_shape(fort, physics::ShapeDef {}, baked);
    print("compound shape: ", kind(physics::shape_type(fort_shape)), ", same compound ",
          physics::compound(fort_shape) == baked);

    // --- filters at work -----------------------------------------------------
    // Three boxes fall onto a floor; the ghost's category is not in the floor's
    // mask, and the two in one negative group pass through each other.
    let pit = physics::create_world(physics::WorldDef { workers: 1 });
    let base = physics::create_body(pit, physics::BodyDef {});
    physics::create_box_shape(base, physics::ShapeDef { filter: physics::Filter { mask_bits: 1 } },
                              float3 { 10.0, 0.5, 10.0 });
    let unseen = physics::ShapeDef { filter: physics::Filter { category_bits: 2 } };
    let grouped = physics::ShapeDef {
        filter: physics::Filter { category_bits: 1, group_index: -1 } };
    let cube = float3 { 0.5, 0.5, 0.5 };
    let ghost = physics::create_body(pit, physics::BodyDef {
        body_type: physics::DYNAMIC, position: float3 { -3.0, 3.0, 0.0 } });
    physics::create_box_shape(ghost, unseen, cube);
    let low = physics::create_body(pit, physics::BodyDef {
        body_type: physics::DYNAMIC, position: float3 { 3.0, 1.5, 0.0 } });
    physics::create_box_shape(low, grouped, cube);
    let high = physics::create_body(pit, physics::BodyDef {
        body_type: physics::DYNAMIC, position: float3 { 3.0, 3.0, 0.0 } });
    physics::create_box_shape(high, grouped, cube);
    for i in 90 { physics::step(pit, DT, 4); }
    print("the ghost fell through the floor: ", physics::position(ghost).y < -2.0,
          "; the grouped boxes both rest on it: ", num(physics::position(low).y), " ",
          num(physics::position(high).y));
    physics::destroy(pit);

    // --- lifetimes -----------------------------------------------------------
    // Destroying a mesh a shape uses lets go of the handle; the shape keeps
    // working until it goes too.
    let mesh_bytes = physics::info(floor_mesh).byte_count;
    let bytes = physics::byte_count();
    physics::destroy(floor_mesh);
    print("floor mesh handle gone: ", physics::is_valid(floor_mesh), "; the shape still valid ",
          physics::is_valid(floor), ", its mesh handle now ", physics::mesh(floor).id,
          "; the data kept: ", physics::byte_count() == bytes);
    let dropped = physics::create_body(world, physics::BodyDef {
        body_type: physics::DYNAMIC, position: float3 { 5.0, 1.0, 5.0 } });
    physics::create_sphere_shape(dropped, physics::ShapeDef {}, physics::Sphere { center: float3_0,
                                                                                  radius: 0.5 });
    for i in 60 { physics::step(world, DT, 4); }
    print("a ball lands on it: ", num(physics::position(dropped).y));
    let with_mesh = physics::byte_count();
    physics::destroy(floor);
    print("the shape gone, the mesh's data freed: ",
          with_mesh - physics::byte_count() >= mesh_bytes as i64);
    physics::destroy(terrain);
    physics::destroy(hill);
    physics::destroy(baked);
    physics::destroy(fort_shape);
    for m in [grid, wave, torus, box_mesh, hollow, platform] { physics::destroy(m); }
    print("the bowl's shape still uses the platform mesh: ", physics::is_valid(bowl_shape));
    for h in [cylinder, cone, rock, box_hull, stretched] { physics::destroy(h); }
    physics::destroy(flat_field);
    physics::destroy(wavy);
    physics::destroy(crate);
    print("shapes left on the body: ", physics::shape_count(thing));
    physics::destroy(world);
}
