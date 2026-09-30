// error: payload of Cons contains self-relative references; bind it by reference (&c)
// A by-value binder copies the payload, whose links would then be measured
// from the copy (§3.9, §8.1).
enum S { Nil, Cons { v: i64, next: S..&<u32>? } }
fn main() {
    var ss: S..[>..] = [];
    let s0 .= ss.push(S.Nil);
    let s1 .= ss.push(S.Cons { v: 1, next: s0 });
    match s1 { Cons c => print(c.v), Nil => {} }
}
