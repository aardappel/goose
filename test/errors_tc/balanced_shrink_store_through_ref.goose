// error: storing a reference into scratch, which holds a grow-shrink array: such a reference lives in a variable, is passed down or returned, and is never stored
// A balanced call frees nothing a view taken before it points into, which
// rests on no callee leaving a newer view where the caller can read it
// (§5.2): writing a slice past the mark into the caller's slice variable
// through a reference is a store, and a view of a grow-shrink array is never
// stored.
fn helper(scratch: i64[>..<]&, into: i64[:]&) {
    let mark = scratch.len;
    scratch.push(5);
    into = scratch[mark..];
    scratch.resize(mark);
}
fn main() {
    var scratch: i64[>..<] = [1];
    var view = scratch[..];
    helper(scratch, &view);
    print(view[0]);
}
