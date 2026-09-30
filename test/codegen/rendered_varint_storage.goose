// runtime-debug: an i64 read out of varint storage -- a varint field or
// element, reached directly, through a reference to its struct, through a
// reference to the varint itself, by a match binder or a `for` binder --
// lies in no i64 storage (§3.6), so a user format overload taking an i64 by
// reference is handed a temporary holding the value it decodes to,
// read-only (§3.7), by print, str and format alike. A varint that is a part
// of a printed struct or array is a varint, which no i64 overload renders.
struct V { n: varint, w: i32 }
struct H { r: const varint& }
struct M { n: varint, x: i64 }
enum Shape { Pts { q: varint, k: i64 }, Nothing }
var gv = V { n: 5, w: 1 };

recursive fn digits(out: u8[>..]&, v: i64) {
    if v < 0 {
        out.push('-');
        digits(out, -v);
        return;
    }
    if v >= 10 { digits(out, v / 10); }
    out.push((v % 10 + 48) as u8);
}

// Renders an i64 in angle brackets.
fn format(out: u8[>..]&, v: i64&) {
    out.append("<");
    digits(out, v);
    out.append(">");
}

fn viaref(v: V&) { print(v.n); }
fn viaconst(v: const V&) { print(v.n, " ", v.w); }
fn generic<T>(x: const T&) { print(x); }

fn main() {
    var lv = V { n: 300, w: 2 };
    print(gv.n, " ", lv.n);                  // <5> <300>
    viaref(lv);                              // <300>
    viaconst(gv);                            // <5> 1
    let r: const varint& = lv.n;
    let h = H { r: gv.n };
    print(r, " ", h.r);                      // <300> <5>
    let o: (const varint&)? = lv.n;
    if o != null { print(o); }               // <300>
    generic(gv.n);                           // <5>
    var sh: Shape.. = Shape.Pts { q: -77, k: 3 };
    match sh {
        Pts p   => print(p.q, " ", p.k),     // <-77> <3>
        Nothing => print("none"),
    }
    let a: varint[] = [1, 300, -5];
    for x in a { print(x); }                 // <1> <300> <-5>
    let m = M { n: 7, x: 8 };
    print(a, " ", m, " ", gv, " ", h);       // [1, 300, -5] M { 7, <8> } V { 5, 1 } H { 5 }
    var c = lv.w > 1;
    print(if c { gv.n } else { lv.n });      // <5>
    let s = str(lv.n, " ", gv.n);
    print(s);                                // <300> <5>
    var b: u8[>..] = [];
    format(b, lv.n, gv.n);
    print(b);                                // <300><5>
    var lim: u8[..16] = [];
    format(lim, lv.n);
    print(lim);                              // <300>
}
