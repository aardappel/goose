// error: match pattern LO is listed twice
// One name listed twice in an arm is always one value, so the second adds
// nothing (§8.1); only differently named constants may overlap.
let LO = 20;

fn main() {
    var x = 4;
    print(match x { LO, 4, LO => 1, _ => 0 });
}
