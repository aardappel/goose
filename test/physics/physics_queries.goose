// Queries in the physics module: overlaps, ray casts (every hit, nearest
// first, and the closest), shape casts and query filters against a world;
// a character mover cast, collided and moved by the plane solver; geometry
// on its own -- mass, bounds, rays, overlaps and shape casts against spheres,
// capsules, hulls, meshes, height fields and compounds, distance, casts and
// time of impact between point clouds -- and the rotation and transform
// helpers.
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

fn cast(what: const u8[:], c: physics::CastOutput) {
    if c.hit {
        print(what, ": hit at ", vec3(c.point), ", normal ", vec3(c.normal), ", fraction ",
              num(c.fraction));
    } else {
        print(what, ": no hit");
    }
}

fn main() {
    let world = physics::create_world(physics::WorldDef { workers: 1 });
    let ground = physics::create_body(world, physics::BodyDef {});
    physics::create_box_shape(ground, physics::ShapeDef {}, float3 { 20.0, 0.5, 20.0 },
                              physics::Transform { p: float3 { 0.0, -0.5, 0.0 },
                                                   q: physics::quat_identity });
    // Three boxes in a row along x; the right one in collision category 2.
    var boxes: physics::Shape[>..] = [];
    for i in 3 {
        let b = physics::create_body(world, physics::BodyDef {
            position: float3 { -4.0 + 4.0 * i as f32, 1.0, 0.0 } });
        let category: u64 = if i == 2 { 2 } else { 1 };
        boxes.push(physics::create_box_shape(b, physics::ShapeDef {
            filter: physics::Filter { category_bits: category } }, float3 { 0.5, 0.5, 0.5 }));
    }

    // --- overlaps ------------------------------------------------------------
    let middle = physics::overlap_aabb(world, physics::AABB { lower: float3 { -1.0, 0.75, -1.0 },
                                                              upper: float3 { 1.0, 1.25, 1.0 } },
                                       physics::QueryFilter {});
    print("box overlap: ", middle.len, " shape, the middle box ", middle[0] == boxes[1]);
    let probe: float3[1] = [float3_0];
    let touching = physics::overlap_shape(world, float3 { 4.0, 1.6, 0.0 }, probe, 0.2,
                                          physics::QueryFilter {});
    print("sphere overlap on the right box: ", touching.len, ", ", touching[0] == boxes[2]);
    let segment: float3[2] = [float3 { -5.0, 0.0, 0.0 }, float3 { 5.0, 0.0, 0.0 }];
    print("a capsule through all three: ",
          physics::overlap_shape(world, float3 { 0.0, 1.0, 0.0 }, segment, 0.1,
                                 physics::QueryFilter {}).len);

    // --- rays ----------------------------------------------------------------
    let from = float3 { -10.0, 1.0, 0.0 };
    let along = float3 { 20.0, 0.0, 0.0 };
    let hits = physics::cast_ray(world, from, along, physics::QueryFilter {});
    print("a ray through the row hits ", hits.len, ", nearest first:");
    for h in hits {
        print("  x ", num(h.point.x), " normal ", vec3(h.normal), " fraction ", num(h.fraction),
              ", which ", position(boxes, h.shape));
    }
    let first = physics::cast_ray_closest(world, from, along, physics::QueryFilter {});
    print("closest: ", first.hit, " at x ", num(first.point.x), ", the left box ",
          first.shape == boxes[0]);
    let skip2 = physics::QueryFilter { category_bits: 0xFFFFFFFFFFFFFFFF, mask_bits: 1 };
    print("seeing only category 1: ", physics::cast_ray(world, from, along, skip2).len, " hits");
    let miss = physics::cast_ray_closest(world, float3 { -10.0, 5.0, 0.0 }, along,
                                         physics::QueryFilter {});
    print("a ray above them: hit ", miss.hit);
    let swept = physics::cast_shape(world, from, probe, 0.25, along, physics::QueryFilter {});
    print("a sphere swept along the row hits ", swept.len, ", first at x ", num(swept[0].point.x),
          " fraction ", num(swept[0].fraction));

    // --- a character mover ---------------------------------------------------
    let mover = physics::Capsule { center1: float3 { 0.0, -0.4, 0.0 },
                                   center2: float3 { 0.0, 0.4, 0.0 }, radius: 0.3 };
    let free = physics::cast_mover(world, float3 { -10.0, 1.2, 0.0 }, mover,
                                   float3 { 10.0, 0.0, 0.0 }, physics::QueryFilter {});
    print("mover free for ", num(free), " of its move toward the left box");
    let planes = physics::collide_mover(world, float3 { -3.3, 1.0, 0.0 }, mover,
                                        physics::QueryFilter {});
    print("mover against the left box's side: ", planes.len, " plane");
    var solver: physics::CollisionPlane[>..] = [];
    for p in planes {
        print("  normal ", vec3(p.plane.normal), " offset ", num(p.plane.offset), ", the box ",
              p.shape == boxes[0]);
        solver.push(physics::CollisionPlane { plane: p.plane });
    }
    let delta, iterations = physics::solve_planes(float3 { -1.0, -1.0, 0.0 }, solver);
    print("moving into both, the solver allows ", vec3(delta), " in ", iterations > 0,
          " iterations");
    print("a velocity into both, clipped: ",
          vec3(physics::clip_vector(float3 { -2.0, -3.0, 1.0 }, solver)));

    // --- geometry on its own -------------------------------------------------
    let at = physics::transform_identity;
    let moved = physics::Transform { p: float3 { 10.0, 0.0, 0.0 }, q: physics::quat_identity };
    let sphere = physics::Sphere { center: float3_0, radius: 1.0 };
    let capsule = physics::Capsule { center1: float3 { 0.0, -1.0, 0.0 },
                                     center2: float3 { 0.0, 1.0, 0.0 }, radius: 0.5 };
    let cube = physics::create_box_hull(float3 { 1.0, 1.0, 1.0 });
    let sm = physics::compute_mass(sphere, 1.0);
    let cm = physics::compute_mass(capsule, 1.0);
    let hm = physics::compute_mass(cube, 1.0);
    print("masses: sphere ", num(sm.mass), " inertia ", num(sm.inertia.cx.x), "; capsule ",
          num(cm.mass), "; cube ", num(hm.mass), " inertia ", num(hm.inertia.cy.y));
    print("bounds moved to x 10: sphere ", vec3(physics::compute_aabb(sphere, moved).lower),
          "; capsule ", vec3(physics::compute_aabb(capsule, moved).upper), "; cube ",
          vec3(physics::compute_aabb(cube, moved).lower));
    cast("ray at the sphere", physics::ray_cast(sphere, float3 { -5.0, 0.0, 0.0 },
                                                float3 { 10.0, 0.0, 0.0 }, 1.0));
    cast("ray at the capsule", physics::ray_cast(capsule, float3 { 0.0, 5.0, 0.0 },
                                                 float3 { 0.0, -10.0, 0.0 }, 1.0));
    cast("ray at the cube", physics::ray_cast(cube, float3 { 0.0, 0.0, 5.0 },
                                              float3 { 0.0, 0.0, -10.0 }, 1.0));
    cast("ray short of the cube", physics::ray_cast(cube, float3 { 0.0, 0.0, 5.0 },
                                                    float3 { 0.0, 0.0, -10.0 }, 0.3));
    let grid = physics::create_grid_mesh(4, 4, 1.0, 0, false);
    let gb = physics::compute_aabb(grid, at, float3 { 2.0, 1.0, 2.0 });
    print("grid mesh scaled by 2: bounds ", vec3(gb.lower), " to ", vec3(gb.upper));
    cast("ray down at the mesh", physics::ray_cast(grid, float3 { 2.0, 1.0, 2.0 },
                                                   float3 { 1.0, 3.0, 1.0 },
                                                   float3 { 0.0, -6.0, 0.0 }, 1.0));
    let field = physics::create_grid_height_field(5, 5, float3 { 1.0, 1.0, 1.0 }, false);
    let fb = physics::compute_aabb(field, at);
    print("height field bounds ", vec3(fb.lower), " to ", vec3(fb.upper));
    cast("ray down at the height field", physics::ray_cast(field, float3 { 1.5, 3.0, 1.5 },
                                                           float3 { 0.0, -6.0, 0.0 }, 1.0));
    let parts: physics::CompoundSphere[2] = [
        physics::CompoundSphere { sphere: physics::Sphere { center: float3 { -2.0, 0.0, 0.0 },
                                                            radius: 0.5 } },
        physics::CompoundSphere { sphere: physics::Sphere { center: float3 { 2.0, 0.0, 0.0 },
                                                            radius: 0.5 } }];
    let no_capsules: physics::CompoundCapsule[>..] = [];
    let no_hulls: physics::CompoundHull[>..] = [];
    let no_meshes: physics::CompoundMesh[>..] = [];
    let pair = physics::create_compound(parts, no_capsules, no_hulls, no_meshes);
    let pb = physics::compute_aabb(pair, at);
    print("compound bounds ", vec3(pb.lower), " to ", vec3(pb.upper));
    cast("ray between the compound's spheres", physics::ray_cast(pair, float3 { 0.0, 5.0, 0.0 },
                                                                 float3 { 0.0, -10.0, 0.0 }, 1.0));
    cast("ray at its right sphere", physics::ray_cast(pair, float3 { 2.0, 5.0, 0.0 },
                                                      float3 { 0.0, -10.0, 0.0 }, 1.0));

    let dot: float3[1] = [float3 { 0.0, 1.2, 0.0 }];
    print("overlaps of a small sphere at y 1.2: sphere ",
          physics::overlap(sphere, at, dot, 0.25), ", capsule ",
          physics::overlap(capsule, at, dot, 0.25), ", cube ",
          physics::overlap(cube, at, dot, 0.25), ", cube moved away ",
          physics::overlap(cube, moved, dot, 0.25), ", mesh ",
          physics::overlap(grid, float3_1, at, dot, 0.25), ", height field ",
          physics::overlap(field, at, dot, 0.25), ", compound ",
          physics::overlap(pair, at, dot, 0.25));
    let low: float3[1] = [float3 { 1.0, 0.1, 1.0 }];
    print("and one resting on the flat ones: mesh ",
          physics::overlap(grid, float3_1, at, low, 0.25), ", height field ",
          physics::overlap(field, at, low, 0.25));
    let start: float3[1] = [float3 { 0.0, 5.0, 0.0 }];
    let down = float3 { 0.0, -10.0, 0.0 };
    cast("sphere cast at the sphere", physics::shape_cast(sphere, start, 0.5, down, 1.0, false));
    cast("sphere cast at the capsule", physics::shape_cast(capsule, start, 0.5, down, 1.0, false));
    cast("sphere cast at the cube", physics::shape_cast(cube, start, 0.5, down, 1.0, false));
    let above: float3[1] = [float3 { 1.5, 5.0, 1.5 }];
    cast("sphere cast at the mesh",
         physics::shape_cast(grid, float3_1, above, 0.5, down, 1.0, false));
    cast("sphere cast at the height field",
         physics::shape_cast(field, above, 0.5, down, 1.0, false));
    let over: float3[1] = [float3 { 2.0, 5.0, 0.0 }];
    cast("sphere cast at the compound", physics::shape_cast(pair, over, 0.5, down, 1.0, false));

    // Between two clouds of points: box corners, one box 3 along x.
    var corners: float3[>..] = [];
    for i in 8 {
        corners.push(float3 { if i & 1 != 0 { 0.5 } else { -0.5 },
                              if i & 2 != 0 { 0.5 } else { -0.5 },
                              if i & 4 != 0 { 0.5 } else { -0.5 } });
    }
    let apart = physics::Transform { p: float3 { 3.0, 0.0, 0.0 }, q: physics::quat_identity };
    let d = physics::shape_distance(corners, 0.0, corners, 0.0, apart, true);
    print("distance between the boxes ", num(d.distance), ", from ", vec3(d.point_a), " to ",
          vec3(d.point_b), ", normal ", vec3(d.normal));
    let rounded = physics::shape_distance(corners, 0.25, corners, 0.25, apart, true);
    print("with radii 0.25 each: ", num(rounded.distance));
    cast("box cast into the other", physics::shape_cast(corners, 0.0, corners, 0.0, apart,
                                                        float3 { -4.0, 0.0, 0.0 }, 1.0, false));
    let still = physics::Sweep { local_center: float3_0, c1: float3_0, c2: float3_0,
                                 q1: physics::quat_identity, q2: physics::quat_identity };
    let flying = physics::Sweep { local_center: float3_0, c1: float3 { 5.0, 0.0, 0.0 },
                                  c2: float3 { -5.0, 0.0, 0.0 }, q1: physics::quat_identity,
                                  q2: physics::quat_identity };
    let toi = physics::time_of_impact(corners, 0.0, still, corners, 0.0, flying, 1.0);
    print("time of impact: hit ", toi.state == physics::TOI_HIT, " at ", num(toi.fraction));
    let passing = physics::Sweep { local_center: float3_0, c1: float3 { 5.0, 3.0, 0.0 },
                                   c2: float3 { -5.0, 3.0, 0.0 }, q1: physics::quat_identity,
                                   q2: physics::quat_identity };
    print("passing above: separated ",
          physics::time_of_impact(corners, 0.0, still, corners, 0.0, passing, 1.0).state ==
          physics::TOI_SEPARATED);
    print("the flying box halfway: ", vec3(physics::sweep_transform(flying, 0.5).p));

    // --- rotations and transforms --------------------------------------------
    let quarter = physics::quat_axis_angle(float3 { 0.0, 0.0, 2.0 }, 1.5707964);
    print("a quarter turn about z takes x to ",
          vec3(physics::rotate(quarter, float3 { 1.0, 0.0, 0.0 })), " and back ",
          vec3(physics::inv_rotate(quarter, float3 { 0.0, 1.0, 0.0 })));
    let half = physics::mul(quarter, quarter);
    print("twice: x to ", vec3(physics::rotate(half, float3 { 1.0, 0.0, 0.0 })), "; conjugate ",
          num(physics::conjugate(quarter).z), "; normalized length ",
          num(length(float4 { half.x, half.y, half.z, half.w })));
    let doubled = physics::normalize(physics::Quat { 0.0, 0.0, 2.0, 2.0 });
    print("normalized ", num(doubled.z), " ", num(doubled.w));
    let m = physics::rotation_matrix(quarter);
    print("as a matrix: ", vec3(m.cx), " / ", vec3(m.cy), " / ", vec3(m.cz));
    let t = physics::Transform { p: float3 { 1.0, 2.0, 3.0 }, q: quarter };
    let p = physics::transform_point(t, float3 { 1.0, 0.0, 0.0 });
    print("a point through the transform ", vec3(p), " and back ",
          vec3(physics::inv_transform_point(t, p)));
    let tt = physics::mul(t, t);
    print("the transform twice: ", vec3(tt.p), "; undone ",
          vec3(physics::inv_mul(t, tt).p));
    let body = physics::create_body(world, physics::BodyDef { position: float3 { 5.0, 0.0, 0.0 },
                                                              rotation: quarter });
    let lf = physics::local_frame(body, physics::Transform { p: float3 { 5.0, 1.0, 0.0 },
                                                             q: physics::quat_identity });
    print("a world frame seen from a turned body: ", vec3(lf.p));
    let jd = physics::joint_def(ground, body, float3 { 5.0, 1.0, 0.0 });
    print("a joint definition from an anchor: ", vec3(jd.frame_a.p), " / ", vec3(jd.frame_b.p));

    physics::destroy(cube);
    physics::destroy(grid);
    physics::destroy(field);
    physics::destroy(pair);
    physics::destroy(world);
}
