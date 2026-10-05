// error: field initializers must follow declaration order: head comes before tail
// error: supplied fields in declaration order: head, tail
// Variable-size payloads retain their in-place construction guarantee.
enum Message { Text { head: u8[], flags: i64 = 0, tail: u8[] } }
fn main() {
    let message = Message.Text { tail: "world", head: "hello" };
    print(message);
}
