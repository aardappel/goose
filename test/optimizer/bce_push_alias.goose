// A counted loop states the length its pushes give an array only where no
// other statement of the body can change that length, a push onto what may
// be the same array included: `a` grows by two per iteration, through itself
// and through `r`, while `b` grows by one.
import std;

fn probe(n: i64) -> i64 {
    if n < 0 { return 0; }
    var a: i64[>..] = [];
    var b: i64[>..] = [];
    let r: i64[>..]& = a;
    for _ in n {
        a.push(1);
        r.push(2);
        b.push(3);
    }
    var t = 0;
    for i in n { t += b[i]; }        // bce:elide
    print(t);
    for i in a.len { t += b[i]; }    // bce:keep
    return t;
}

fn main() {
    var ks: i64[>..] = [3];
    print(probe(ks[0]));
}
