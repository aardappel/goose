// runtime-debug: a guarded base case must test slices after prefix writes.
import std;

var visits = 0;

// The recursive arguments alias, so changing q changes the elements p compares.
recursive fn descend(p: u8[:], q: u8[:], wanted: u8[:], n: i64) {
    q[0] += 1;
    if p == wanted && n > 0 {
        descend(q, q, wanted, n - 1);
    }
    visits += 1;
}

fn main() {
    var a: u8[1] = [2];
    var b: u8[1] = [0];
    var wanted: u8[1] = [2];

    descend(a, b, wanted, 2);
    print(b[0], " ", visits);
}
