// runtime-debug: a call's array result passed straight to a slice parameter
// (§3.10) is a temporary of the caller's statement, sliced whole -- also when
// both calls are inlined, where the result has to be built outside the body
// producing it to outlive that body and the slice's uses. Each consumer grows
// a scratch array before it reads its slice, over the storage an argument
// released too early would have left behind. Resizable results of u8, i32,
// struct and variable-size elements, nested, in loops, in every shape a
// callee can return them in and from recursive calls; then fixed, limited
// and variable arrays and references to arrays.
struct Pt { x: i32, y: i32 }

fn scratch() -> u8[>..] {
    var s: u8[>..] = [];
    for i in 64 { s.push(0xEE); }
    return s;
}

fn bytes(n: i64) -> u8[>..] {
    var a: u8[>..] = [];
    for i in n { a.push((i + 1) as u8); }
    return a;
}
fn ints(n: i64) -> i32[>..] {
    var a: i32[>..] = [];
    for i in n { a.push((i * 10 + 1) as i32); }
    return a;
}
fn points(n: i64) -> Pt[>..] {
    var a: Pt[>..] = [];
    for i in n { a.push(Pt { x: i as i32, y: (i * i) as i32 }); }
    return a;
}
fn words() -> u8[][>..] {
    var a: u8[][>..] = [];
    a.push("goose");
    a.push("gander");
    return a;
}
fn literal() -> u8[>..] { return [7, 8, 9]; }
fn branch(n: i64) -> u8[>..] { if n > 1 { [1, 2] } else { [3] } }
fn early(n: i64) -> u8[>..] {
    var a: u8[>..] = [];
    if n == 0 { return a; }
    for i in n { a.push(i as u8); }
    return a;
}
fn doubled(data: const u8[:]) -> u8[>..] {
    var out: u8[>..] = [];
    for x in data { out.push(x); out.push(x); }
    return out;
}
fn fixed(x: u8) -> u8[3] {
    var a: u8[3] = [1, 2, 3];
    a[0] = x;
    return a;
}
fn limited(n: i64) -> u8[..8] {
    var a: u8[..8] = [];
    for i in n { a.push((i + 1) as u8); }
    return a;
}
fn variable(n: i64) -> u8[] {
    var a: u8[>..] = [];
    for i in n { a.push((i + 1) as u8); }
    return a;
}
fn pick_fixed(a: u8[3]&) -> u8[3]& { a }
fn pick_grown(a: u8[>..]&) -> u8[>..]& { a }

// The elements' sum and the count, read after the scratch array exists.
fn sum(data: const u8[:]) -> i64 {
    let s = scratch();
    var t = 0;
    for x in data { t += x as i64; }
    t * 100 + data.len
}
fn sum_ints(data: const i32[:]) -> i64 {
    let s = scratch();
    var t = 0;
    for x in data { t += x as i64; }
    t * 100 + data.len
}
fn sum_points(ps: const Pt[:]) -> i64 {
    let s = scratch();
    var t = 0;
    for p in ps { t += (p.x * 10 + p.y) as i64; }
    t * 100 + ps.len
}
fn letters(ws: const u8[][:]) -> i64 {
    let s = scratch();
    var t = 0;
    for w in ws { t = t * 10 + w.len; }
    t
}
fn pair(a: const u8[:], b: const u8[:]) -> i64 {
    let s = scratch();
    sum(a) * 10000 + sum(b)
}
fn ident(data: const u8[:]) -> const u8[:] { data }

// Arguments bound inside a body that is itself inlined into a loop.
fn shapes(n: i64) -> i64 {
    var t = 0;
    for i in n { t += sum(branch(i)) + sum(early(i)); }
    t
}

// Self-calls with a base case the optimizer could copy to the call site.
recursive fn depth_fixed(n: i64) -> u8[3] {
    if n == 0 { return [1, 2, 3]; }
    var r: u8[3] = [0, 0, 0];
    r[0] = (sum(depth_fixed(n - 1)) % 251) as u8;
    return r;
}
recursive fn depth_grown(n: i64) -> u8[>..] {
    if n == 0 { return [4, 5]; }
    return [(sum(depth_grown(n - 1)) % 251) as u8];
}

fn main() {
    print(sum(bytes(3)));                   // 603
    print(sum_ints(ints(4)));               // 6404
    print(sum_points(points(3)));           // 3503
    print(letters(words()));                // 56
    print(sum(literal()));                  // 2403
    print(sum(doubled(bytes(3))));          // 1206
    print(sum(doubled(doubled(bytes(2))))); // 1208
    print(pair(bytes(2), bytes(3)));        // 3020603
    print(ident(bytes(5)).len);             // 5
    print(ident(bytes(4))[3]);              // 4

    var total = 0;
    for i in 4 { total += pair(bytes(i), doubled(bytes(i))); }
    print(total);                           // 10062012
    var j = 0;
    var ti = 0;
    while j < 3 {
        ti += sum_ints(ints(j + 1));
        j++;
    }
    print(ti);                              // 4606
    print(shapes(4));                       // 1612
    print(sum(depth_fixed(3)));             // 2803
    print(sum(depth_grown(3)));             // 16501

    print(sum(fixed(10)));                  // 1503
    print(sum(limited(3)));                 // 603
    print(sum(variable(4)));                // 1004
    var f: u8[3] = [4, 5, 6];
    var g: u8[>..] = [7, 8];
    print(sum(pick_fixed(f)));              // 1503
    print(sum(pick_grown(g)));              // 1502
}
