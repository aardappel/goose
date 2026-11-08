// error: cannot assign through this path (const, or a read-only instantiation, §9.5)
// R1 (§4.3): a slice an array from outside the body holds, bound by a plain
// for in the body, is read-only there, as the array is.
fn main() {
    var xs = [1, 2, 3, 4];
    let views: i64[:][1] = [xs[0..4]];
    par for k in 4 { for &v in views { v[0] = k; } }
    print(xs);
}
