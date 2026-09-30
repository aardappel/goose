// A name in a size, a capacity or a fill count is the global constant
// wherever no local of the name hides it (§3.3, §11.1). Every size here is
// the global K's 3, though main, filled and the block have locals or
// parameters named K by the time they run: an alias's sizes, a field's type
// and default and a type bound to a type parameter name what they name
// where they are written, a parameter default names what a top-level
// declaration would (§7.1), a local declared after a size or a nested
// function is not in scope there, and `::K` names the global past a local.
let K = 3;
type Three = i64[K];
type Holder = P<i64[..K]>;

struct P<T> { a: T }
struct W<T> { s: u8[], t: T }
struct Q { a: i64[K], b: i64[3] = [7; K] }
struct R { p: P<i64[K]> }
struct X { w: W<i64[K]> }

// T is bound to a type written in main, before main's K: the `..` fills a
// with a default<T>() made here, where K is a local.
fn filled<T>() -> i64 {
    let K = 5;
    let p = P<T> { .. };
    p.a.len + K - 5
}

fn deflt(a: i64[3] = [1; K]) -> i64 { a[0] + a.len }

fn apply<F>(x: i64) -> i64 { F(x) }

fn main() {
    var early: i64[K] = [0; K];
    let viafilled = filled<i64[K]>();
    fn declared(a: i64[K]) -> i64[..K] {
        let b: i64[K] = a;
        [b[0], b.len]
    }
    let K = 5;
    var three: Three = [1, 2, 3];
    var held: Holder = P<i64[..::K]> { a: [] };
    let q = Q { .. };
    let r = R { .. };
    let x = X { .. };
    var qualified: i64[::K] = [9; ::K];
    let viablock = apply(K) { k: i64 =>
        let a: i64[::K] = [k; ::K];
        a.len + a[0]
    };
    let d = declared([4, 5, 6]);
    print(early.len, " ", three.len, " ", held.a.cap, " ", q.a.len, " ", q.b.len);
    print(r.p.a.len, " ", x.w.t.len, " ", qualified.len, " ", viafilled, " ", deflt());
    print(d.len, " ", d.cap, " ", d[0], " ", viablock, " ", K);
    print(match 3 { ::K => "global K", _ => "other" });
}
