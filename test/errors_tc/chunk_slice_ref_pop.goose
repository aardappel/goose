// error: cannot call pop_all, which shrinks q1: the elements of r1, which the statement reads after it, may be in q1
// error: cannot call pop_all, which shrinks q2: the elements of r2, which the statement reads after it, may be in q2
// error: cannot call pop_all, which shrinks q3: the elements of r3, which the statement reads after it, may be in q3
// error: cannot call pop_all, which shrinks q4: the elements of r4, which the statement reads after it, may be in q4
// The grow-shrink counterpart of chunk_slice_ref_shrink.goose: a chunk's
// receiver reached through a reference to a slice of a grow-shrink array is
// held as the elements the slice views while the arguments after it run, in
// either spelling of either builtin, so none of them may shrink the array
// (§5.2).
fn pop_all(q: i64[>..<]&) -> i64 {
    q.clear();
    q.push(9);
    return 0;
}

fn chunk_member() {
    var q1: i64[>..<] = [1, 2, 3, 4];
    let s1 = q1[..];
    let r1: i64[:]& = s1;
    print(r1.chunk(0, pop_all(q1) + 1));
}

fn chunk_free() {
    var q2: i64[>..<] = [1, 2, 3, 4];
    let s2 = q2[..];
    let r2: i64[:]& = s2;
    print(chunk(r2, 0, pop_all(q2) + 1));
}

fn chunk_at_member() {
    var q3: i64[>..<] = [1, 2, 3, 4];
    let bounds: i64[2] = [0, 4];
    let s3 = q3[..];
    let r3: i64[:]& = s3;
    print(r3.chunk_at(bounds, pop_all(q3)));
}

fn chunk_at_free() {
    var q4: i64[>..<] = [1, 2, 3, 4];
    let bounds: i64[2] = [0, 4];
    let s4 = q4[..];
    let r4: i64[:]& = s4;
    print(chunk_at(r4, bounds, pop_all(q4)));
}

fn main() {
    chunk_member();
    chunk_free();
    chunk_at_member();
    chunk_at_free();
}
