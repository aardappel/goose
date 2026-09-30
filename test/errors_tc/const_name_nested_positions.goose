// error: array size K names a local variable, not a constant
// Wherever a size is in a written type: here in an enum's type argument, in
// the elements of an array, of a slice, in a struct's type argument and a
// reference's pointee, through arithmetic and negation (§3.3). Taking the
// global K made the annotation P<E<i64[5]>[2][:]>&, which &p fits.
let K = 3;

struct P<T> { t: T }
enum E<T> { A { t: T }, B }

fn count(p: P<E<i64[5]>[2][:]>&) -> i64 { p.t.len }

fn main() {
    let K = 5;
    var arrs: E<i64[5]>[2][1] = [[E<i64[5]>.B {}, E<i64[5]>.B {}]];
    var p = P<E<i64[5]>[2][:]> { t: arrs[..] };
    let r: P<E<i64[8 + -K]>[2][:]>& = &p;
    print(count(r), " ", K);
}
