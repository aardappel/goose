// error: cannot pop arr while keep is still used: a reference into it was stored there
// A byte view read out of a call's result views what the call put in it:
// here arr's elements.
struct V { b: const u8[:] }
fn view(a: i64[>..]&) -> V { V { b: bytes_of(a) } }
fn main() {
    var arr: i64[>..] = [1, 2, 3];
    var keep: V[>..] = [];
    keep.push(V { b: view(arr).b });
    arr.pop(); arr.pop(); arr.pop();
    arr.push(42);
    print(keep[0].b.len);
}
