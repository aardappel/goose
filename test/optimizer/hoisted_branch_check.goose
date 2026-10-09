// Both arms of an if starting with the same bounds check (an element of the
// same array at the same index variable) share one check ahead of the
// branch, after the condition. It fails exactly where either arm's would,
// with the location of the arm the condition selects: here the else arm's,
// on the last merge step, which writes one past the output's end.
import std;

struct Out { vals: i64[:] }

// A merge written the plain way: each arm stores into dst[k] itself.
fn merge(a: i64[:], b: i64[:], dst: i64[:]) {
    var i = 0;
    var j = 0;
    var k = 0;
    while i < a.len && j < b.len {
        if a[i] <= b[j] {
            dst[k] = a[i];
            i++;
        } else {
            dst[k] = b[j];
            j++;
        }
        k++;
    }
    while i < a.len { dst[k] = a[i]; i++; k++; }
    while j < b.len { dst[k] = b[j]; j++; k++; }
}

// The same through a field path and with a compound assignment.
fn tally(o: Out, xs: i64[:], at: i64) {
    for x in xs {
        if x % 2 == 0 {
            o.vals[at] += x;
        } else {
            o.vals[at] -= x;
        }
    }
}

fn main() {
    let a: i64[] = [1, 4, 6, 9];
    let b: i64[] = [2, 3, 7, 8, 10];
    var dst: i64[9] = [0; 9];
    merge(a[..], b[..], dst[..]);
    print(dst);
    var acc: i64[2] = [0, 0];
    tally(Out { acc[..] }, b[..], 1);
    print(acc);
    // The output is one short: the second step takes c's head, in the else
    // arm, and has no slot to put it in.
    let c: i64[] = [1, 2];
    let d: i64[] = [3];
    var short: i64[1] = [0];
    merge(d[..], c[..], short[..]);
    print(short);
}
