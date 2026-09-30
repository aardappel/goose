// error: enum S has payloads holding self-relative references, which a copy does not keep
// error: use it in variable mode (S..), whose payloads bind by reference, or make the references `in pool`
// A fixed-mode payload binds only by value (§3.5), and a copy does not keep
// self-relative references (§3.9), so nothing could read S's links: S's
// own link to a fixed-mode S is rejected where S is declared.
enum S { Nil, Cons { v: i64, next: S&<u32>? } }
fn main() {
    var ss: S[>..] = [];
    ss.push(S.Nil);
    ss.push(S.Cons { v: 1, next: ss[0] });
    match ss[1] { Cons c => print(c.v), Nil => {} }
}
