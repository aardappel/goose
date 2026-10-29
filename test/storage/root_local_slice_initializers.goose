// runtime-debug: owning storage needs a stack frame even for slice initializers.
root struct State { first: u8[>..], second: u8[>..] }
fn build(a: u8[:], b: u8[:]) -> i64 {
    var state = State { a, b };
    return 7;
}
fn text() -> u8[] {
    build("abc", "def");
    return "text";
}
fn main() {
    var words: u8[][>..] = [];
    words.push(text());
    words.push(text());
    for word in words { print(word); }
}
