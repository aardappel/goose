// The Goose standard library: `mat` (docs/stdlib.md): square matrices for
// 2D and 3D transforms, over vec's vectors. A matrix is a fixed array of
// its columns, as GLSL's are: m[i] is column i, m[i].y the element in row 1
// of it, and a float4x4 is laid out as a uniform block lays out GLSL's
// mat4. Elementwise + - and scalar * / are the language's (§6.1), which is
// also what a * b of two matrices is (GLSL's matrixCompMult): the products
// are mul(). The names are GLSL's, and glm's where GLSL has none
// (projections). Float types only. A call whose arguments are all literals
// works in f64, as math's functions do; one typed f32 argument makes it f32.

import math;
import vec;

type float2x2 = float2[2];
type float3x3 = float3[3];
type float4x4 = float4[4];
type double2x2 = double2[2];
type double3x3 = double3[3];
type double4x4 = double4[4];

let float2x2_identity: float2x2 = [float2 { 1.0, 0.0 }, float2 { 0.0, 1.0 }];
let float3x3_identity: float3x3 = [float3 { 1.0, 0.0, 0.0 }, float3 { 0.0, 1.0, 0.0 },
                                   float3 { 0.0, 0.0, 1.0 }];
let float4x4_identity: float4x4 = [float4 { 1.0, 0.0, 0.0, 0.0 }, float4 { 0.0, 1.0, 0.0, 0.0 },
                                   float4 { 0.0, 0.0, 1.0, 0.0 }, float4 { 0.0, 0.0, 0.0, 1.0 }];
let double2x2_identity: double2x2 = [double2 { 1.0, 0.0 }, double2 { 0.0, 1.0 }];
let double3x3_identity: double3x3 = [double3 { 1.0, 0.0, 0.0 }, double3 { 0.0, 1.0, 0.0 },
                                     double3 { 0.0, 0.0, 1.0 }];
let double4x4_identity: double4x4 = [double4 { 1.0, 0.0, 0.0, 0.0 }, double4 { 0.0, 1.0, 0.0, 0.0 },
                                     double4 { 0.0, 0.0, 1.0, 0.0 }, double4 { 0.0, 0.0, 0.0, 1.0 }];

// --- products ---------------------------------------------------------------

// mul(m, v) is GLSL's m * v, the column vector v transformed by m.
checkfree fn mul<T>(m: vec2<T>[2], v: vec2<T>) -> vec2<T> { m[0] * v.x + m[1] * v.y }
checkfree fn mul<T>(m: vec3<T>[3], v: vec3<T>) -> vec3<T> { m[0] * v.x + m[1] * v.y + m[2] * v.z }
checkfree fn mul<T>(m: vec4<T>[4], v: vec4<T>) -> vec4<T> {
    m[0] * v.x + m[1] * v.y + m[2] * v.z + m[3] * v.w
}

// mul(v, m) is GLSL's v * m, the row vector v times m: mul(transpose(m), v).
checkfree fn mul<T>(v: vec2<T>, m: vec2<T>[2]) -> vec2<T> { vec2<T> { dot(v, m[0]), dot(v, m[1]) } }
checkfree fn mul<T>(v: vec3<T>, m: vec3<T>[3]) -> vec3<T> {
    vec3<T> { dot(v, m[0]), dot(v, m[1]), dot(v, m[2]) }
}
checkfree fn mul<T>(v: vec4<T>, m: vec4<T>[4]) -> vec4<T> {
    vec4<T> { dot(v, m[0]), dot(v, m[1]), dot(v, m[2]), dot(v, m[3]) }
}

// mul(a, b) is GLSL's a * b: b's transform, then a's.
checkfree fn mul<T>(a: vec2<T>[2], b: vec2<T>[2]) -> vec2<T>[2] { [mul(a, b[0]), mul(a, b[1])] }
checkfree fn mul<T>(a: vec3<T>[3], b: vec3<T>[3]) -> vec3<T>[3] {
    [mul(a, b[0]), mul(a, b[1]), mul(a, b[2])]
}
checkfree fn mul<T>(a: vec4<T>[4], b: vec4<T>[4]) -> vec4<T>[4] {
    [mul(a, b[0]), mul(a, b[1]), mul(a, b[2]), mul(a, b[3])]
}

