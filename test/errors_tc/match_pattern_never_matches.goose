// error: match pattern 3..8 never matches: earlier arms match all of its values
// A pattern that earlier arms cover between them could never select its arm
// (§8.1), though no one of them covers it alone.
fn main() {
    var x = 4;
    print(match x { 0..5 => 1, 5..10 => 2, 3..8 => 3, _ => 0 });
}
