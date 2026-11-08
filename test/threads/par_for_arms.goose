// goose-threads
// Par fors standing as a match's arms, braced or not, and as an if's
// branches inside one, each outlined and forked as one in a block is
// (CodeGen::OutlineParLoops).
fn fill(which: i64, a: i64[:]) {
    match which {
        0 => par for k in a.len { a[k] = k; },
        1 => { par for k in a.len { a[k] = 10 * k; } },
        _ => if which > 5 {
            par for k in a.len { a[k] = 100 * k; }
        } else {
            par for k in a.len { a[k] = 1000 * k; }
        },
    }
}

fn main() {
    var a: i64[4] = [0, 0, 0, 0];
    for w in [0, 1, 2, 7] {
        fill(w, a);
        print(a);
    }
}
