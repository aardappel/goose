// error: .index_of takes a reference to an element of i64[>..], got varint
// A varint field is no i64 element however it reads (§3.3, §3.6).
struct V { n: varint, w: i64 }
fn main() {
    let v = V { n: 300, w: 4 };
    var xs: i64[>..] = [1, 2];
    print(xs.index_of(v.n));
}
