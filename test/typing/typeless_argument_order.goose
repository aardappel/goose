// A [] or null has no type of its own and binds no type variable: it takes
// the type the call's other arguments give its parameter, typed ones and
// literals alike, wherever it stands among them (§7.7), so the arguments'
// order does not decide whether a call resolves.
struct P { x: i64, y: i64 }

fn both<T>(x: T[], y: T[]) -> i64 { x.len * 10 + y.len }
fn put<T>(xs: T[..4], x: T) -> T[..4] {
    var ys = xs;
    ys.push(x);
    ys
}
fn put_last<T>(x: T, xs: T[..4]) -> T[..4] { put(xs, x) }
fn view<T>(x: const T[:], y: const T[:]) -> i64 { x.len * 10 + y.len }
fn count<T>(xs: const T[:], x: T) -> i64 { xs.len + x }
fn some<T>(x: T?, y: T&) -> i64 { if x { 1 } else { 0 } }
fn some_last<T>(y: T&, x: T?) -> i64 { if x { 1 } else { 0 } }
fn some_ref<T>(x: T?&, y: T&) -> i64 { if x { 1 } else { 0 } }
fn some_ref_last<T>(y: T&, x: T?&) -> i64 { if x { 1 } else { 0 } }

fn main() {
    let a: i64[] = [1, 2];
    let c = a.len > 1;
    print(both(copy(a), []));                           // 20
    print(both([], copy(a)));                           // 2
    print(both(if c { [] } else { [] }, copy(a)));      // 2
    print([].both(copy(a)));                            // 2
    let y: i16 = 7;
    print(put([], y));                                  // [7]
    print(put_last(y, []));                             // [7]
    print(put([], 5));                                  // [5]
    print(put_last(5, []));                             // [5]
    print(put([], 0.25));                               // [0.25]
    print(put([], P { x: 3, y: 4 }));                   // [P { 3, 4 }]
    let s = [1, 2];
    print(view(s, []));                                 // 20
    print(view([], s));                                 // 2
    print([].view(s));                                  // 2
    print(count([], y));                                // 7
    print(count([], 5));                                // 5
    var n = 5;
    var p = P { x: 1, y: 2 };
    print(some(null, n));                               // 0
    print(some_last(n, null));                          // 0
    print(some(null, p));                               // 0
    print(some(if c { null } else { null }, p));        // 0
    print(some_ref(null, n));                           // 0
    print(some_ref_last(n, null));                      // 0
}
