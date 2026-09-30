// runtime-debug: a function value's call checked as another array or slice
// type than its body's value, which the body builds as its own type: an array
// where a slice is expected (§3.10), and any array or slice constructing a
// static-capacity limited, variable, resizable or runtime-capacity limited
// array (§4.2). The call's value is a copy of the body's (§4.1), so a slice
// of it does not see a write to the variable the body names. The bodies give
// fixed, limited, variable and resizable arrays, slices and references to
// arrays, of bytes, integers, structs and variable-size elements, and another
// function value's call; the calls stand as arguments, results, bindings,
// fields and an `if`'s value, in callees inlined at -O2 or not, one of them
// into callers wanting another array type than its result.
struct Pt { x: i32, y: i32 }
struct Rec { tag: u8[], s: u8[..8], after: i64 }
struct Fix { n: i64, s: u8[..8] }
struct View { tag: u8[], s: const u8[:] }
struct Named { name: u8[], n: i64 }

var ga: u8[4] = [1, 2, 3, 4];
var gp: Pt[2] = [Pt { x: 1, y: 2 }, Pt { x: 3, y: 4 }];

fn mk() -> u8[:] { "hey" }
fn mkv() -> u8[] { return "var"; }
fn mkr() -> u8[>..] {
    var r: u8[>..] = [];
    r.push('r');
    r.push('z');
    return r;
}
fn mkl() -> u8[..8] { return "lim"; }
fn mkc() -> u8[..] {
    var l: u8[..] = [..6];
    l.push('c');
    l.push('p');
    return l;
}
fn get_a() -> u8[4]& { ga }
fn ints() -> i32[3] { return [10, 20, 30]; }
fn words() -> u8[][>..] {
    var a: u8[][>..] = [];
    a.push("goose");
    a.push("gander");
    return a;
}
fn names() -> Named[>..] {
    var a: Named[>..] = [];
    a.push(Named { name: "x", n: 1 });
    a.push(Named { name: "yz", n: 2 });
    return a;
}

// A slice wanted.
fn count(s: u8[:]) -> i64 { return s.len * 1000 + s[0]; }
fn pass<F>() -> i64 { return count(F()); }
fn seen(s: u8[:]) -> i64 {
    ga[0] = 9;
    return s[0];
}
fn copied<F>() -> i64 { return seen(F()); }
fn total(s: i32[:]) -> i64 {
    var t = 0;
    for x in s { t += x; }
    return t;
}
fn summed<F>() -> i64 { return total(F()); }
fn second(s: Pt[:]) -> i64 { return s.len * 100 + s[1].y; }
fn points<F>() -> i64 { return second(F()); }
fn lengths(s: u8[][:]) -> i64 {
    var t = 0;
    for w in s { t = t * 10 + w.len; }
    return t;
}
fn worded<F>() -> i64 { return lengths(F()); }
fn weights(s: Named[:]) -> i64 {
    var t = 0;
    for x in s { t += x.name.len * 10 + x.n; }
    return t;
}
fn weighed<F>() -> i64 { return weights(F()); }
fn given<F>() -> u8[4] { return F(); }
fn tagged(v: View) -> i64 { return v.s.len * 1000 + v.tag.len * 100 + v.s[0]; }
fn viewed<F>() -> i64 { return tagged(View { tag: "tt", s: F() }); }

// A static-capacity limited array wanted.
fn lim<F>() -> u8[..8] { return F(); }
fn limarg(x: u8[..8]) -> i64 { return x.len * 1000 + x[0]; }
fn limpass<F>() -> i64 { return limarg(F()); }
fn chosen<F>(first: bool) -> i64 {
    let x: u8[..8] = if first { F() } else { mkl() };
    return x.len * 1000 + x[0];
}
fn limlet<F>() -> i64 {
    let x: u8[..8] = F();
    return x.len * 1000 + x[x.len - 1];
}
fn limrec<F>() -> Rec { return Rec { tag: "t", s: F(), after: 7 }; }
fn limfix<F>() -> Fix { return Fix { n: 5, s: F() }; }

