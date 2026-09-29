// error: .free_slice needs a slice of the pool it is called on
// error: this one is rooted at s3
// A pool reference chosen between two slice pools may be either, but not a
// third pool this function owns: a run of that one cannot be the chosen
// pool's (§5.4).
fn main() {
    reusable[] var s1: i64[>..] = [];
    reusable[] var s2: i64[>..] = [];
    reusable[] var s3: i64[>..] = [];
    let c = s1.len == 0;
    let r: i64[>..]& = if c { s1 } else { s2 };
    r.free_slice(s3.alloc_slice(1));
}
