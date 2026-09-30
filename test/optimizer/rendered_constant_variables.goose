// A format overload taking a variable by reference is handed the variable
// (§3.7), one initialized with a constant and never assigned included, which
// the optimizer would otherwise read as that constant wherever it is named:
// what the overload keeps points at the variable after the function that
// printed it returns, and what it writes there the variable holds, at every
// optimization level.
let K: i32 = 7;
const C: i32 = 8;
var keep: (const i32&)? = null;
fn format(out: u8[>..]&, v: const i32&) {
    keep .= v;
    out.append("k");
}

var gk: i64 = 5;
let gl: i64 = 5;
fn format(out: u8[>..]&, v: i64&) {
    v += 1;
    out.append("w");
}

// Overwrites the frame the function before it printed from.
fn clobber(x: i64) -> i64 {
    var arr: i64[64] = [x; 64];
    var s: i64 = 0;
    for i in 0..64 { s += arr[i]; }
    s
}

fn show_k() { print(K); }
fn show_c() -> u8[>..] { str(C) }
fn bump(x: i64) {
    print(x);
    print(x == 6);
}

fn main() {
    show_k();
    let z = clobber(99);
    if keep != null {
        let y: i32 = keep;
        print(y == 7, " ", z == 6336);
    }
    let s = show_c();
    let z2 = clobber(98);
    if keep != null {
        let y: i32 = keep;
        print(s, " ", y == 8, " ", z2 == 6272);
    }
    print(gk);
    print(gk == 6);
    print(gl);
    print(gl == 6);
    var k: i64 = 5;
    print(k);
    print(k == 6);
    let lk: i64 = 5;
    print(lk);
    print(lk == 6);
    var sk: i64 = 5;
    let t = str(sk);
    print(t, " ", sk == 6);
    var fk: i64 = 5;
    var b: u8[>..] = [];
    format(b, fk, ".");
    print(b, " ", fk == 6);
    var uk: i64 = 5;
    uk.print();
    print(uk == 6);
    bump(5);
}
