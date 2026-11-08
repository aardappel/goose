// error: cannot assign hits in a par for: a global is read-only in a par for's body and in every function it calls
// R1 and R3 (§4.3): a function the body calls writes no global.
var hits = 0;

fn note() { hits += 1; }

fn main() {
    note();
    par for k in 4 { note(); }
    print(hits);
}
