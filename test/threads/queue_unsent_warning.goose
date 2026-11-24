// warning: no qput sends Ev, so qget<Ev> receives nothing (§11.2); Ev.A, which a qput sends, has a queue of its own
// warning: no qput sends i32, so qpoll<i32> receives nothing (§11.2); i64, which a qput sends, has a queue of its own
// warning: no qput sends Unsent, so qpoll<Unsent> receives nothing (§11.2)
// A qget or qpoll of a type no qput in the program sends receives nothing
// (§11.2). A variant literal has its variant's type and an integer literal
// is an i64, so those go on queues of their own, which the warnings name.
// What a qput does send is received without one: a const value, a variant
// type's own queue, and a generic function's receive.
enum Ev { A { n: i64 }, B }
struct Unsent { n: i64 }
struct Pair { a: i64, b: i64 }

const P = Pair { a: 3, b: 4 };

fn take<T>() -> T { qget<T>() }

fn main() {
    qput(Ev.A { n: 7 });
    qput(7);
    let e, gote = qget<Ev>(10);
    let v, gotv = qpoll<i32>();
    let u, gotu = qpoll<Unsent>();
    print(gote, " ", gotv, " ", gotu);
    let a = qget<Ev.A>();
    let w = take<i64>();
    let q = Pair { a: 1, b: 2 };
    qput(q);
    qput(P);
    let x = qget<Pair>();
    let y = qget<Pair>();
    print(a.n, " ", w, " ", x.a + y.b);
}
