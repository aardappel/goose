// error: cannot assign t while v is still used: the slices it views may point into it
// Assigning t frees its grow-only tail, which v's elements view (§5.1).
struct T { n: i64, tail: u8[>..] }
fn main() {
    var t = T { n: 1, tail: [] };
    t.tail.append("abc");
    var ss: (u8[:])[>..] = [];
    ss.push(t.tail[..]);
    let v = ss[..];
    t = T { n: 2, tail: [] };
    t.tail.append("ZZZZZZZZ");
    print(v[0]);
}
