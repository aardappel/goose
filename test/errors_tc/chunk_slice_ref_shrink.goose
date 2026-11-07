// error: cannot call shrink, which shrinks a1: the elements of r1, which the statement reads after it, may be in a1
// error: cannot call shrink, which shrinks a2: the elements of r2, which the statement reads after it, may be in a2
// error: cannot call shrink, which shrinks a3: the elements of r3, which the statement reads after it, may be in a3
// error: cannot call shrink, which shrinks a4: the elements of r4, which the statement reads after it, may be in a4
// error: cannot call shrink_g, which shrinks g: the elements of r5, which the statement reads after it, may be in g
// A chunk's receiver reached through a reference to a slice is held as the
// elements that slice views, as a slice expression's object is: chunk and
// chunk_at, in either spelling, read them before the arguments after the
// receiver run, so an argument that shrinks the grow-only array they lie in,
// through a reference or as a global, would leave the chunk past the
// array's end (§5.1).
fn shrink(a: i64[>..]&) -> i64 {
    a.clear();
    a.push(9);
    return 0;
}

var g: i64[>..] = [1, 2, 3, 4];

fn shrink_g() -> i64 {
    g.clear();
    g.push(9);
    return 0;
}

fn chunk_member() {
    var a1: i64[>..] = [1, 2, 3, 4];
    let s1 = a1[..];
    let r1: i64[:]& = s1;
    print(r1.chunk(0, shrink(a1) + 1));
}

fn chunk_free() {
    var a2: i64[>..] = [1, 2, 3, 4];
    let s2 = a2[..];
    let r2: i64[:]& = s2;
    print(chunk(r2, 0, shrink(a2) + 1));
}

fn chunk_at_member() {
    var a3: i64[>..] = [1, 2, 3, 4];
    let bounds: i64[2] = [0, 4];
    let s3 = a3[..];
    let r3: i64[:]& = s3;
    print(r3.chunk_at(bounds, shrink(a3)));
}

fn chunk_at_free() {
    var a4: i64[>..] = [1, 2, 3, 4];
    let bounds: i64[2] = [0, 4];
    let s4 = a4[..];
    let r4: i64[:]& = s4;
    print(chunk_at(r4, bounds, shrink(a4)));
}

fn chunk_global() {
    let s5 = g[..];
    let r5: i64[:]& = s5;
    print(r5.chunk(0, shrink_g() + 1));
}

fn main() {
    chunk_member();
    chunk_free();
    chunk_at_member();
    chunk_at_free();
    chunk_global();
}
