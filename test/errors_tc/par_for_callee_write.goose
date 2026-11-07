// error: cannot write through this reference: non-writable provenance (§9.5)
// R1 (§4.3): a variable the body sees from outside it reaches a function the
// body calls read-only, as the writability rule judges it there.
fn set(x: i64&) { x = 7; }

fn main() {
    var y = 1;
    par for k in 4 { set(y); }
    print(y);
}
