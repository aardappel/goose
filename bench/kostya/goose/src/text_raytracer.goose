// Etc::TextRaytracer: one ray per character cell of a w x h grid into a scene
// of three spheres, the first sphere hit shaded to a character of an ASCII
// ramp; the checksum adds up the characters.

namespace text_raytracer;

import std;
import math;
import bench;

// The language's elementwise `+` and `-` on structs of floats are the
// reference's add and sub.
struct Vector { x: f64, y: f64, z: f64 }

fn scale(v: Vector, s: f64) -> Vector { Vector { v.x * s, v.y * s, v.z * s } }
fn dot(v: Vector, other: Vector) -> f64 { v.x * other.x + v.y * other.y + v.z * other.z }
fn magnitude(v: Vector) -> f64 { sqrt(v.dot(v)) }
fn normalize(v: Vector) -> Vector { v.scale(1.0 / v.magnitude()) }

struct Ray { orig: Vector, dir: Vector }

struct Color { r: f64, g: f64, b: f64 }

fn scale(c: Color, s: f64) -> Color { Color { c.r * s, c.g * s, c.b * s } }

struct Sphere { center: Vector, radius: f64, color: Color }

fn get_normal(s: Sphere, pt: Vector) -> Vector { (pt - s.center).normalize() }

struct Light { position: Vector, color: Color }

struct Hit { obj: Sphere, value: f64 }

const WHITE = Color { 1.0, 1.0, 1.0 };
const RED = Color { 1.0, 0.0, 0.0 };
const GREEN = Color { 0.0, 1.0, 0.0 };
const BLUE = Color { 0.0, 0.0, 1.0 };

const LIGHT1 = Light { position: Vector { 0.7, -1.0, 1.7 }, color: WHITE };

const LUT: u8[6] = ['.', '-', '+', '*', 'X', 'M'];

const SCENE: Sphere[3] = [
    Sphere { center: Vector { -1.0, 0.0, 3.0 }, radius: 0.3, color: RED },
    Sphere { center: Vector { 0.0, 0.0, 3.0 }, radius: 0.8, color: GREEN },
    Sphere { center: Vector { 1.0, 0.0, 3.0 }, radius: 0.4, color: BLUE },
];

fn shade_pixel(ray: Ray, obj: Sphere, tval: f64) -> i64 {
    let pi = ray.orig + ray.dir.scale(tval);
    let color = diffuse_shading(pi, obj, LIGHT1);
    let col = (color.r + color.g + color.b) / 3.0;
    return floor(col * 6.0) as i64;
}

// The distance along `ray` to where it enters the sphere, if it does.
fn intersect_sphere(ray: Ray, center: Vector, radius: f64) -> f64, bool {
    let l = center - ray.orig;
    let tca = l.dot(ray.dir);
    if tca < 0.0 { return 0.0, false; }

    let d2 = l.dot(l) - tca * tca;
    let r2 = radius * radius;
    if d2 > r2 { return 0.0, false; }

    let thc = sqrt(r2 - d2);
    let t0 = tca - thc;
    if t0 > 10000.0 { return 0.0, false; }

    return t0, true;
}

fn clamp(x: f64, a: f64, b: f64) -> f64 {
    if x < a { a } else if x > b { b } else { x }
}

fn diffuse_shading(pi: Vector, obj: Sphere, light: Light) -> Color {
    let n = obj.get_normal(pi);
    let light_dir = (light.position - pi).normalize();
    let lam1 = light_dir.dot(n);
    let lam2 = clamp(lam1, 0.0, 1.0);
    let light_color = light.color.scale(lam2 * 0.5);
    let obj_color = obj.color.scale(0.3);
    return light_color + obj_color;
}

fn run(b: Bench&) -> u32 {
    let w = config_i64(b.name, "w");
    let h = config_i64(b.name, "h");
    var result_val: u32 = 0;

    measure(b) {
        let fw = w as f64;
        let fh = h as f64;

        for j in h {
            for i in w {
                let fi = i as f64;
                let fj = j as f64;

                let ray = Ray {
                    orig: Vector { 0.0, 0.0, 0.0 },
                    dir: Vector { (fi - fw / 2.0) / fw, (fj - fh / 2.0) / fh, 1.0 }.normalize(),
                };

                // An optional is a nullable reference rather than a value, so
                // the hit found, if any, is a Hit and a flag.
                var hit = Hit { .. };
                var has_hit = false;

                for obj in SCENE {
                    let tval, found = intersect_sphere(ray, obj.center, obj.radius);
                    if found {
                        hit = Hit { obj: obj, value: tval };
                        has_hit = true;
                        break;
                    }
                }

                let pixel: u8 = if has_hit {
                    let idx = shade_pixel(ray, hit.obj, hit.value);
                    if idx < LUT.len { LUT[idx] } else { ' ' }
                } else {
                    ' '
                };

                result_val += pixel;
            }
        }
    };
    return result_val;
}
