// error: cannot write through this reference
// `.=` binds a varint field as the read-only varint& it is (§3.6, §3.8): a
// varint is written only at construction, so nothing writes through it.
struct V { n: varint, w: i64 }
fn main() {
    var v = V { n: 300, w: 4 };
    let r .= v.n;
    r = 7;
    print(v);
}
