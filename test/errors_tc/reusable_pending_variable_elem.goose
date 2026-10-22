// error: reusable applies to grow-only arrays of fixed-size elements (§5.4), and this makes pool a u8[][>..]
// A pool declared with `= []` takes its element type from what first goes
// into it (§4.2), and that must be fixed-size as an annotated pool's is
// (§5.4): a string literal makes the elements owned strings.
fn main() {
    reusable var pool = [];
    let i = pool.alloc_index("abc");
    pool.free(i);
    print(i);
}
