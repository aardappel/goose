// Native C and TinyCC must agree where floating-point arguments spill onto
// the stack. On Apple ARM64 a float3 takes 12 bytes there, and a float 4;
// rounding either to eight-byte slots loses arguments that follow them.
// The first two signatures reproduce the sphere and mesh raycast failures.
struct V3 { x: f32, y: f32, z: f32 }
struct V4 { v: V3, w: f32 }
struct Handle { id: u32 }

extern fn abi_sphere(s: V4, origin: V3, translation: V3, fraction: f32) -> i32;
extern fn abi_mesh(m: Handle, scale: V3, origin: V3, translation: V3,
                   fraction: f32) -> i32;
extern fn abi_scalars(a: f32, b: f32, c: f32, d: f32, e: f32, f: f32,
                      g: f32, h: f32, i: f32, j: f32, k: f64) -> i32;
extern fn abi_aligned(a: V4, b: V4, c: f32, d: V3, e: f32, f: f64) -> i32;

fn main() {
    let a = V3 { 1.0, 2.0, 3.0 };
    let b = V3 { 5.0, 6.0, 7.0 };
    let c = V3 { 8.0, 9.0, 10.0 };
    let s = V4 { a, 4.0 };
    print("sphere: ", abi_sphere(s, b, c, 11.0) == 1);
    print("mesh: ", abi_mesh(Handle { 42 }, a, b, c, 11.0) == 1);
    print("scalars: ", abi_scalars(1.0, 2.0, 3.0, 4.0, 5.0, 6.0,
                                   7.0, 8.0, 9.0, 10.0, 11.0) == 1);
    print("alignment: ", abi_aligned(s, V4 { b, 8.0 }, 9.0,
                                      V3 { 10.0, 11.0, 12.0 }, 13.0, 14.0) == 1);
}
