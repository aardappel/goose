// error: cannot call remove, which shrinks a inside a larger expression: a reference taken earlier in it may still be live, so bind the result first
fn read(r: i64&, unused: i64) { print(r); }
fn remove(a: i64[>..]&) -> i64 { let v = a.pop(); return v; }
fn main() {
    var a: i64[>..] = [42];
    read(a[0], remove(a));
}
