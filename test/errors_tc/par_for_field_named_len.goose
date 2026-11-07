// error: head is or may point into storage partitioned by this par for
// R2 (§4.4): only an array's own length is read beside the elements the
// iterations own; a field named len is an element's, here another
// iteration's.
struct Run { len: i64 }

fn main() {
    var xs: Run[>..] = [];
    for i in 100 { xs.push(Run { len: i }); }
    let head = &xs[0];
    par for &x in xs { x.len = head.len + 1; }
    print(xs[99].len);
}
