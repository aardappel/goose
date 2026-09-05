// A small ray tracer: a few spheres on a checkered floor, one light, hard
// shadows and mirror reflections, written with the library's vec3 math.
// `float3` is an ordinary struct of three f32s; `a + b`, `a - b`, `a * b`
// are the language's elementwise operators on any same-typed struct, and
// dot/normalize/length are library functions. The image is written as a
// binary PPM (open it in any image viewer), and a coarse ASCII version is
// printed so the run shows something without one.
//
// Build and run (from this directory):
//   goose -o raytrace.c raytrace.goose && cl raytrace.c && raytrace

import std;
import vec;
import math;
import os;

struct Sphere { center: float3, radius: f32, color: float3, shine: f32 }
struct Hit { t: f32, point: float3, normal: float3, color: float3, shine: f32 }

let W = 320;
let H = 200;
let LIGHT = float3 { 5.0, 8.0, -3.0 };
let SKY = float3 { 0.55, 0.7, 0.95 };

var spheres: Sphere[>..] = [];

fn scale(v: float3, k: f32) -> float3 { float3 { v.x * k, v.y * k, v.z * k } }

// The nearest intersection along the ray, and whether there was one.
fn trace(origin: float3, dir: float3) -> Hit, bool {
    var best = Hit { t: 1.0e30, point: float3_0, normal: float3_0, color: float3_0, shine: 0.0 };
    var found = false;
    for s in spheres {
        // |origin + t*dir - center|^2 = r^2, a quadratic in t.
        let oc = origin - s.center;
        let b = dot(oc, dir);
        let c = dot(oc, oc) - s.radius * s.radius;
        let disc = b * b - c;
        if disc < 0.0 { continue; }
        let t = 0.0 - b - sqrt(disc);
        if t > 0.001 && t < best.t {
            let p = origin + scale(dir, t);
            best = Hit { t: t, point: p, normal: normalize(p - s.center), color: s.color, shine: s.shine };
            found = true;
        }
    }
    // The floor: the plane y = -1, checkered in world space.
    if dir.y < 0.0 {
        let t = (0.0 - 1.0 - origin.y) / dir.y;
        if t > 0.001 && t < best.t {
            let p = origin + scale(dir, t);
            let check = ((floor(p.x) as i64 + floor(p.z) as i64) % 2) == 0;
            let color = if check { float3 { 0.9, 0.9, 0.9 } } else { float3 { 0.2, 0.25, 0.3 } };
            best = Hit { t: t, point: p, normal: float3 { 0.0, 1.0, 0.0 }, color: color, shine: 0.15 };
            found = true;
        }
    }
    return best, found;
}

fn shadowed(p: float3) -> bool {
    let h, hit = trace(p, normalize(LIGHT - p));
    return hit;
}

// Diffuse light plus a reflection, up to `depth` bounces.
recursive fn shade(origin: float3, dir: float3, depth: i64) -> float3 {
    let h, hit = trace(origin, dir);
    guard hit else { return SKY; }
    let to_light = normalize(LIGHT - h.point);
    var diffuse = max(dot(h.normal, to_light), 0.0);
    if shadowed(h.point) { diffuse = diffuse * 0.2; }
    var color = scale(h.color, 0.15 + 0.85 * diffuse);
    if h.shine > 0.0 && depth > 0 {
        let reflected = dir - scale(h.normal, 2.0 * dot(dir, h.normal));
        let bounce = shade(h.point, reflected, depth - 1);
        color = color + scale(bounce - color, h.shine);
    }
    return color;
}

fn to_byte(x: f32) -> u8 { (clamp(x, 0.0, 1.0) * 255.0) as u8 }

fn main() {
    spheres.push(Sphere { center: float3 { 0.0, 0.0, 4.0 }, radius: 1.0, color: float3 { 0.9, 0.2, 0.2 }, shine: 0.4 });
    spheres.push(Sphere { center: float3 { -2.2, -0.3, 5.0 }, radius: 0.7, color: float3 { 0.2, 0.8, 0.3 }, shine: 0.1 });
    spheres.push(Sphere { center: float3 { 2.0, 0.2, 6.0 }, radius: 1.2, color: float3 { 0.9, 0.85, 0.3 }, shine: 0.7 });
    spheres.push(Sphere { center: float3 { 0.6, -0.7, 2.6 }, radius: 0.3, color: float3 { 0.3, 0.4, 0.95 }, shine: 0.0 });

    // The image: one byte per channel, row by row, built straight into the
    // buffer that becomes the file.
    var img: u8[>..] = [];
    format(img, "P6 ", W, " ", H, " 255\n");
    let t0 = clock();
    let eye = float3 { 0.0, 0.5, 0.0 };
    for y in H {
        for x in W {
            // A pixel's direction: the view plane at z = 1.
            let px = (x as f32 + 0.5) / W as f32 * 2.0 - 1.0;
            let py = 1.0 - (y as f32 + 0.5) / H as f32 * 2.0;
            let dir = normalize(float3 { px * 1.6, py, 1.0 });
            let c = shade(eye, dir, 3);
            img.push(to_byte(c.x));
            img.push(to_byte(c.y));
            img.push(to_byte(c.z));
        }
    }
    let ms = (clock() - t0) * 1000.0;
    let ok = write_file("raytrace.ppm", img);
    print("wrote raytrace.ppm: ", ok, " (", img.len, " bytes)");
    var note: u8[>..] = [];
    format(note, "traced ", W * H, " pixels in ");
    format_flt(note, ms, 1);
    format(note, " ms\n");
    write_stderr(note);

    // The same scene, 80x25, as brightness characters.
    let header = img.len - W * H * 3;
    let ramp = " .:-=+*#%@";
    var line: u8[>..] = [];
    for cy in 25 {
        line.clear();
        for cx in 80 {
            let x = cx * W / 80;
            let y = cy * H / 25;
            let i = header + (y * W + x) * 3;
            let lum = (img[i] as i64 * 3 + img[i + 1] as i64 * 6 + img[i + 2] as i64) / 10;
            line.push(ramp[lum * ramp.len / 256]);
        }
        print(line);
    }
}
