// error: cannot copy the payload of Cons, which contains self-relative references, into a case of first taking it by value
// error: take it by reference: S.Cons&
// A case taking the variant by value copies the payload, whose links would
// then be measured from the copy (§3.9, §8.2).
enum S { Nil, Cons { v: i64, next: S..&<u32>? } }
fn first(c: S.Cons) -> i64 {
    let n = c.next;
    guard n else { return c.v; }
    return match n { Cons &d => d.v, Nil => -1 };
}
fn first(n: S.Nil) -> i64 { 0 }
fn main() {
    var ss: S..[>..] = [];
    let s0 .= ss.push(S.Cons { v: 42, next: null });
    let s1 .= ss.push(S.Cons { v: 1, next: s0 });
    print(first(s1));
}
