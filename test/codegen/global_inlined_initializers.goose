// runtime-debug: globals are declared like locals (§11.1), so an
// initializer's value is read as the global's type asks, also where the
// call it makes is inlined (-O2) and the callee's `return` delivers the
// call's own result: a reference result read as its pointee (§3.8) -- a
// slice, a struct, a scalar, a varint field -- and a slice copied into a
// static-capacity limited array (§4.2); called directly, chosen by an `if`
// or a `block`, or given by a function value's call.
struct P { x: i64, y: i64 }
struct V { n: varint, w: i32 }

var word: u8[>..] = [119, 111, 114, 100];
var gw: u8[:] = word[..];
var gp = P { 3, 4 };
var gi: i64 = 5;
var gj: i64 = 9;
var gv = V { 300, 6 };

fn get_w() -> u8[:]& { gw }
fn get_p() -> P& { gp }
fn get_i() -> i64& { gi }
fn get_j() -> i64& { gj }
fn get_n() -> varint& { gv.n }
fn part() -> u8[:] { return gw[1..]; }
fn given<F>() -> u8[:] { return F(); }
fn plus<F>() -> i64 { return F() + 1; }

var w2: u8[:] = get_w();
var p2: P = get_p();
var i2: i64 = get_i();
var n2: i64 = get_n();
var l2: u8[..8] = part();
var c2: i64 = if gi > gj { get_i() } else { get_j() };
var b2: i64 = block { if gi < gj { break get_i(); } break get_j(); };
var w3: u8[:] = given() { get_w() };
var i3: i64 = plus() { get_i() };

fn main() {
    print(w2, " ", p2, " ", i2, " ", n2, " ", l2);     // word P { 3, 4 } 5 300 ord
    print(c2, " ", b2, " ", w3, " ", i3);               // 9 5 word 6
}
