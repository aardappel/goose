import.scan;
import.config;

// Layout is conservative around type punctuation (angles, references, array
// modes) so it does not need to guess whether a token is an operator or type.
fn layout_newline(out: u8[>..]&, eol: const u8[:]) {
    if out.len > 0 && out[out.len - 1] != '\n' { out.append(eol); }
}

fn layout_space(out: u8[>..]&) {
    if out.len > 0 && !source_space(out[out.len - 1]) { out.push(' '); }
}

fn layout_operator(s: const u8[:]) -> bool {
    for op in["=", "==", "!=", "<=", ">=", "&&", "||", "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=", "<<=", ">>=", ".=", ".==", ".!=", "=>", "->", "|", "^", "/", "%"] {
        if s == op { return true; }
    }
    return false;
}

// Recognize declaration bodies without interpreting expression or type punctuation.
// Only bodies with at most maxCompactStatements top-level statements can be
// compacted. A final expression or a block such as `if` counts as one
// statement, and statements nested inside that block do not count. Comments
// and declarations stay multiline. veto marks bodies whose compact form was
// found to exceed maxLineWidth. closers maps a compact body's closing brace
// to its opening brace, so the width can be measured once the line is laid out.
fn inline_expression_close(s: const u8[:], ts: const SourceToken[:], open: i64,
options: FormatterOptions, veto: const bool[:]) -> i64 {
    if !options.single_line_expressions || veto[open] { return -1; }
    var close = open + 1;
    var statements = 0;
    while close < ts.len {
        let text = token_text(s, ts[close]);
        if text == "}" { break; }
        // Nested blocks and comments need their own layout decisions.
        if text == "{" || ts[close].kind == COMMENT_TOKEN { return -1; }
        if text == ";" { statements++; }
        for keyword in["let", "var", "const", "fn", "thread_fn", "for", "while", "loop", "match", "guard", "defer", "struct", "enum", "type"] {
            if text == keyword { return -1; }
        }
        close++;
    }
    if close >= ts.len || close == open + 1 || find(s[ts[open].end..ts[close].start], "\n") >= 0 {
        return -1;
    }
    if token_text(s, ts[close - 1]) != ";" { statements++; }
    return if statements == 1 { close } else { -1 };
}

fn function_layout(s: const u8[:], ts: const SourceToken[:], options: FormatterOptions,
veto: const bool[:], compact: bool[>..]&, gaps: i64[>..]&, closers: i64[>..]&) {
    for t in ts {
        compact.push(false);
        gaps.push(-1);
        closers.push(-1);
    }
    var last_close = -1;
    var last_compact = false;
    var nesting = 0;
    for n in ts.len {
        let word = token_text(s, ts[n]);
        if word == "}" { nesting--; }
        let first_name = if n + 2 < ts.len && token_text(s, ts[n + 1]) == "::" && ts[n + 2].kind == NAME_TOKEN {
            n + 2
        } else {
            n + 1
        };
        if (word == "fn" || word == "thread_fn") && first_name < ts.len && ts[first_name].kind == NAME_TOKEN {
            var open = first_name;
            var parens = 0;
            var squares = 0;
            while open < ts.len {
                let text = token_text(s, ts[open]);
                if parens == 0 && squares == 0 &&(text == "{" || text == ";" || text == "=") {
                    break;
                }
                if text == "(" { parens++; }
                if text == ")" { parens--; }
                if text == "[" { squares++; }
                if text == "]" { squares--; }
                open++;
            }
            if open < ts.len && token_text(s, ts[open]) == "{" {
                var close = open + 1;
                var braces = 1;
                var simple = options.single_line_expressions && !veto[open];
                var statements = 0;
                while close < ts.len {
                    let text = token_text(s, ts[close]);
                    if text == "{" && close > open {
                        let inline_close = inline_expression_close(s, ts, close, options, veto);
                        if inline_close >= 0 {
                            for i in close..inline_close + 1 { compact[i] = true; }
                            closers[inline_close] = close;
                        }
                    }
                    if text == "}" {
                        braces--;
                        if braces == 0 { break; }
                        // A block that is not continued (else, call, `?`) is a statement of its own.
                        if braces == 1 && close + 1 < ts.len {
                            let after = token_text(s, ts[close + 1]);
                            if after != "else" && after != "." && after != "?" && after != ")" && after != "," && after != ";" && after != "}" {
                                statements++;
                            }
                        }
                    }
                    if text == "{" { braces++; }
                    if ts[close].kind == COMMENT_TOKEN || find(text, "\n") >= 0 { simple = false; }
                    if text == ";" && braces == 1 { statements++; }
                    for keyword in["let", "var", "const", "fn", "thread_fn", "for", "while", "loop", "match", "guard", "defer", "struct", "enum", "type"] {
                        if text == keyword { simple = false; }
                    }
                    close++;
                }
                if close < ts.len {
                    if close > open + 1 && token_text(s, ts[close - 1]) != ";" { statements++; }
                    if statements > options.max_compact_statements { simple = false; }
                    if close == open + 1 { simple = true; }
                    if simple {
                        // The whole body is one line, so its inline blocks need no layout of their own.
                        for i in open..close + 1 {
                            compact[i] = true;
                            closers[i] = -1;
                        }
                        if close > open + 1 { closers[close] = open; }
                    }
                    if nesting == 0 {
                        var start = declaration_prefix(s, ts, n);
                        while start > 0 {
                            if ts[start - 1].kind != COMMENT_TOKEN { break; }
                            // A comment following code on the same line belongs to that code.
                            if start > 1 && find(s[ts[start - 2].end..ts[start - 1].start], "\n") < 0 {
                                break;
                            }
                            start--;
                        }
                        if last_close >= 0 {
                            var consecutive = true;
                            for i in last_close + 1..start {
                                if ts[i].kind != COMMENT_TOKEN { consecutive = false; }
                            }
                            if consecutive {
                                var breaks = 0;
                                let end = if start > last_close + 1 { ts[start - 1].end } else {
                                    ts[last_close].end
                                };
                                for i in end..ts[start].start {
                                    if s[i] == '\n' { breaks++; }
                                }
                                gaps[start] = if !simple || !last_compact { 1 } else {
                                    min(max(0, breaks - 1), options.max_blank_lines_between_functions)
                                };
                            }
                        }
                        last_close = close;
                        last_compact = simple;
                    }
                }
            }
        }
        if word == "{" { nesting++; }
    }
}

