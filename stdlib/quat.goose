// The Goose standard library: `quat` (docs/stdlib.md): quaternions for 3D
// rotations, with glm's names, and their conversions to and from mat's
// matrices. Elementwise + - and scalar * / are the language's (§6.1),
// which is also what a * b of two quaternions is: the product is mul().
// Float types only, and as in mat, a call whose arguments are all literals
// works in f64.

import math;
import vec;
import mat;

// x, y, z the vector part and w the scalar part, in glm's storage order. A
// rotation is a unit quaternion.
struct quat<T> { x: T, y: T, z: T, w: T }

type floatq = quat<f32>;
type doubleq = quat<f64>;

let floatq_identity = floatq { 0.0, 0.0, 0.0, 1.0 };
let doubleq_identity = doubleq { 0.0, 0.0, 0.0, 1.0 };

checkfree fn dot<T>(a: quat<T>, b: quat<T>) -> T { a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w }
checkfree fn length_sq<T>(q: quat<T>) -> T { dot(q, q) }
checkfree fn length<T>(q: quat<T>) -> T { sqrt(dot(q, q)) }
checkfree fn normalize<T>(q: quat<T>) -> quat<T> { q / length(q) }
checkfree fn conjugate<T>(q: quat<T>) -> quat<T> { quat<T> { -q.x, -q.y, -q.z, q.w } }
// conjugate(q) for a unit quaternion.
checkfree fn inverse<T>(q: quat<T>) -> quat<T> { conjugate(q) / dot(q, q) }

// The Hamilton product, glm's a * b: b's rotation, then a's.
checkfree fn mul<T>(a: quat<T>, b: quat<T>) -> quat<T> {
    quat<T> { a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y + a.y * b.w + a.z * b.x - a.x * b.z,
              a.w * b.z + a.z * b.w + a.x * b.y - a.y * b.x, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z }
}

// v rotated by the unit quaternion q, glm's q * v.
checkfree fn rotate<T>(q: quat<T>, v: vec3<T>) -> vec3<T> {
    let u = vec3<T> { q.x, q.y, q.z };
    let t = cross(u, v) * 2.0;
    v + t * q.w + cross(u, t)
}

// glm's angleAxis: `angle` radians about `axis`, counterclockwise as
// rotate(axis, angle) is. The axis need not be unit length.
checkfree fn angle_axis<T>(angle: T, axis: vec3<T>) -> quat<T> {
    let a = normalize(axis) * sin(angle * 0.5);
    quat<T> { a.x, a.y, a.z, cos(angle * 0.5) }
}

// The angle in [0, 2 PI] and unit axis of the unit quaternion q, glm's angle
// and axis; x for the axis of no rotation. From atan2 of the vector part's
// length, which keeps a small angle's precision where acos(w) loses it.
checkfree fn to_angle_axis<T>(q: quat<T>) -> T, vec3<T> {
    let v = vec3<T> { q.x, q.y, q.z };
    let s = length(v);
    if s == 0.0 {
        return 2.0 * atan2(s, q.w), vec3<T> { 1.0, 0.0, 0.0 };
    }
    return 2.0 * atan2(s, q.w), v / s;
}

// glm's rotation: the shortest rotation taking the direction `from` to the
// direction `to`, about some axis perpendicular to `from` when they are
// opposite. Neither need be unit length.
checkfree fn rotation<T>(from: vec3<T>, to: vec3<T>) -> quat<T> {
    let a = normalize(from);
    let b = normalize(to);
    // Half the rotation takes a to h, halfway between a and b, so q is
    // (a x h, a . h): unit length however close to opposite they are.
    let sum = a + b;
    if length_sq(sum) < 0.000000000001 {
        var axis = cross(vec3<T> { 0.0, 0.0, 1.0 }, a);
        if length_sq(axis) < 0.000001 {
            axis = cross(vec3<T> { 1.0, 0.0, 0.0 }, a);
        }
        return angle_axis(PI, axis);
    }
    let h = normalize(sum);
    let v = cross(a, h);
    quat<T> { v.x, v.y, v.z, dot(a, h) }
}

// glm's quatLookAtRH: the rotation that turns -z toward `direction`, with y
// toward `up`, as a camera looking down `direction` is turned. NaN where
// `up` is parallel to `direction`.
checkfree fn quat_look_at<T>(direction: vec3<T>, up: vec3<T>) -> quat<T> {
    let back = -normalize(direction);
    let right = normalize(cross(up, back));
    quat_cast([right, cross(back, right), back])
}

// Linear interpolation, normalized and the shorter way round: cheaper than
// slerp, and as good for small steps, though not at constant speed.
checkfree fn nlerp<T>(a: quat<T>, b: quat<T>, t: T) -> quat<T> {
    var e = b;
    if dot(a, b) < 0.0 { e = -b; }
    normalize(a + (e - a) * t)
}

// Spherical interpolation, the shorter way round, at constant angular speed.
checkfree fn slerp<T>(a: quat<T>, b: quat<T>, t: T) -> quat<T> {
    var e = b;
    var c = dot(a, b);
    if c < 0.0 {
        e = -b;
        c = -c;
    }
    if c > 0.9995 {
        return normalize(a + (e - a) * t);
    }
    let theta = acos(c);
    (a * sin((1.0 - t) * theta) + e * sin(t * theta)) / sin(theta)
}

