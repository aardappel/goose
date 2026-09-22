var calls = 0;
fn limited(n: i64) -> i64[..] {
    calls++;
    var result: i64[..] = [..n + 5];
    for i in n { result.push(10 + i); }
    return result;
}
fn variable(n: i64) -> i64[] { calls++; return [n, n + 1]; }
fn grow(n: i64) -> i64[>..] { calls++; return [n, n + 1]; }
fn main() {
    var out: i64[>..] = [-1];
    for k in 0..3 {
        out.append(if k == 0 { limited(0) } else { limited(k) });
        out.push(-2);
    }
    out.append(if calls == 3 { variable(20) } else { variable(30) });
    out.append(if calls == 4 { grow(40) } else { grow(50) });
    out.push(-3);
    print(out, " ", calls);
}