// A variable, resizable or runtime-capacity limited array wanted.
fn vararr<F>() -> u8[] { return F(); }
fn varg(x: u8[]) -> i64 { return x.len * 1000 + x[0]; }
fn varpass<F>() -> i64 { return varg(F()); }
fn varlet<F>() -> i64 {
    let x: u8[u16] = F();
    return x.len * 1000 + x[x.len - 1];
}
fn grown<F>() -> u8[>..] {
    var r: u8[>..] = F();
    r.push('!');
    return r;
}
fn rzarg(x: u8[>..]) -> i64 { return x.len * 1000 + x[0]; }
fn rzpass<F>() -> i64 { return rzarg(F()); }
fn capped<F>() -> i64 {
    let x: u8[..] = F();
    return x.len * 1000 + x[0];
}

// Inlined at -O2 into callers wanting other array types than its result,
// which the call in its `return` was checked as.
fn wide<F>() -> u8[u16] { return F(); }

fn main() {
    // Slices, of every kind of body.
    print(pass() { mk() }, " ", pass() { [7 as u8, 8] }, " ", pass() { ga },
          " ", pass() { get_a() }, " ", pass() { mkl() }, " ", pass() { mkv() },
          " ", pass() { mkr() }, " ", pass() { mkc() });
                                        // 3104 2007 4001 4001 3108 3118 2114 2099
    print(summed() { ints() }, " ", summed() { [1 as i32, 2, 3] }, " ", points() { gp },
          " ", viewed() { ga }, " ", viewed() { [5 as u8, 6] }, " ", viewed() { mkv() });
                                        // 60 6 204 4201 2205 3318
    print(worded() { words() }, " ", weighed() { names() }, " ", pass() { given() { ga } },
          " ", vararr() { given() { get_a() } }.len);  // 56 33 4001 4
    // The slice is of a copy: the callee's write to ga does not reach it.
    print(copied() { ga }, " ", ga[0]);         // 1 9
    ga[0] = 1;
    print(copied() { get_a() }, " ", ga[0]);    // 1 9
    ga[0] = 1;

    // Static-capacity limited arrays.
    print(lim() { mk() }, " ", lim() { ga }.len, " ", lim() { mkv() }, " ",
          lim() { mkr() }, " ", lim() { mkc() });       // hey 4 var rz cp
    print(limpass() { mk() }, " ", limpass() { get_a() }, " ", limpass() { mkv() },
          " ", limlet() { mkr() }, " ", limlet() { [5 as u8, 6] });
                                        // 3104 4001 3118 2122 2006
    print(chosen(true) { ga }, " ", chosen(false) { ga }, " ", chosen(true) { mkv() });
                                        // 4001 3108 3118
    let r = limrec() { mkv() };
    let q = limrec() { mk() };
    let w = limrec() { mkr() };
    let f = limfix() { mk() };
    print(r.tag, " ", r.s, " ", q.s, " ", w.s, " ", r.after, " ", f.n, " ", f.s);
                                        // t var hey rz 7 5 hey

    // Variable, resizable and runtime-capacity limited arrays.
    print(vararr() { mk() }, " ", vararr() { mkl() }, " ", vararr() { get_a() }.len,
          " ", vararr() { mkc() });             // hey lim 4 cp
    print(varpass() { mk() }, " ", varpass() { [9 as u8] }, " ", varlet() { mkl() },
          " ", varlet() { mkr() });             // 3104 1009 3109 2122
    print(grown() { mk() }, " ", grown() { mkl() }, " ", grown() { mkc() }, " ",
          rzpass() { get_a() }, " ", rzpass() { mkv() });
                                        // hey! lim! cp! 4001 3118
    print(capped() { mk() }, " ", capped() { mkv() }, " ", capped() { mkr() });
                                        // 3104 3118 2114

    // Through a callee returning another array type than the caller wants.
    print(varg(wide() { mk() }), " ", varg(wide() { mkl() }), " ",
          rzarg(wide() { ga }));                // 3104 3108 4001
}
