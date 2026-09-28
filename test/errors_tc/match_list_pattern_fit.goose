// error: match pattern does not fit the scrutinee type u8
// Every value and range of a pattern list must fit the scrutinee's type
// (§8.1), not only the first.
fn main() {
    var n: u8 = 5;
    print(match n { 0, 10..20, 200..300 => 1, _ => 0 });
}
