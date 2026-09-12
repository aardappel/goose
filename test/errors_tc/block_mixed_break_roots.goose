var global: i64[1] = [7];
fn bad(flag: bool) -> i64[:] {
    var local: i64[1] = [42];
    return block {
        if flag { break local[..]; }
        global[..]
    };
}
fn main() { let view = bad(true); print(view[0]); }
