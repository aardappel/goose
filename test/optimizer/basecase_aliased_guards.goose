// runtime-debug: a guarded base case must test references and slices after
// prefix writes, which can reach them through an aliasing argument.
import std;

// A guarded base case must test a reference after prefix writes.

var ref_visits = 0;

// The recursive arguments alias, so changing q also changes the value p reads.
recursive fn descend_ref(p: i64&, q: i64&, n: i64) {
    q += 1;
    if p > 0 && n > 0 {
        descend_ref(q, q, n - 1);
    }
    ref_visits += 1;
}

// A guarded base case must test slices after prefix writes.

var slice_visits = 0;

// The recursive arguments alias, so changing q changes the elements p compares.
recursive fn descend_slice(p: u8[:], q: u8[:], wanted: u8[:], n: i64) {
    q[0] += 1;
    if p == wanted && n > 0 {
        descend_slice(q, q, wanted, n - 1);
    }
    slice_visits += 1;
}

fn main() {
    var values: i64[2] = [1, -1];
    descend_ref(values[0], values[1], 2);
    print(values[1], " ", ref_visits);

    var a: u8[1] = [2];
    var b: u8[1] = [0];
    var wanted: u8[1] = [2];

    descend_slice(a, b, wanted, 2);
    print(b[0], " ", slice_visits);
}
