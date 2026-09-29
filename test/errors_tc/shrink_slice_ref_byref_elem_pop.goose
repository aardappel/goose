// error: cannot pop entries while n is still used: a reference into it was stored there
// rs refers to the text field of the element entries[1] itself (§3.8,
// §4.1), which the pop frees and the push reuses (§5.1).
struct E { text: u8[:] }
struct Named { rs: u8[:]& }
var src: u8[>..] = [];
var entries: E[>..] = [];
fn main() {
    src.append("abcdef");
    entries.push(E { text: src[0..2] });
    entries.push(E { text: src[2..4] });
    let n = Named { rs: entries[1].text };
    entries.pop();
    entries.push(E { text: src[4..6] });
    print(n.rs);
}
