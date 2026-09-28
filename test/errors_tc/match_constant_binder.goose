// error: constant pattern GATE has no payload to bind
// In an integer match a bare name is a constant (§8.1), which has no payload
// for a binder to take.
let GATE = 7;

fn main() {
    var t = 7;
    print(match t { GATE g => g, _ => 0 });
}
