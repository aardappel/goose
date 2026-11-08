// error: h is or may point into storage partitioned by this par for
// h holds a reference into the pool values, which get reads through: the
// iterations own dst[k], and dst is values (docs/design/child_threads.md §4.4).
var values: i64[>..] = [];
struct Link { r: i64&<u32 in values> }
fn get(h: Link) -> i64 { h.r }
fn delay(k: i64) -> i64 { return k * 0; }
fn mix(dst: i64[:], h: Link) {
    par for k in dst.len { dst[k] = get(h) + delay(k); }
}
fn main() {
    values.push(927623);
    values.push(1);
    mix(values, Link { r: values[0] });
    print(values[0], " ", values[1]);
}
