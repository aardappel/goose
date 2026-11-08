// const if conditions (const_for.md §11.2): comparisons of each constant
// kind, the connectives, and same<T1, T2>; the query names are identifiers
// elsewhere.
const SMALL: u8 = 7;
let BIG = 1 << 40;
let HALF = 0.5;
let NAME = "goose";
let ON = true;
type Text = const u8[:];
struct Pair<T> { a: T, b: T }
enum Shape { Circle { r: i64 }, Dot }

fn kind<T>() -> const u8[:] {
    const if same<T, i64> { return "int"; }
    else const if same<T, Pair<i64>> { return "pair"; }
    return "other";
}
fn same(a: i64, b: i64) -> bool { a == b }
fn main() {
    const for <C> in [1, 2, 4, 8, -3] {
        const if C >= 4 { print(C, " wide"); }
        else const if C % 2 == 0 { print(C, " even"); }
        else const if C < 0 { print(C, " negative"); }
        else { print(C, " small"); }
    }
    // A literal adapts to the typed constant's u8, and the comparison is at u8.
    const for <C> in [SMALL, SMALL + 1, 255] {
        const if C == SMALL { print("small"); }
        else const if C > SMALL && C <= 200 { print("above"); }
        else { print("far"); }
    }
    const if BIG > 1000000 && BIG == 1 << 40 { print("big"); }
    const for <F> in [0.25, HALF, 1.0, 2] {
        const if F < HALF { print(F, " below"); }
        else const if F == 1 { print(F, " one"); }
        else { print(F, " other"); }
    }
    const for <S> in ["a", NAME, "z"] {
        const if S == NAME { print("name"); }
        else const if S != "a" { print("not a: ", S); }
        else { print("a"); }
    }
    const if ON && !false { print("on"); }
    const if ON == true && (NAME == "goose" || 1 > 2) { print("combined"); }
    const if !(ON || SMALL == 0) { print("never"); } else { print("else"); }
    const for <N, A> in fields<Pair<i64>> { const if N == "b" { print("found ", N); } }
    const for <N1, V1> in variants<Shape> {
        const for <N2, V2> in variants<Shape> {
            const if N1 != N2 && !same<V1, V2> { print(N1, " vs ", N2); }
        }
    }
    const for <U> in types<i64, const u8[:], Text, Pair<i64>, Pair<bool>, i64&, Shape.Dot, u8[4]> {
        const if same<U, i64> { print("i64"); }
        else const if same<U, Text> { print("text"); }
        else const if same<U, Pair<i64>> { print("pair of i64"); }
        else const if same<U, i64&> { print("ref"); }
        else const if same<U, Shape.Dot> { print("dot"); }
        else const if same<U, u8[2 + 2]> { print("four bytes"); }
        else { print("other"); }
    }
    print(kind<i64>(), " ", kind<Pair<i64>>(), " ", kind<bool>());
    // Outside a const if condition, same and has_default are ordinary names.
    let has_default = 3;
    print(same(has_default, 3), " ", has_default < 4);
    if same(1, 1) && has_default < 4 { print("plain"); }
}
