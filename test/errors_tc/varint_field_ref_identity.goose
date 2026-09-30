// error: .== needs references to the same type, got const varint& and i64&
// .== takes a varint field by reference as the varint& it is (§3.6, §4.5),
// which is no reference to an i64 field however both read.
struct V { n: varint, w: i64 }
fn main() {
    let v = V { n: 300, w: 4 };
    print(v.n .== v.w);
}
