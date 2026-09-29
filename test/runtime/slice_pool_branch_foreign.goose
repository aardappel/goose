// runtime-debug: through a pool reference chosen between two slice pools, a
// run of either may be handed back, so it is checked when the call runs,
// against the pool the reference was given.
fn main() {
    reusable[] var s1: i64[>..] = [];
    reusable[] var s2: i64[>..] = [];
    let first = s1.len == 0;
    let r: i64[>..]& = if first { s1 } else { s2 };
    r.free_slice(s1.alloc_slice(2));        // checked, and r's pool's
    print(s1.len, " ", r.alloc_slice(2).len, " ", s1.len);
    r.free_slice(s2.alloc_slice(3));        // checked, and the other pool's
    print("unreachable");
}
