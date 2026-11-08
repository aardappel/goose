// error: src is or may point into storage partitioned by this par for
// A byte view may view the elements of any array (bytes_of), those of the
// one the loop partitions too (docs/design/child_threads.md §4.4).
fn mix(dst: i64[:], src: const u8[:]) {
    par for k in dst.len { dst[k] = src[(k * 8 + 8) % src.len]; }
}
fn main() {
    var a: i64[8] = [1, 2, 3, 4, 5, 6, 7, 8];
    var b: i64[8] = [10, 20, 30, 40, 50, 60, 70, 80];
    let c = a[0] == 1;
    mix(a, if c { bytes_of(a) } else { bytes_of(b) });
    print(a);
}
