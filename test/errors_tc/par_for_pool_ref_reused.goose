// error: h is or may point into storage partitioned by this par for
// As par_for_pool_ref_callee.goose, with get's specialization made by a call
// before the loop and reused in it (docs/design/child_threads.md §4.4).
var values: i64[>..] = [];
struct Link { r: i64&<u32 in values> }
fn get(h: Link) -> i64 { h.r }
fn mix(dst: i64[:], h: Link) {
    var first = get(h);
    par for k in dst.len { dst[k] = get(h) + first; }
}
fn main() {
    values.push(927623);
    values.push(1);
    mix(values, Link { r: values[0] });
    print(values[0], " ", values[1]);
}
