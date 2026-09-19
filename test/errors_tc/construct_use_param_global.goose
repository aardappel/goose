// error: cannot use v in the value assigned to SAVED: v may refer to SAVED
// A caller may pass SAVED itself: the parameter's array is not provably a
// different one, and such a use is an error, as a growth is (§4.2, §4.4).
var SAVED: u8[>..] = [];
fn save(v: u8[>..]&) { SAVED = copy(v); }
fn main() {
    var w: u8[>..] = [];
    w.append("abc");
    save(w);
    print(SAVED);
}
