// error: a line of a multi-line string must start with the indentation of its closing """
fn main() {
    let s = """
        indented
      less
        """;
    print(s);
}
