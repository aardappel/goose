// """ strings (§2): raw text, without escapes. Spanning lines, the text is
// the lines between the delimiters' own, each without the indentation of the
// closing """; a line of only whitespace may have less of it.
import std;

// A global holding one, a named constant.
let banner = """
    +--------+
    | "42" \ |
    +--------+
    """;

// The text with its line breaks shown as |, and its length.
fn show(label: const u8[:], s: const u8[:]) {
    var shown: u8[>..] = [];
    for c in s {
        if c == '\n' { shown.push('|'); } else { shown.push(c); }
    }
    print(label, ": [", shown, "] ", s.len);
}

fn main() {
    show("margin", """
        first
          indented by two
        last
        """);
    // An empty line, one with less whitespace than the margin and one with
    // more, which keeps what is past the margin.
    show("blank lines", """
        a

    
          
        b
        """);
    show("trailing line break", """
        text

        """);
    show("empty", """
        """);
    show("no margin", """
    kept
      as is
""");
    show("tab margin", """
		tab
		  then spaces
		""");
    show("raw", """
        no escapes: \n \t \" \\ \x41
        "quoted" and ""doubled"" quotes
        // not a comment, /* nor this */ { } [ ]
        """);
    show("one line", """say "hi" to C:\temp\new""");
    show("one line, empty", """""");
    show("banner", banner);
    print(banner);
    print("""
        abc
        """.len, " ", """xyz""" == "xyz", " ", find("""
            a"b
            """, "\""));
}
