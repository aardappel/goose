// runtime-debug: a guarded base case must test a reference after prefix writes.
import std;

var visits = 0;

// The recursive arguments alias, so changing q also changes the value p reads.
recursive fn descend(p: i64&, q: i64&, n: i64) {
    q += 1;
    if p > 0 && n > 0 {
        descend(q, q, n - 1);
    }
    visits += 1;
}

fn main() {
    var values: i64[2] = [1, -1];
    descend(values[0], values[1], 2);
    print(values[1], " ", visits);
}
