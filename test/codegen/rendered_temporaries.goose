// runtime-debug
// A value that is no storage -- a literal, an operator's or a cast's result,
// a `.len` or `.cap`, a payload-less variant, default<T>(), a part of any of
// those -- is rendered from a temporary of the printing statement (§3.7). A
// format overload taking it by reference reads it there, the same before and
// after calls of its own.
struct P { a: i32, b: i32 }
struct Buf { data: i64[..8] }
enum Color { Red, Green, Blue }
enum Shape { Dot, Box { p: P, n: i64 } }
enum Msg { Quit, Say { p: P, text: u8[] } }

fn clobber(a: i64) -> i64 {
    var t: i64[16] = [a; 16];
    t[3] = a + 1;
    return t[3] + t[7];
}
fn format(out: u8[>..]&, v: i64&) {
    let first = v;
    let c = clobber(first + 1000);
    format(out, "<", first as i32, if v == first && c == 2 * first + 2001 { "" } else { "!" }, ">");
}
fn format(out: u8[>..]&, v: bool&) { out.append(if v { "T" } else { "F" }); }
fn format(out: u8[>..]&, v: f64&) { format(out, "f", (v * 10.0) as i32); }
fn format(out: u8[>..]&, v: Color&) { format(out, "c", v == Color.Blue); }
fn format(out: u8[>..]&, v: P&) { format(out, "P", v.a, "/", v.b); }

var small: i32 = 3;

fn show(x: i64, n: i32, c: bool, f: f64) {
    print(7);
    print(x + 1, " ", -x, " ", x * x - 1);
    print(n as i64, " ", small as i64);
    var xs: i64[>..] = [x, x];
    var buf = Buf { data: [x] };
    print(xs.len, " ", buf.data.cap, " ", buf.data.len);
    print(default<i64>());
    print(x > 3, !c, " ", c && x == 5);
    print(f * 2.0, " ", 2.5);
    print(Color.Blue, " ", Color.Red);
    print(Shape.Dot);
    print(Shape.Box { p: P { a: n, b: 2 }, n: x + 2 });
    print(Msg.Quit, " ", Msg.Say { p: P { a: n + 1, b: 4 }, text: "hi" });
    print([x - 2, x + 2]);
    let s = str(x + 3, " ", n as i64 + 4);
    print(s);
    var b: u8[>..] = [];
    format(b, x + 5, " ", f + 0.5);
    print(b);
}

fn main() {
    small = 9;
    show(5, 3, true, 1.5);
}
