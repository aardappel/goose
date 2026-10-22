// A global integer keeps `>= 0` only where no write anywhere can make it
// negative, and a global's initializer is somewhere too: one written in an
// initializer, one written by a function inlined into one, and one written
// through a reference an initializer formed, held in a variable or in a
// struct, all lose it.
import std;

var g1 = 0;
var h1 = { g1 = -1; 5 };

var g2 = 0;
fn setup() -> i64 { g2 = -2; return 6; }
var h2 = setup();

var g3 = 0;
var r3: i64& = g3;
fn spoil3() { r3 = -3; }

struct Holder { r: i64& }
var g4 = 0;
var hold = Holder { r: g4 };
fn spoil4() { hold.r = -4; }

fn probe1(xs: i64[:]) -> i64 {
    if g1 < xs.len { return xs[g1]; }           // bce:keep (h1's initializer stored -1)
    return 0;
}

fn probe2(xs: i64[:]) -> i64 {
    if g2 < xs.len { return xs[g2]; }           // bce:keep (setup, run by h2's initializer, stored -2)
    return 0;
}

fn probe3(xs: i64[:]) -> i64 {
    spoil3();
    if g3 < xs.len { return xs[g3]; }           // bce:keep (spoil3 stored -3 through r3)
    return 0;
}

fn probe4(xs: i64[:]) -> i64 {
    spoil4();
    if g4 < xs.len { return xs[g4]; }           // bce:keep (spoil4 stored -4 through hold.r)
    return 0;
}

fn main() {
    var a: i64[>..] = [1, 2, 3];
    print(h1, " ", h2);
    print(probe4(a[..]));
    print(probe1(a[..]), " ", probe2(a[..]), " ", probe3(a[..]));
}
