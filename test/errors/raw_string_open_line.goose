// error: a multi-line string's text starts on the line after its opening """
fn main() {
    let s = """first line
        second line
        """;
    print(s);
}
