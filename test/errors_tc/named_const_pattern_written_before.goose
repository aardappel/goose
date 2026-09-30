// error: the match pattern relies on K keeping its initializer's value, but a writable reference bound to K at
// error: named_const_pattern_written_before.goose:10 may change it
// §8.1, §11.1, §4.4: the arm matches 3 as K's initializer says, so after
// bump, K would not match itself.

fn bump(r: i64&) { r += 10; }
let K = 3;

fn main() {
    bump(K);
    match K { K => print("K"), _ => print("not K") }
}
