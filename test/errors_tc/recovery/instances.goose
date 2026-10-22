// error: variable mode (..) requires an ADT type, not i64
// error: variable mode (..) requires an ADT type, not i64
// error: struct Pair takes 1 type argument(s), 2 given
// A struct or enum instance whose build an error cut short is not used
// again (docs/design/language_server.md §5 D2): a later root that names it
// ends silently, as a consequence of the error reported, rather than
// finding it half built, and a global of its type stays unassigned. The
// errors are S<i64>'s, E<i64>'s, and first's Pair<i64, i64>'s.
struct S<T> { x: T.., y: i64 }
enum E<T> { A { x: T.. }, B { y: i64 } }
struct Pair<T> { a: T }

var s1: S<i64>&? = null;
var s2: S<i64>&? = null;
var e1: E<i64>&? = null;
var e2: E<i64>&? = null;

fn first(p: Pair<i64, i64>&) -> i64 { 1 }
fn again(p: Pair<i64, i64>&) -> i64 { p.a }

fn main() {
    if s2 { print(s2.y); }
}
