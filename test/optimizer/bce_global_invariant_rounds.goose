// The writes of every global integer kept `>= 0` are checked with all of
// them assumed at once, so one dropped fails the writes it was assumed for:
// b is nonnegative only as long as the a it copies is.
import std;

var a = 0;
var b = 0;
fn spoil() { a = -1; }
fn copy() { b = a; }

fn probe(xs: i64[:]) -> i64 {
    if b < xs.len { return xs[b]; }             // bce:keep (b copied a's -1)
    return 0;
}

fn main() {
    var arr: i64[>..] = [1, 2, 3];
    copy();
    print(probe(arr[..]));
    spoil();
    copy();
    print(probe(arr[..]));
}
