// error: .free_slice needs a slice of the pool it is called on
// error: this one is rooted at s3
// The same in an inner loop, while the outer one rebinds the reference: the
// outer loop's passes find the pools it may be, and the verdict waits for them.
fn main() {
    reusable[] var s1: i64[>..] = [];
    reusable[] var s2: i64[>..] = [];
    reusable[] var s3: i64[>..] = [];
    var q: i64[>..]& = s1;
    for i in 2 {
        for j in 2 { q.free_slice(s3.alloc_slice(1)); }
        q .= s2;
    }
}
