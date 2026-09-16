// Float members compare as the scalars do (§4.5): -0.0 equals 0.0 and a NaN
// equals nothing, whichever aggregate, array or slice holds them.
import vec;

struct Mixed { id: i64, w: f64 }
struct Seg { a: float3, b: float3 }
struct Cells { c: f32[3] }
enum Reading { Missing, Value { v: f32 } }
struct Named { name: u8[], pos: float3 }
struct Series { name: u8[], v: f64[] }
enum Shape { Dot, Label { text: u8[], size: f32 } }

fn same<T>(x: T[:], y: T[:]) -> bool { x == y }

fn main() {
    // Read from an array, so no value is known to the optimizer.
    var src: f32[>..] = [0.0, 1.0];
    let z = src[0];
    let nz = -z;
    let n = z / z;
    let zd = z as f64;
    let nzd = -zd;
    let nd = zd / zd;
    print(z == nz, " ", n == n, " ", n != n, " ", zd == nzd, " ", nd == nd);   // true false true true false

    // Structs, nested structs and struct-held arrays.
    print(float3 { z, 1.0, 2.0 } == float3 { nz, 1.0, 2.0 }, " ",
          float3 { n, 1.0, 2.0 } == float3 { n, 1.0, 2.0 }, " ",
          float3 { n, 1.0, 2.0 } != float3 { n, 1.0, 2.0 }, " ",
          float3 { z, 1.0, 2.0 } == float3 { z, 1.0, 3.0 });                   // true false true false
    print(double2 { zd, nzd } == double2 { nzd, zd }, " ",
          Mixed { 1, nzd } == Mixed { 1, zd }, " ",
          Mixed { 1, nd } == Mixed { 1, nd }, " ",
          Mixed { 1, zd } == Mixed { 2, zd });                                 // true true false false
    let s0 = Seg { float3_1, float3 { z, z, z } };
    print(s0 == Seg { float3_1, float3 { nz, nz, nz } }, " ",
          Seg { float3_1, float3 { n, z, z } } == Seg { float3_1, float3 { n, z, z } }, " ",
          Cells { [z, 1.0, nz] } == Cells { [nz, 1.0, z] }, " ",
          Cells { [z, n, z] } == Cells { [z, n, z] });                         // true false true false

    // Fixed arrays of floats and of float structs.
    let xa: f32[2] = [z, 1.0];
    let xb: f32[2] = [nz, 1.0];
    let xn: f32[2] = [n, 1.0];
    let va: float3[2] = [float3_1, float3 { z, 1.0, 1.0 }];
    let vb: float3[2] = [float3_1, float3 { nz, 1.0, 1.0 }];
    let vn: float3[2] = [float3_1, float3 { n, 1.0, 1.0 }];
    print(xa == xb, " ", xn == xn, " ", va == vb, " ", vn == vn);              // true false true false

    // Limited arrays.
    let la: float3[..4] = [float3 { z, 1.0, 1.0 }];
    let lb: float3[..4] = [float3 { nz, 1.0, 1.0 }];
    let ln: float3[..4] = [float3 { n, 1.0, 1.0 }];
    let ls: f32[..4] = [z, nz];
    let lt: f32[..4] = [nz, z];
    print(la == lb, " ", ln == ln, " ", ls == lt);                             // true false true

    // Slices.
    print(same(xa[..], xb[..]), " ", same(xn[..], xn[..]), " ",
          same(va[..], vb[..]), " ", same(vn[..], vn[..]), " ",
          same(xa[..], xb[0..1]));                                             // true false true false false

    // Resizable arrays, and arrays of different kinds compared as slices.
    var ra: f32[>..] = [z, 1.0];
    var rb: f32[>..] = [nz, 1.0];
    var rn: f32[>..] = [n, 1.0];
    var rva: float3[>..] = [float3 { 1.0, z, 1.0 }];
    var rvb: float3[>..] = [float3 { 1.0, nz, 1.0 }];
    var rvn: float3[>..] = [float3 { 1.0, n, 1.0 }];
    print(ra == rb, " ", rn == rn, " ", rva == rvb, " ", rvn == rvn, " ",
          ra == xb, " ", rn == xn);                                            // true false true false true false
    rb.push(1.0);
    print(ra == rb);                                                           // false

    // Fixed-mode ADT payloads.
    let ez: Reading = Reading.Value { v: z };
    let enz: Reading = Reading.Value { v: nz };
    let en: Reading = Reading.Value { v: n };
    let em: Reading = Reading.Missing {};
    print(ez == enz, " ", en == en, " ", ez == em);                            // true false false

    // Bytes values: floats beside variable-size text and arrays.
    let na = Named { name: "p", pos: float3 { z, 1.0, 1.0 } };
    let nb = Named { name: "p", pos: float3 { nz, 1.0, 1.0 } };
    let nn = Named { name: "p", pos: float3 { n, 1.0, 1.0 } };
    let nq = Named { name: "q", pos: float3 { z, 1.0, 1.0 } };
    print(na == nb, " ", nn == nn, " ", na == nq);                             // true false false
    let sa = Series { name: "s", v: [zd, 1.0, nzd] };
    let sb = Series { name: "s", v: [nzd, 1.0, zd] };
    let sn = Series { name: "s", v: [zd, nd] };
    let sl = Series { name: "s", v: [zd, 1.0] };
    print(sa == sb, " ", sn == sn, " ", sa == sl);                             // true false false
    var da: Shape.. = Shape.Label { text: "t", size: z };
    var db: Shape.. = Shape.Label { text: "t", size: nz };
    var dn: Shape.. = Shape.Label { text: "t", size: n };
    var dd: Shape.. = Shape.Dot {};
    print(da == db, " ", dn == dn, " ", da == dd);                             // true false false
    var ga: Named[>..] = [];
    var gb: Named[>..] = [];
    var gn: Named[>..] = [];
    ga.push(Named { name: "a", pos: float3 { 1.0, 1.0, z } });
    gb.push(Named { name: "a", pos: float3 { 1.0, 1.0, nz } });
    gn.push(Named { name: "a", pos: float3 { 1.0, 1.0, n } });
    print(ga == gb, " ", gn == gn);                                            // true false
}
