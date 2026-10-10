// error: cannot pop p while v is still used
// The tail field of a struct with a variable-size prefix shrinks under the
// same rule (§5.1).
struct P { c: u8[], j: i64[>..] }
fn main() {
    var p = P { c: [1, 2], j: [4, 5, 6] };
    let v = p.j[..];
    p.j.pop();
    print(v[0]);
}
