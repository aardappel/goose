// runtime-debug: borrowed owner links form cycles without owning nesting.
root struct S { next: S&?, values: i64[>..], scratch: i64[>..<] }
fn walk(s: S&, n: i64) -> i64 {
    var at .= s;
    var total = 0;
    for i in n {
        total += at.values[0];
        let next = at.next;
        if next { at .= next; }
    }
    return total;
}
fn main() {
    var a = S { null, [1], [] };
    var b = S { a, [2], [] };
    a.next .= b;
    print(walk(a, 10));
}
