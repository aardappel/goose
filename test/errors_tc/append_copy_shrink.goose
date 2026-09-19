// error: cannot pop nums while refs is still used: a reference into it was stored there
// The references an append copies are stores on record for the shrink rules
// (§5.1): refs holds one into nums after xs is gone.
fn main() {
    var nums: i64[>..] = [1, 2, 3];
    var refs: (i64&)[>..] = [];
    {
        let xs: (i64&)[1] = [nums[2]];
        refs.append(xs);
    }
    nums.pop();
    print(refs[0]);
}