// glm's quat(eulerAngles): pitch about x, then yaw about y, then roll about
// z, each in radians.
checkfree fn from_euler<T>(e: vec3<T>) -> quat<T> {
    let cx = cos(e.x * 0.5);
    let cy = cos(e.y * 0.5);
    let cz = cos(e.z * 0.5);
    let sx = sin(e.x * 0.5);
    let sy = sin(e.y * 0.5);
    let sz = sin(e.z * 0.5);
    quat<T> { sx * cy * cz - cx * sy * sz, cx * sy * cz + sx * cy * sz, cx * cy * sz - sx * sy * cz,
              cx * cy * cz + sx * sy * sz }
}

// glm's eulerAngles, the inverse of from_euler: pitch, yaw and roll of the
// unit quaternion q, with yaw in [-PI/2, PI/2]. At a yaw of +-PI/2 pitch and
// roll turn about the same axis, and only their difference or sum is
// fixed: how it splits between them follows rounding. Pitch and yaw are
// solved after roll, from the matrix with roll's turn taken out, so the
// three always rebuild q's rotation, there too (glm's can be far off).
checkfree fn euler_angles<T>(q: quat<T>) -> vec3<T> {
    // m is Rz(roll) Ry(yaw) Rx(pitch); m[c].r is row r of column c.
    let m = mat3_cast(q);
    let roll = atan2(m[0].y, m[0].x);
    let c = cos(roll);
    let s = sin(roll);
    let yaw = atan2(-m[0].z, c * m[0].x + s * m[0].y);
    let pitch = atan2(s * m[2].x - c * m[2].y, c * m[1].y - s * m[1].x);
    vec3<T> { pitch, yaw, roll }
}

// glm's mat3_cast and mat4_cast: the rotation matrix of the unit quaternion q.
checkfree fn mat3_cast<T>(q: quat<T>) -> vec3<T>[3] {
    let xx = q.x * q.x;
    let yy = q.y * q.y;
    let zz = q.z * q.z;
    let xy = q.x * q.y;
    let xz = q.x * q.z;
    let yz = q.y * q.z;
    let wx = q.w * q.x;
    let wy = q.w * q.y;
    let wz = q.w * q.z;
    [vec3<T> { 1.0 - 2.0 * (yy + zz), 2.0 * (xy + wz), 2.0 * (xz - wy) },
     vec3<T> { 2.0 * (xy - wz), 1.0 - 2.0 * (xx + zz), 2.0 * (yz + wx) },
     vec3<T> { 2.0 * (xz + wy), 2.0 * (yz - wx), 1.0 - 2.0 * (xx + yy) }]
}
checkfree fn mat4_cast<T>(q: quat<T>) -> vec4<T>[4] { to4x4(mat3_cast(q)) }

// The transform that scales by s, rotates by the unit quaternion q, then
// moves by t: mul(translate(t), mul(mat4_cast(q), scale(s))), with no
// products. glm's recompose, without its skew and perspective.
checkfree fn compose<T>(t: vec3<T>, q: quat<T>, s: vec3<T>) -> vec4<T>[4] {
    let r = mat3_cast(q);
    let x = r[0] * s.x;
    let y = r[1] * s.y;
    let z = r[2] * s.z;
    [vec4<T> { x.x, x.y, x.z, 0.0 }, vec4<T> { y.x, y.y, y.z, 0.0 }, vec4<T> { z.x, z.y, z.z, 0.0 },
     vec4<T> { t.x, t.y, t.z, 1.0 }]
}

// glm's quat_cast: the unit quaternion of the rotation matrix m (its upper
// left for a 4x4), q or -q, both the same rotation. m must be a rotation
// alone: a model matrix's scale gives a quaternion that is not unit length.
checkfree fn quat_cast<T>(m: vec3<T>[3]) -> quat<T> {
    // Shepperd's method: divide by the largest of the four candidates.
    let m00 = m[0].x;
    let m11 = m[1].y;
    let m22 = m[2].z;
    let trace = m00 + m11 + m22;
    if trace > 0.0 {
        let s = sqrt(trace + 1.0) * 2.0;
        return quat<T> { (m[1].z - m[2].y) / s, (m[2].x - m[0].z) / s, (m[0].y - m[1].x) / s, s * 0.25 };
    }
    if m00 > m11 && m00 > m22 {
        let s = sqrt(1.0 + m00 - m11 - m22) * 2.0;
        return quat<T> { s * 0.25, (m[1].x + m[0].y) / s, (m[2].x + m[0].z) / s, (m[1].z - m[2].y) / s };
    }
    if m11 > m22 {
        let s = sqrt(1.0 + m11 - m00 - m22) * 2.0;
        return quat<T> { (m[1].x + m[0].y) / s, s * 0.25, (m[2].y + m[1].z) / s, (m[2].x - m[0].z) / s };
    }
    let s = sqrt(1.0 + m22 - m00 - m11) * 2.0;
    quat<T> { (m[2].x + m[0].z) / s, (m[2].y + m[1].z) / s, s * 0.25, (m[0].y - m[1].x) / s }
}
checkfree fn quat_cast<T>(m: vec4<T>[4]) -> quat<T> { quat_cast(to3x3(m)) }
