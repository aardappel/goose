// error: cannot call grow, which grows P: the element pushed into P is still under construction
// An element holding `in pool` references is written field by field where it
// stays, as a self-relative one is, so a field initializer that grows the
// array would land inside the fields already written (§1.3(4)).
struct PN { link: PN&<u32 in P>?, key: i64 }
var P: PN[>..] = [];
fn grow() -> i64 { P.push(PN { link: null, key: 99 }); 5 }
fn main() {
    P.push(PN { link: null, key: 1 });
    P.push(PN { link: P[0], key: grow() });
    for n in P { print(n.key); }
}
