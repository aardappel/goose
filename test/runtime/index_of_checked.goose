// index_of with a message places a reference the checker cannot: one that
// came through a parameter may be an element of any array. It is checked as
// the call runs, to be aligned to an element and inside the length, and the
// program aborts with the message otherwise.
fn at(xs: i64[>..]&, r: i64&) -> i64 { xs.index_of(r, "not an element of xs") }

fn main() {
    var xs: i64[>..] = [10, 20, 30];
    var ys: i64[>..] = [1];
    print(at(xs, xs[2]), " ", at(ys, ys[0]), " ", xs.index_of(xs[1], "known to be one"));
    print(at(xs, ys[0]));
    print("not reached");
}
