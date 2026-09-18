// This file has CRLF line breaks, which .gitattributes keeps: a """ string's
// line breaks are \n all the same, and its lines count toward the line
// numbers after it, which the assert failing at the end reports.
fn main() {
    let s = """
        one
        two

        """;
    var crs = 0;
    for c in s {
        if c == '\r' { crs++; }
    }
    print(s.len, " bytes, ", crs, " carriage returns, ", """one line""".len);
    let t = """
        three
        """;
    print(t);
    assert(t.len == 0);
}
