// error: match patterns 1..5 and 3 overlap
// Literals that overlap in one arm cannot be aliases, as named constants
// can (§8.1), so one of them adds nothing.
fn main() {
    var x = 4;
    print(match x { 1..5, 3 => 1, _ => 0 });
}