// GLSL's outerProduct: c times r transposed, so column j is c * r's j-th.
checkfree fn outer_product<T>(c: vec2<T>, r: vec2<T>) -> vec2<T>[2] { [c * r.x, c * r.y] }
checkfree fn outer_product<T>(c: vec3<T>, r: vec3<T>) -> vec3<T>[3] { [c * r.x, c * r.y, c * r.z] }
checkfree fn outer_product<T>(c: vec4<T>, r: vec4<T>) -> vec4<T>[4] {
    [c * r.x, c * r.y, c * r.z, c * r.w]
}

// --- rows, transposes, inverses -----------------------------------------------

checkfree fn transpose<T>(m: vec2<T>[2]) -> vec2<T>[2] {
    [vec2<T> { m[0].x, m[1].x }, vec2<T> { m[0].y, m[1].y }]
}
checkfree fn transpose<T>(m: vec3<T>[3]) -> vec3<T>[3] {
    [vec3<T> { m[0].x, m[1].x, m[2].x }, vec3<T> { m[0].y, m[1].y, m[2].y },
     vec3<T> { m[0].z, m[1].z, m[2].z }]
}
checkfree fn transpose<T>(m: vec4<T>[4]) -> vec4<T>[4] {
    [vec4<T> { m[0].x, m[1].x, m[2].x, m[3].x }, vec4<T> { m[0].y, m[1].y, m[2].y, m[3].y },
     vec4<T> { m[0].z, m[1].z, m[2].z, m[3].z }, vec4<T> { m[0].w, m[1].w, m[2].w, m[3].w }]
}

// Row i of m; a column is m[i]. Bounds-checked, as m[i] is.
fn row<T>(m: vec2<T>[2], i: i64) -> vec2<T> { transpose(m)[i] }
fn row<T>(m: vec3<T>[3], i: i64) -> vec3<T> { transpose(m)[i] }
fn row<T>(m: vec4<T>[4], i: i64) -> vec4<T> { transpose(m)[i] }

checkfree fn determinant<T>(m: vec2<T>[2]) -> T { m[0].x * m[1].y - m[1].x * m[0].y }
checkfree fn determinant<T>(m: vec3<T>[3]) -> T { dot(m[0], cross(m[1], m[2])) }
checkfree fn determinant<T>(m: vec4<T>[4]) -> T {
    let a = xyz(m[0]);
    let b = xyz(m[1]);
    let c = xyz(m[2]);
    let d = xyz(m[3]);
    dot(cross(a, b), c * m[3].w - d * m[2].w) + dot(cross(c, d), a * m[1].w - b * m[0].w)
}

// The inverse from the cofactors; a singular matrix gives infinities and
// NaNs, where GLSL leaves the result undefined.
checkfree fn inverse<T>(m: vec2<T>[2]) -> vec2<T>[2] {
    let r = 1.0 / determinant(m);
    [vec2<T> { m[1].y * r, -m[0].y * r }, vec2<T> { -m[1].x * r, m[0].x * r }]
}
checkfree fn inverse<T>(m: vec3<T>[3]) -> vec3<T>[3] {
    // The rows of the inverse are the columns' pairwise cross products.
    let r0 = cross(m[1], m[2]);
    let r1 = cross(m[2], m[0]);
    let r2 = cross(m[0], m[1]);
    let r = 1.0 / dot(r2, m[2]);
    transpose([r0 * r, r1 * r, r2 * r])
}
checkfree fn inverse<T>(m: vec4<T>[4]) -> vec4<T>[4] {
    // Lengyel's form (Foundations of Game Engine Development, vol. 1):
    // the columns' upper three components a b c d and bottom row x y z w.
    let a = xyz(m[0]);
    let b = xyz(m[1]);
    let c = xyz(m[2]);
    let d = xyz(m[3]);
    let x = m[0].w;
    let y = m[1].w;
    let z = m[2].w;
    let w = m[3].w;
    var s = cross(a, b);
    var t = cross(c, d);
    var u = a * y - b * x;
    var v = c * w - d * z;
    let r = 1.0 / (dot(s, v) + dot(t, u));
    s *= r;
    t *= r;
    u *= r;
    v *= r;
    let r0 = cross(b, v) + t * y;
    let r1 = cross(v, a) - t * x;
    let r2 = cross(d, u) + s * w;
    let r3 = cross(u, c) - s * z;
    transpose([vec4<T> { r0.x, r0.y, r0.z, -dot(b, t) }, vec4<T> { r1.x, r1.y, r1.z, dot(a, t) },
               vec4<T> { r2.x, r2.y, r2.z, -dot(d, s) }, vec4<T> { r3.x, r3.y, r3.z, dot(c, s) }])
}

