// A struct holding a reference to a loop-body local, pushed into an array
// declared outside the loop (§9.2).
struct S { r: i64&, k: i64 }
fn main() {
    var outer: S[>..] = [];
    for i in 3 {
        var x = i;
        let s = S { r: x, k: i };
        outer.push(s);
    }
    print(outer.len);
}
