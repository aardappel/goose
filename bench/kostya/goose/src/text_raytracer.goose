// Etc::TextRaytracer: one ray per character cell of a w x h grid into a scene
// of three spheres, the first sphere hit shaded to a character of an ASCII
// ramp; the checksum adds up the characters.

namespace text_raytracer;

import std;
import math;
import vec;
import bench;

// Vectors are the vec module's double3, whose `dot` and `length` are the
// reference's dot and magnitude. The language's elementwise `+ - *` work on
// any struct of floats, so a color is a plain struct too.
struct Color { r: f64, g: f64, b: f64 }
struct Ray { orig: double3, dir: double3 }
struct Sphere { center: double3, radius: f64, color: Color }
struct Light { position: double3, color: Color }

// The reference scales by the reciprocal of the length, which rounds
// differently from vec's normalize dividing by it.
fn normalize(v: double3) -> double3 { v * (1.0 / length(v)) }

const LIGHT1 = Light { position: double3 { 0.7, -1.0, 1.7 }, color: Color { 1.0, 1.0, 1.0 } };

const LUT: u8[6] = ".-+*XM";

const SCENE: Sphere[3] = [
    Sphere { center: double3 { -1.0, 0.0, 3.0 }, radius: 0.3, color: Color { 1.0, 0.0, 0.0 } },
    Sphere { center: double3 { 0.0, 0.0, 3.0 }, radius: 0.8, color: Color { 0.0, 1.0, 0.0 } },
    Sphere { center: double3 { 1.0, 0.0, 3.0 }, radius: 0.4, color: Color { 0.0, 0.0, 1.0 } },
];

// The distance along `ray` to where it enters the sphere, if it does.
fn intersect_sphere(ray: Ray, center: double3, radius: f64) -> f64, bool {
    let l = center - ray.orig;
    let tca = dot(l, ray.dir);
    if tca < 0.0 { return 0.0, false; }
    let d2 = dot(l, l) - tca * tca;
    let r2 = radius * radius;
    if d2 > r2 { return 0.0, false; }
    let thc = sqrt(r2 - d2);
    let t0 = tca - thc;
    if t0 > 10000.0 { return 0.0, false; }
    return t0, true;
}

fn diffuse_shading(pi: double3, obj: Sphere, light: Light) -> Color {
    let n = normalize(pi - obj.center);
    let light_dir = normalize(light.position - pi);
    let lam1 = dot(light_dir, n);
    let lam2 = clamp(lam1, 0.0, 1.0);
    return light.color * (lam2 * 0.5) + obj.color * 0.3;
}

// The ramp index of the point `tval` along `ray`, on `obj`.
fn shade_pixel(ray: Ray, obj: Sphere, tval: f64) -> i64 {
    let pi = ray.orig + ray.dir * tval;
    let color = diffuse_shading(pi, obj, LIGHT1);
    let col = (color.r + color.g + color.b) / 3.0;
    return floor(col * 6.0) as i64;
}

fn run(b: Bench&) -> u32 {
    let w = config_i64(b.name, "w");
    let h = config_i64(b.name, "h");
    var result: u32 = 0;
    measure(b) {
        let fw = w as f64;
        let fh = h as f64;
        for j in h {
            for i in w {
                let ray = Ray {
                    orig: double3 { 0.0, 0.0, 0.0 },
                    dir: normalize(double3 { (i - fw / 2.0) / fw, (j - fh / 2.0) / fh, 1.0 }),
                };
                var pixel: u8 = ' ';
                for obj in SCENE {
                    let tval, hit = intersect_sphere(ray, obj.center, obj.radius);
                    if hit {
                        let idx = shade_pixel(ray, obj, tval);
                        if idx < LUT.len { pixel = LUT[idx]; }
                        break;
                    }
                }
                result += pixel;
            }
        }
    };
    return result;
}