// --- size conversions -----------------------------------------------------------

// GLSL's mat3(m4) and mat2(m3): the upper-left part.
checkfree fn to3x3<T>(m: vec4<T>[4]) -> vec3<T>[3] { [xyz(m[0]), xyz(m[1]), xyz(m[2])] }
checkfree fn to2x2<T>(m: vec3<T>[3]) -> vec2<T>[2] { [xy(m[0]), xy(m[1])] }

// GLSL's mat4(m3) and mat3(m2): m in the upper left, the identity elsewhere.
checkfree fn to4x4<T>(m: vec3<T>[3]) -> vec4<T>[4] {
    [vec4<T> { m[0].x, m[0].y, m[0].z, 0.0 }, vec4<T> { m[1].x, m[1].y, m[1].z, 0.0 },
     vec4<T> { m[2].x, m[2].y, m[2].z, 0.0 }, vec4<T> { 0.0, 0.0, 0.0, 1.0 }]
}
checkfree fn to3x3<T>(m: vec2<T>[2]) -> vec3<T>[3] {
    [vec3<T> { m[0].x, m[0].y, 0.0 }, vec3<T> { m[1].x, m[1].y, 0.0 }, vec3<T> { 0.0, 0.0, 1.0 }]
}

// --- 3D transforms ------------------------------------------------------------------

// These build a transform, as gfx's do, where glm's translate(m, v) and
// friends multiply one into m: glm's translate(m, t) is mul(m, translate(t)).
checkfree fn translate<T>(t: vec3<T>) -> vec4<T>[4] {
    [vec4<T> { 1.0, 0.0, 0.0, 0.0 }, vec4<T> { 0.0, 1.0, 0.0, 0.0 }, vec4<T> { 0.0, 0.0, 1.0, 0.0 },
     vec4<T> { t.x, t.y, t.z, 1.0 }]
}

checkfree fn scale<T>(s: vec3<T>) -> vec4<T>[4] {
    [vec4<T> { s.x, 0.0, 0.0, 0.0 }, vec4<T> { 0.0, s.y, 0.0, 0.0 }, vec4<T> { 0.0, 0.0, s.z, 0.0 },
     vec4<T> { 0.0, 0.0, 0.0, 1.0 }]
}

// A rotation of `angle` radians about `axis`, counterclockwise looking down
// the axis toward the origin. The axis need not be unit length.
checkfree fn rotate<T>(axis: vec3<T>, angle: T) -> vec4<T>[4] {
    let a = normalize(axis);
    let c = cos(angle);
    let s = sin(angle);
    let k = 1.0 - c;
    [vec4<T> { c + a.x * a.x * k, a.y * a.x * k + a.z * s, a.z * a.x * k - a.y * s, 0.0 },
     vec4<T> { a.x * a.y * k - a.z * s, c + a.y * a.y * k, a.z * a.y * k + a.x * s, 0.0 },
     vec4<T> { a.x * a.z * k + a.y * s, a.y * a.z * k - a.x * s, c + a.z * a.z * k, 0.0 },
     vec4<T> { 0.0, 0.0, 0.0, 1.0 }]
}

checkfree fn rotate_x<T>(angle: T) -> vec4<T>[4] {
    let c = cos(angle);
    let s = sin(angle);
    [vec4<T> { 1.0, 0.0, 0.0, 0.0 }, vec4<T> { 0.0, c, s, 0.0 }, vec4<T> { 0.0, -s, c, 0.0 },
     vec4<T> { 0.0, 0.0, 0.0, 1.0 }]
}
checkfree fn rotate_y<T>(angle: T) -> vec4<T>[4] {
    let c = cos(angle);
    let s = sin(angle);
    [vec4<T> { c, 0.0, -s, 0.0 }, vec4<T> { 0.0, 1.0, 0.0, 0.0 }, vec4<T> { s, 0.0, c, 0.0 },
     vec4<T> { 0.0, 0.0, 0.0, 1.0 }]
}
checkfree fn rotate_z<T>(angle: T) -> vec4<T>[4] {
    let c = cos(angle);
    let s = sin(angle);
    [vec4<T> { c, s, 0.0, 0.0 }, vec4<T> { -s, c, 0.0, 0.0 }, vec4<T> { 0.0, 0.0, 1.0, 0.0 },
     vec4<T> { 0.0, 0.0, 0.0, 1.0 }]
}

