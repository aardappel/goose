// error: storing a reference rooted at x, which does not outlive the destination
// The elements an array appends are copies stored in the receiver, so the
// references they hold must outlive it (§9.2), as an appended literal's do.
fn main() {
    var refs: (i64&)[>..] = [];
    {
        let x = 5;
        let xs: (i64&)[1] = [x];
        refs.append(xs);
    }
    print(refs.len);
}
