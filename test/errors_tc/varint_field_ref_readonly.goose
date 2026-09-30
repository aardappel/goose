// error: storing a read-only reference of type const varint& in a slot of type varint&
// A reference to a varint field is read-only (§3.6, §3.8), bound without &
// as with it, so only a const varint& variable holds one.
struct V { n: varint, w: i64 }
fn main() {
    var v = V { n: 300, w: 4 };
    let r: varint& = v.n;
    print(r);
}
