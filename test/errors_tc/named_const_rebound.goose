// error: the match pattern relies on K keeping its initializer's value, but a writable reference bound to K at
// error: named_const_rebound.goose:10 may change it
// §11.1, §3.8, §4.4: rebinding r to K makes assigning r write K.

let K = 3;
var x = 7;
var r: i64& = x;

fn main() {
    r .= K;
    r = 4;
    match 3 { K => print("three"), _ => print("other") }
}