// One layout pass. Returns true when a compact body overflowed maxLineWidth and
// was vetoed. The caller then discards out and lays the source out again.
fn format_pass(s: const u8[:], ts: const SourceToken[:], options: FormatterOptions, veto: bool[>..]&,
out: u8[>..]&, eol: const u8[:]) -> bool {
    let width = options.indent_width;
    let spaces = options.insert_spaces;
    let base = out.len;
    var overflow = false;
    var compact: bool[>..] =[];
    var gaps: i64[>..] =[];
    var closers: i64[>..] =[];
    function_layout(s, ts, options, veto, compact, gaps, closers);
    var depth = 0;
    var parens = 0;
    var squares = 0;
    var brace_parens: i64[>..] =[];
    var brace_squares: i64[>..] =[];
    var previous: const u8[:] = s[..0];
    var previous_end = 0;
    var previous_kind = 0;
    var previous_binary = false;
    for n in ts.len {
        let t = ts[n];
        let text = token_text(s, t);
        let operand_before = previous_kind == NUMBER_TOKEN || previous_kind == LITERAL_TOKEN || previous == ")" || previous == "]" ||
        (previous_kind == NAME_TOKEN && previous != "return" && previous != "break" && previous != "in" && previous != "if" && previous != "while" && previous != "as" && previous != "else");
        let binary =(text == "+" || text == "-" || text == "*") && operand_before;
        let next: const u8[:] = if n + 1 < ts.len { token_text(s, ts[n + 1]) } else { "" };
        var linebreaks = 0;
        for i in previous_end..t.start {
            if s[i] == '\n' { linebreaks++; }
        }
        if compact[n] && text != "{" { linebreaks = 0; }
        if gaps[n] >= 0 {
            layout_newline(out, eol);
            for i in gaps[n] { out.append(eol); }
            linebreaks = 0;
        }
        if linebreaks > 0 { layout_newline(out, eol); }
        if linebreaks > 1 && out.len > 0 {
            // At most one empty line. Blank lines already present stay as they are.
            if out.len < eol.len * 2 || out[out.len - eol.len * 2..] != str(eol, eol) {
                out.append(eol);
            }
        }
        if text == "}" {
            depth--;
            if previous != "{" && !compact[n] { layout_newline(out, eol); }
            if brace_parens.len > 0 {
                brace_parens.pop();
                brace_squares.pop();
            }
        }
        if out.len == 0 || out[out.len - 1] == '\n' {
            for i in depth {
                if spaces {
                    for j in width { out.push(' '); }
                } else { out.push('\t'); }
            }
        } else {
            let tight_left = text == ")" || text == "]" || text == "," || text == ";" || text == ":" || text == "." || text == "::" || text == "?";
            let tight_right = previous == "(" || previous == "[" || previous == "." || previous == "::";
            let control_paren = text == "(" &&(previous == "if" || previous == "while" || previous == "match" || previous == "guard" || previous == "for");
            // A bracket that starts a value (after an operator, separator or keyword) is not a call or an index.
            let value_open =(text == "(" || text == "[") &&(layout_operator(previous) || previous_binary || previous == "," || previous == ":" || previous == ";" ||
                previous == "return" || previous == "in" || previous == "else");
            let import_dot = text == "." && previous == "import";
            if text == "}" && previous == "{" {
                /* empty block */
            }
            else if text == "::" && previous == ":" { layout_space(out); }
            else if text == "{" ||(previous == "}" && !tight_left) ||(compact[n] &&(previous == "{" || text == "}")) || control_paren || value_open || import_dot {
                layout_space(out);
            }
            else if !tight_left && !tight_right && text != "(" && text != "[" {
                if layout_operator(text) || layout_operator(previous) || binary || previous_binary || previous == "," || previous == ":" || previous == ";" ||
                (previous_kind != PUNCT_TOKEN && t.kind != PUNCT_TOKEN) || t.kind == COMMENT_TOKEN || previous_kind == COMMENT_TOKEN ||
                t.start > previous_end { layout_space(out); }
            }
        }
        out.append(text);
        if closers[n] >= 0 {
            var line = out.len;
            while line > base && out[line - 1] != '\n' { line--; }
            var columns = 0;
            for k in line..out.len {
                if out[k] == '\t' { columns += width; }
                else if (out[k] & 192) != 128 { columns++; }
            }
            if columns > options.max_line_width {
                veto[closers[n]] = true;
                overflow = true;
            }
        }
        if text == "(" { parens++; }
        if text == ")" { parens--; }
        if text == "[" { squares++; }
        if text == "]" { squares--; }
        if text == "{" {
            depth++;
            brace_parens.push(parens);
            brace_squares.push(squares);
            if next != "}" && !compact[n] { layout_newline(out, eol); }
        }
        var at_statement = parens == 0 && squares == 0;
        if brace_parens.len > 0 {
            at_statement = parens == brace_parens[brace_parens.len - 1] && squares == brace_squares[brace_squares.len - 1];
        }
        let trailing_comment = n + 1 < ts.len && ts[n + 1].kind == COMMENT_TOKEN && source_has(s, ts[n + 1].start, "//") &&
        find(s[t.end..ts[n + 1].start], "\n") < 0;
        if text == ";" && at_statement && !trailing_comment && !compact[n] {
            layout_newline(out, eol);
        }
        if text == "}" && next != ";" && next != "," && next != ")" && next != "]" && next != "." && next != "?" && next != "else" && next != "(" && next != "}" && !trailing_comment &&(!compact[n] || closers[n] >= 0) {
            layout_newline(out, eol);
        }
        if t.kind == COMMENT_TOKEN && source_has(s, t.start, "//") { layout_newline(out, eol); }
        previous = text;
        previous_end = t.end;
        previous_kind = t.kind;
        previous_binary = binary;
    }
    layout_newline(out, eol);
    return overflow;
}

// Appends formatted text. Callers discard out when it returns false. Raw strings and
// block comment contents keep their exact bytes.
fn format_source(s: const u8[:], out: u8[>..]&, options: FormatterOptions) -> bool {
    var ts: SourceToken[>..] =[];
    if scan_source(s, ts) >= 0 { return false; }
    if ts.len == 0 { return true; }
    var eol: const u8[:] = "\n";
    for i in s.len {
        if source_has(s, i, "\r\n") {
            eol = "\r\n";
            break;
        }
    }
    var veto: bool[>..] =[];
    for t in ts { veto.push(false); }
    // Each retry vetoes at least one more body, so this terminates.
    loop {
        var pass: u8[>..] =[];
        if !format_pass(s, ts, options, veto, pass, eol) {
            out.append(pass);
            break;
        }
    }
    // Verify that whitespace changes did not merge punctuation or numbers.
    var check: SourceToken[>..] =[];
    if scan_source(out, check) >= 0 || check.len != ts.len { return false; }
    for i in ts.len {
        if token_text(s, ts[i]) != token_text(out, check[i]) { return false; }
    }
    return true;
}
