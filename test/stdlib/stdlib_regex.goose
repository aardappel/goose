// runtime-debug: stdlib/regex.goose. A table of patterns and texts, each
// printed with every match's groups as the regex crate finds them (the
// table was checked against the crate, regex 1.13 with Unicode off):
// literals, escapes, classes, the Perl classes, `.` with and without (?s),
// groups of every kind, flags, alternation, every repetition greedy and
// lazy; leftmost-first preference; captures that take no part, nest,
// repeat, or need the backtracker; empty matches and the rule for iterating
// over them; case folding. Then the skip loops over long texts against
// plain counts, the errors and the regex a malformed pattern gives,
// patterns too large to compile, and the rest of the API.
import std;
import regex;

// s with its line breaks and tabs made visible.
fn visible(out: u8[>..]&, s: u8[:]) {
    var a: u8[>..] = [];
    format_replaced(a, s, "\n", "\\n");
    format_replaced(out, a, "\t", "\\t");
}

// "pattern" on "text": each match as its groups' spans, start,end per group.
fn show(pattern: u8[:], text: u8[:]) {
    var line: u8[>..] = [];
    visible(line, pattern);
    line.append(" on ");
    visible(line, text);
    line.append(" =>");
    let re = regex::compile(pattern);
    var n = 0;
    regex::each_captures(re, text) { c =>
        line.push(' ');
        for v, k in c.slots { format(line, if k == 0 { "" } else if k % 2 == 0 { "|" } else { "," }, v); }
        n++;
    };
    if n == 0 { line.append(" none"); }
    let s, e = regex::find(re, text);
    if regex::count(re, text) != n || regex::matches(re, text) != (n > 0) || (n > 0) != (s >= 0) {
        line.append(" (count, matches or find disagree)");
    }
    print(line);
}

fn main() {
    // Literals, escapes and classes.
    show("abc", "xabcabcab");
    show("a\\.b\\\\c\\-", "a.b\\c- axb\\c-");
    show("\\x41\\x7a\\t\\n", "Az\t\n");
    show("[a-c]+", "xaby cc d");
    show("[^a-c\\n]+", "abxyz\nqcb");
    show("[]a]+", "x]a]b");
    show("[a-]+|[-b]+", "a-b-");
    show("[\\d.]+", "v1.25 and 3");
    show("\\d+", "a12b345");
    show("\\w+", "foo_bar1 baz!");
    show("\\s+", "a \t\nb");
    show("\\D\\W\\S", "a b!c");
    show(".+", "ab\ncd");
    show("(?s).+", "ab\ncd");
    show("(?s:.)b", "\nb");
    show("a.c", "abc a\nc");

    // Groups and flags.
    show("(a)(b)?(c)", "ac abc");
    show("(?:ab)+", "ababa");
    show("(?P<word>\\w+)@(?<host>\\w+)", "me@host you@there");
    show("((a)|(b))+", "abba");
    show("(?i)hello", "HeLLo hello HELLO");
    show("(?i:a)b", "Ab AB ab");
    show("(?i)a(?-i:b)c", "ABC AbC abc");
    show("(?i)[a-c]x", "Bx bX");
    show("a(?i)b|c", "aB C c");
    show("(?is)a.b", "A\nB");

    // Alternation and leftmost-first preference.
    show("a|ab", "ab");
    show("ab|a", "ab");
    show("foo|foobar", "foobar");
    show("(a|ab)(c|bcd)(d*)", "abcd");
    show("x*y|x*z", "xxz");
    show("|a", "aa");

    // Repetition, greedy and lazy.
    show("a*", "aaa");
    show("a+?", "aaa");
    show("a??b", "ab b");
    show("a{2}", "aaaaa");
    show("a{2,}", "a aa aaaa");
    show("a{1,3}", "aaaaa");
    show("a{1,3}?", "aaaaa");
    show("a{0}b", "ab");
    show("a**", "aaa");
    show("a{2}{3}", "aaaaaaa");
    show("<.+>", "<a><b>");
    show("<.+?>", "<a><b>");
    show("(a+)(a+)", "aaaa");
    show("(a+?)(a+)", "aaaa");
    show("(ab|a)*c", "abac");
    show("(a*)*b", "aab");
    show("(a|)+b", "aab");

    // Empty matches.
    show("", "abc");
    show("a*", "baaa");
    show("x*", "axxb");
    show("a*?", "aa");
    show("(a*)b?", "ab");

    // Captures the backtracker finds.
    show("(\\w+)\\s+(\\w+)?", "one two three");
    show("(a|bc)+(d)", "abcad");
    show("([a-c]*)c([a-c]*)", "abcbca");

    // The skip loops: sparse matches in long texts, counted against a plain
    // count of the words written there.
    var text: u8[>..] = [];
    var r = rng { 3 };
    var words = 0;
    var mails = 0;
    for i in 3000 {
        let k = r.rand_int(100);
        if k == 0 { text.append("xyz123 "); words++; }
        else if k == 1 { text.append("ab@cd.com "); mails++; }
        else if k == 2 { text.append("@@.com "); }
        else { text.append(if k % 2 == 0 { "plain " } else { "words " }); }
    }
    let prefix = regex::compile("xyz\\d+");
    let mail = regex::compile("\\w+@\\w+\\.com");
    let rare = regex::compile("Q");
    print(regex::count(prefix, text) == words, " ", regex::count(mail, text) == mails, " ",
          regex::count(rare, text), " ", prefix.skip == regex::Skip.Prefix, " ",
          mail.skip == regex::Skip.Inner);

    // Errors, and the regex a malformed pattern gives.
    let bad = ["[", "[a", "(", "(a", "a)", "*a", "+", "a{", "a{1", "a{x}", "a{2,1}", "a{1001}",
               "\\", "\\q", "\\xg1", "[z-a]", "(?x)", "(?P<n", "[[a]]", "[a--b]", "[\\w&&x]", "^a", "a$",
               "(?:a{1000}){20}", "[ab]*a[ab]{20}"];
    for p in bad {
        var err: u8[>..] = [];
        let re = regex::compile(p, err);
        let s, e = regex::find(re, "aaa[ab]");
        print(err, " / ", regex::matches(re, "a"), " ", regex::count(re, "aaa"), " ", s, " ", e);
    }

    // The rest of the API.
    let re = regex::compile("""\{\{\s*(\w+)\s*\}\}""");
    var out: u8[>..] = [];
    out.append("> ");
    regex::format_replaced(out, re, "Hi {{ name }}, {{x}}{{y}} {{ z") { caps =>
        let g = regex::group(caps, 1);
        let with: u8[>..] = if g == "name" { "you" } else { str("<", g, ">") };
        with
    };
    print(out);
    let s, e = regex::find(re, "no match");
    print(s, " ", e, " ", regex::matches(re, "{{a}}"), " ", regex::matches(re, "{{}}"));
    let opt = regex::compile("(a)|(b)");
    regex::each_captures(opt, "ab") { c =>
        print(regex::group(c, 0), " [", regex::group(c, 1), "] [", regex::group(c, 2), "] ",
              c.slots[2], " ", c.slots[4]);
    };

    // A block's `return` leaves the function calling the search.
    fn second_digit_run(t: u8[:]) -> i64 {
        let digits = regex::compile("\\d+");
        var n = 0;
        regex::each_match(digits, t) { s, e =>
            if n == 1 { return s; }
            n++;
        };
        return -1;
    }
    print(second_digit_run("a1b22c333"), " ", second_digit_run("a1"));
}
