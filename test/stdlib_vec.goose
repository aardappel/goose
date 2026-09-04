// stdlib/vec.goose: the component-combining functions and the aliases.
import vec;

// A global initialized from an imported module's global: imported globals
// initialize first, whatever order the files were parsed in.
let ORIGIN = float3_0;

fn main() {
    print(ORIGIN.x, ORIGIN.y, ORIGIN.z);                          // 000
    let a = float3 { 1.0, 2.0, 3.0 };
    let b = float3 { 4.0, 5.0, 6.0 };
    print(dot(a, b));                                             // 32
    let c = cross(a, b);
    print(c.x, " ", c.y, " ", c.z);                               // -3 6 -3
    print(length_sq(a), " ", length(float3 { 3.0, 4.0, 0.0 }));   // 14 5
    let n = normalize(double3 { 0.0, 3.0, 4.0 });
    print(n.y, " ", n.z);                                         // 0.6 0.8
    print(distance(double2 { 0.0, 0.0 }, double2 { 3.0, 4.0 }));  // 5
    let l = lerp(double3 { 1.0, 2.0, 3.0 }, double3 { 4.0, 5.0, 6.0 }, 0.5);
    print(l.x, " ", l.z);                                         // 2.5 4.5
    let v4 = float4 { 1.0, 2.0, 3.0, 4.0 };
    print(xyz(v4).z, " ", xy(v4).y, " ", xy(a).x);                // 3 2 1
    let s = a + b;
    print(s.x, s.y, s.z);                                         // 579
    let i = int3 { 1, 2, 3 } + int3_1;
    print(i.x, i.y, i.z);                                         // 234
    print(float2_0.x, " ", double4_1.w, " ", int2_1.y);           // 0 1 1
    print(dot(int2 { 2, 3 }, int2 { 4, 5 }));                     // 23
    print(length(double4 { 1.0, 1.0, 1.0, 1.0 }));                // 2
}
