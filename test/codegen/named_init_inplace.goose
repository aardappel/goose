// runtime-define: GS_MAX_STACKS=1
// Variable fields build into their final destination without staging copies.
struct Text { head: u8[], tail: u8[] }
fn make() -> Text { Text { tail: "world", head: "hello" } }
fn main() {
    let text = make();
    assert(text.head == "hello" && text.tail == "world");
}
