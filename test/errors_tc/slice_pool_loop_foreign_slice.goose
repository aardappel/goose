// error: .realloc_slice needs a slice of the pool it is called on
// error: this one is rooted at s3
// A pool reference a loop rebinds may be each pool it is bound to there, which
// the loop's later passes find: judged with all of them, a run of a third pool
// is still not its own (§5.4).
fn main() {
    reusable[] var s1: i64[>..] = [];
    reusable[] var s2: i64[>..] = [];
    reusable[] var s3: i64[>..] = [];
    var q: i64[>..]& = s1;
    for i in 2 {
        q.realloc_slice(s3.alloc_slice(1), 2);
        q .= s2;
    }
}
