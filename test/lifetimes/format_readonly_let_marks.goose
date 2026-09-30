// A format overload given a `let` by a `const` reference or by value, or a
// `const` one by any reference, cannot write it (§3.7, §4.4): the `let` stays
// known non-negative, meeting a u64 in a comparison (§6.1), and a mark stays
// one, whose resize back to it is balanced (§5.2), so v stays in use across
// the call. print, str and format alike.

fn format(out: u8[>..]&, v: const i64&) { out.append("<"); out.format(v as u64); out.append(">"); }
fn format(out: u8[>..]&, v: i32) { out.append("("); out.format(v as u64); out.append(")"); }
fn format(out: u8[>..]&, v: i16&) { out.append("["); out.format(v as u64); out.append("]"); }

fn helper(s: i64[>..<]&) -> u8[>..] {
    let mark = s.len;
    s.push(7);
    var b = str(mark, " ");
    b.format(mark);
    print(mark);
    s.resize(mark);
    b
}

fn main() {
    var a: i64[>..<] = [1, 2];
    let v = a[0..];
    let n = a.len;
    let m: i32 = 3;
    let c: const i16 = 4;
    var b: u8[>..] = "";
    b.format(n, m, c);
    print(n, " ", m, " ", c, " ", str(n, m, c), " ", b);
    let u: u64 = 2;
    print(u < n, " ", u < m, " ", u < c);
    print(helper(a), " ", a.len as u64, " ", v[1] as u64);
}
