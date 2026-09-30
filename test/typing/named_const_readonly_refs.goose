// `let` globals that compile-time sizes, fill counts and match patterns take
// at their initializers' values (§11.1), which nothing can write (§4.4): a
// read-only reference, a by-value argument, an identity comparison and a
// writable reference bound to a copy leave them so. A `const` global, which
// no reference can write, may meet a reference parameter too.

let K = 3;
let N = K + 1;
const C = 2;
var other = 7;
var a: i64[K] = [1, 2, 3];
var view: const i64& = other;
struct Grid { cells: i64[N] }

fn show(r: const i64&) -> i64 { r }
fn twice(x: i64) -> i64 { x * 2 }
fn bump(r: i64&) { r += 10; }
fn peek<T>(r: T&) -> i64 { r * 10 }
fn kind(x: i64) -> u8[] { match x { K => "K", N => "N", -C..C => "C", _ => "other" } }

fn main() {
    let c: const i64& = K;
    print(show(K), " ", twice(K), " ", c, " ", K .== other, " ", c .== K);
    var k = K;
    bump(k);
    print(k, " ", K, " ", peek(C));
    view .= K;
    print(view, " ", view .== K);
    let g = Grid { cells: [0; N] };
    let f = [5; K * C];
    print(a.len, " ", g.cells.len, " ", f.len);
    print(kind(3), " ", kind(4), " ", kind(-1), " ", kind(13));
    for i in 0..K { print(a[i]); }
}
