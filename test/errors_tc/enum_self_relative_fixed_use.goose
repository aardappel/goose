// error: enum S has payloads holding self-relative references
// S links to variable-mode S's, but an array of fixed-mode ones would hold
// payloads that bind only by value (§3.5), which loses the links (§3.9).
enum S { Nil, Cons { v: i64, next: S..&<u32>? } }
fn main() {
    var ss: S[>..] = [];
    ss.push(S.Nil);
    print(ss.len);
}