// The point p (w = 1) transformed by m, divided by the result's w, so a
// projection's point comes out in normalized device coordinates.
checkfree fn transform_point<T>(m: vec4<T>[4], p: vec3<T>) -> vec3<T> {
    let r = m[0] * p.x + m[1] * p.y + m[2] * p.z + m[3];
    xyz(r) / r.w
}

// The direction v (w = 0) transformed by m: no translation.
checkfree fn transform_vector<T>(m: vec4<T>[4], v: vec3<T>) -> vec3<T> {
    xyz(m[0] * v.x + m[1] * v.y + m[2] * v.z)
}

// --- cameras ------------------------------------------------------------------------

// Right-handed, looking down -z, into SDL_GPU's clip space (gfx's, on every
// backend): y up, depth 0 at `near` and 1 at `far`. As gfx's functions of
// the same names, and glm's perspectiveRH_ZO, orthoRH_ZO and lookAtRH.
checkfree fn perspective<T>(fovy: T, aspect: T, near: T, far: T) -> vec4<T>[4] {
    let f = 1.0 / tan(fovy * 0.5);
    [vec4<T> { f / aspect, 0.0, 0.0, 0.0 }, vec4<T> { 0.0, f, 0.0, 0.0 },
     vec4<T> { 0.0, 0.0, far / (near - far), -1.0 }, vec4<T> { 0.0, 0.0, near * far / (near - far), 0.0 }]
}

checkfree fn ortho<T>(left: T, right: T, bottom: T, top: T, near: T, far: T) -> vec4<T>[4] {
    [vec4<T> { 2.0 / (right - left), 0.0, 0.0, 0.0 }, vec4<T> { 0.0, 2.0 / (top - bottom), 0.0, 0.0 },
     vec4<T> { 0.0, 0.0, 1.0 / (near - far), 0.0 },
     vec4<T> { (left + right) / (left - right), (top + bottom) / (bottom - top), near / (near - far), 1.0 }]
}

// For 2D: the ortho above with near -1 and far 1, so z = 0 is at depth 0.5
// and either side of it is in view. glm's four-argument ortho is for GL's
// depth range instead, and sends a positive z behind the near plane.
checkfree fn ortho<T>(left: T, right: T, bottom: T, top: T) -> vec4<T>[4] {
    ortho(left, right, bottom, top, -1.0, 1.0)
}

// The view from `eye` toward `target`, with `up` toward the top of the view.
// NaN where `up` is parallel to the direction of view.
checkfree fn look_at<T>(eye: vec3<T>, target: vec3<T>, up: vec3<T>) -> vec4<T>[4] {
    let f = normalize(target - eye);
    let s = normalize(cross(f, up));
    let u = cross(s, f);
    [vec4<T> { s.x, u.x, -f.x, 0.0 }, vec4<T> { s.y, u.y, -f.y, 0.0 }, vec4<T> { s.z, u.z, -f.z, 0.0 },
     vec4<T> { -dot(s, eye), -dot(u, eye), dot(f, eye), 1.0 }]
}

// --- 2D transforms ------------------------------------------------------------------

// Homogeneous 3x3 transforms of 2D points, with the translation in m[2].
checkfree fn translate<T>(t: vec2<T>) -> vec3<T>[3] {
    [vec3<T> { 1.0, 0.0, 0.0 }, vec3<T> { 0.0, 1.0, 0.0 }, vec3<T> { t.x, t.y, 1.0 }]
}

checkfree fn scale<T>(s: vec2<T>) -> vec3<T>[3] {
    [vec3<T> { s.x, 0.0, 0.0 }, vec3<T> { 0.0, s.y, 0.0 }, vec3<T> { 0.0, 0.0, 1.0 }]
}

// Counterclockwise by `angle` radians, with x right and y up.
checkfree fn rotate<T>(angle: T) -> vec3<T>[3] {
    let c = cos(angle);
    let s = sin(angle);
    [vec3<T> { c, s, 0.0 }, vec3<T> { -s, c, 0.0 }, vec3<T> { 0.0, 0.0, 1.0 }]
}

checkfree fn transform_point<T>(m: vec3<T>[3], p: vec2<T>) -> vec2<T> {
    let r = m[0] * p.x + m[1] * p.y + m[2];
    xy(r) / r.z
}

checkfree fn transform_vector<T>(m: vec3<T>[3], v: vec2<T>) -> vec2<T> { xy(m[0] * v.x + m[1] * v.y) }
