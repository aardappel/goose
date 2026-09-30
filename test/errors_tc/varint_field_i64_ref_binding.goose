// error: expected a value of type const i64&, got varint
// A varint field binds a varint& (§3.6, §3.8), never an i64&, const or
// not: the reference would read the encoded bytes as an i64.
struct V { n: varint, w: i64 }
fn main() {
    let v = V { n: 300, w: 4 };
    let r: const i64& = v.n;
    print(r);
}
