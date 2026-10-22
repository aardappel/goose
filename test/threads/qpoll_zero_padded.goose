// A missed qpoll or qget<T>(ms) yields T's zero value (§11.2), every byte
// of which is written, an explicit pad's (§3.2) included: of a struct, and
// of an ADT's first variant. Each loop's second receive misses where its
// first copied a value holding 300 elements, the high byte of whose count
// would otherwise show through.
struct M { a: u8, pad 3, b: i32, items: i64[] }
enum K { Z { m: M }, O { n: i64 } }

fn main() {
    qput(M { a: 1, b: 2, items: [7; 300] });
    for i in 0..2 {
        let m, ok = qpoll<M>();
        print(ok, " ", m.a, " ", m.b, " ", m.items.len);
    }
    let k: K.. = K.Z { m: M { a: 3, b: 4, items: [8; 300] } };
    qput(k);
    for i in 0..2 {
        let v, ok = qget<K..>(0);
        match v {
            Z z => print(ok, " ", z.m.a, " ", z.m.b, " ", z.m.items.len),
            O o => print(ok, " ", o.n),
        }
    }
}
