// error: cannot clear xs while v is still used: the slices it views may point into it
// ss holds a byte view of xs's elements, which v views (§5.1).
fn main() {
    var xs: i32[>..] = [];
    xs.push(1684234849);
    var ss: (const u8[:])[>..] = [];
    ss.push(xs.bytes_of());
    let v = ss[..];
    xs.clear();
    xs.push(1515870810);
    print(v[0]);
}
