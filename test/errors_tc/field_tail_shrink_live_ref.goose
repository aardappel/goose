// error: cannot pop t while r is still used
// A struct's tail field shrinks the storage the struct lies in: a reference
// into it still used afterwards stops the shrink (§5.1).
struct S { n: i64, a: u8[>..] }
fn main() {
    var t = S { n: 1, a: [1, 2, 3] };
    let r: u8& = t.a[0];
    t.a.pop();
    print(r);
}
