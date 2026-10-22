// A dispatched call's cases (§8.2) empty the parts of the holder they are
// handed before they shrink the pool those parts' references pointed into
// (§5.1): each case's shrink is judged with what its body emptied, in the
// argument the call passes, as a call of one function's would be, so s's
// references are no longer live into the pool.
enum K { A, B }
struct S { pool: i64[>..]&, refs: (i64&)[>..] }
fn build(k: K.A, t: S&, i: i64) { t.refs = []; t.pool = []; t.refs.push(t.pool.push(i)); }
fn build(k: K.B, t: S&, i: i64) { t.refs = []; t.pool = []; t.refs.push(t.pool.push(i * 2)); }
fn main() {
    var pool: i64[>..] = [];
    var s = S { pool: pool, refs: [] };
    var k: K = K.A {};
    for i in 3 {
        build(k, s, i + 1);
        k = if i == 0 { K.B {} } else { K.A {} };
    }
    print(s.refs.len, " ", pool.len, " ", s.refs[0]);
}
