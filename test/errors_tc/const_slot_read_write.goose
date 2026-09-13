// error: cannot assign through this path (const, or a read-only instantiation, §9.5)
// A `const` slot reads back read-only, wherever the value came from (§9.5).
struct Box { s: const u8[:] }
fn main() {
    var text: u8[>..] = "abc";
    var box = Box { s: text[..] };
    box.s[0] = 88;
}
