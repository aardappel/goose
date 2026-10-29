// runtime-debug: pin the receiver before an operand rebinds its owner reference.
root struct S { first: i64[>..], second: i64[>..] }
fn main() {
    var a = S { [1], [2] };
    var b = S { [3], [4] };
    var r .= a;
    fn next() -> i64 { r .= b; 9 }
    r.first.push(next());
    print(a.first, " ", b.first, " ", r.first);
}
