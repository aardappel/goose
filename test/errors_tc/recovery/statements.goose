// error: expression expected, found ';'
// error: ';' expected after expression, found '1'
// error: expected ';' in variable declaration, found '}'
// error: expected ')' in call arguments, found 'a'
// error: expression expected, found 'let'
// error: expected ';' in assignment statement, found '}'
// error: expected identifier in variable declaration, found '='
// The parser reports the error a statement raises, loses the statement,
// and goes on at the next one, in the same block or after its `}`
// (docs/design/language_server.md §5 D): every function's errors come
// out, in order, one per broken statement. A function type, `fn(`, in
// what is skipped starts no nested function.
fn f(x: i64) -> i64 {
    let y = x +;
    let z = x * 2;
    z
}

fn g(a: i64) -> i64 {
    a 1
    a + 1
}

fn h(n: i64) -> i64 {
    var total = 0;
    for i in 0..n {
        if i > 2 {
            let w = i
        }
        total += i;
    }
    total
}

fn k(a: i64[:]) -> i64 {
    print(a[0] a[1]);
    print(a[1],
    let b = 5;
    var c = 1;
    c = b
}

fn cast(a: i64) -> i64 {
    let = f as fn(i64) -> i64;
    a
}

fn main() {
    print(f(1) + g(2) + h(3));
}
