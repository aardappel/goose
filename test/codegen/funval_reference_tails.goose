// runtime-debug: a function value's call gives a copy of its body's value
// (§4.1, §7.6), and a body whose value is a call returning a reference
// gives that reference's pointee (§3.8), also where the call is inlined
// (-O2) and its `return` delivers the reference itself: slice, struct,
// fixed array and scalar pointees, a varint field's among them, read by a
// field, an operator, an index, a render, a binding, an argument and a
// return, and as what an `if` or a `match` chooses, a `loop` breaks with
// or another function value's call gives.
struct P { x: i64, y: i64 }
struct V { n: varint, w: i32 }
enum Pick { First, Second }

var text: u8[>..] = [];
var gs: u8[:] = text[..];
var gt: u8[:] = text[..];
var gp = P { 3, 4 };
var gf: i64[3] = [7, 8, 9];
var gi: i64 = 5;
var gu: u8 = 200;
var gd: f64 = 2.5;
var gb = false;
var gv = V { 300, 6 };

fn get_s() -> u8[:]& { gs }
fn get_t() -> u8[:]& { gt }
fn get_p() -> P& { gp }
fn get_f() -> i64[3]& { gf }
fn get_i() -> i64& { gi }
fn get_u() -> u8& { gu }
fn get_d() -> f64& { gd }
fn get_b() -> bool& { gb }
fn get_n() -> varint& { gv.n }
fn at(xs: i64[3]&, i: i64) -> i64& { xs[i] }

fn length<F>() -> i64 { return F().len; }
fn show<F>() { print(F()); }
fn bound<F>() -> i64 { let x = F(); return x.len * 1000 + x[1] as i64; }
fn count(s: u8[:]) -> i64 { return s.len; }
fn passed<F>() -> i64 { return count(F()); }
fn given<F>() -> u8[:] { return F(); }
fn field<F>() -> i64 { return F().y; }
fn fields<F>() -> i64 { let q = F(); return q.x * 10 + q.y; }
fn second<F>() -> i64 { return F()[1]; }
fn plus<F>() -> i64 { return F() + 1; }
fn widened<F>() -> i64 { return F() as i64 + 1; }
fn doubled<F>() -> f64 { return F() * 2.0; }
fn which<F>() -> i64 { return if F() { 1 } else { 2 }; }

fn main() {
    text.append("abcdef");
    gs = text[..];
    gt = text[2..];

    // Slices.
    print(length() { get_s() }, " ", passed() { get_t() }, " ", bound() { get_s() });
                                                        // 6 4 6098
    show() { get_t() };                                 // cdef
    print(given() { get_s() }.len, " ", given() { get_t() });   // 6 cdef

    // Structs and fixed arrays.
    print(field() { get_p() }, " ", fields() { get_p() }, " ", second() { get_f() });
                                                        // 4 34 8
    show() { get_p() };                                 // P { 3, 4 }
    show() { get_f() };                                 // [7, 8, 9]

    // Scalars.
    print(plus() { get_i() }, " ", widened() { get_u() }, " ", doubled() { get_d() },
          " ", which() { get_b() }, " ", plus() { get_n() }, " ", plus() { at(gf, 2) });
                                                        // 6 201 5 2 301 10
    show() { get_i() };                                 // 5

    // Chosen by a construct, or given by another function value.
    var pick = Pick.First;
    var first = true;
    for k in 2 {
        print(length() { if first { get_s() } else { get_t() } }, " ",
              length() { match pick { First => get_s(), Second => get_t() } }, " ",
              length() { loop { if first { break get_s(); } break get_t(); } }, " ",
              length() { given() { get_t() } });
        pick = Pick.Second;
        first = false;
    }                                                   // 6 6 6 4, 4 4 4 4
}
