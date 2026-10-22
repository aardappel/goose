// error: expected identifier in field access, found ','
// error: type expected, found '}'
// error: expected identifier in field access, found '}'
// error: type expected, found '}'
// error: expected identifier in field access, found ','
// error: type expected, found ')'
// error: expected identifier in field access, found ';'
// error: expected identifier in field access, found 'let'
// error: expected identifier in field access, found '}'
// error: expected identifier in field access, found 'end of file'
// A `.` with no name after it is reported once and kept as an
// incomplete Dot (docs/design/language_server.md §5 D); the error the rest
// of its statement then raises is a consequence and is not reported, nor,
// where the `.` ends the file, is the end of the blocks still open there.
// In a declaration, the error of a member after the one holding the `.`
// is no consequence of it.
struct V { x: i64 }
struct S { a: i64 = v., b: }
enum E { A { x: i64 = v. }, B { y: } }
fn h(a: i64 = v., b: ) -> i64 { a }
fn f(v: V) -> i64 {
    let a = v.;
    print(v.
    let b = 2;
    a + b
}
fn main() {
    let v = V { x: 1 };
    print(f(v));
    let w = v.
}
fn g(v: V) {
    if v.x > 0 {
        let y = v.
