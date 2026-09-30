// error: LIMIT is bound to a writable reference here, through which it may change, while the match pattern at
// error: named_const_pattern_written.goose:8 relies on it keeping its initializer's value
// §8.1, §11.1, §4.4: the arm matches -5..0 as LIMIT's initializer says,
// whatever bump makes of LIMIT.

fn bump(r: i64&) { r += 10; }
let LIMIT = 5;
fn sign(x: i64) -> i64 { match x { -LIMIT..0 => -1, 0 => 0, _ => 1 } }

fn main() {
    print(sign(-3));
    bump(LIMIT);
    print(sign(-12), " ", -LIMIT);
}
