// error: only indentation may come before the closing """ of a multi-line string
fn main() {
    let s = """
        text
        more text""";
    print(s);
}
