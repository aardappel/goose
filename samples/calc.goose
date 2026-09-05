// An interactive calculator: reads a line, tokenizes it, parses it by
// recursive descent straight to a value, and prints the result. Variables
// are kept in a dictionary, so `x = 3 * (2 + y)` works. Errors are the
// interesting part: a bad token, a missing parenthesis or a division by
// zero can happen deep inside the recursion, and each one is a single
// `return ... from evaluate`, which unwinds every parse frame in between
// and lands the message at the one place that reports it.
//
// Build and run (from this directory):
//   goose -o calc.c calc.goose && cl calc.c && calc < data/calc.stdin
// or run it without redirection and type expressions at the prompt.

import std;
import dictionary;
import math;
import os;

enum Token {
    Num { v: f64 },
    Name { at: i32, len: i32 },          // a slice of the line, by position
    Op { c: u8 },                        // + - * / ( ) = ^
    End,
}

// Tokens are fixed-mode enum values in a plain array: 9 bytes each,
// indexable, and the parser walks them with one cursor. The array is
// grow-shrink, since it is a global cleared for every new line; the line
// itself is a local of main, passed down as a slice.
var tokens: Token[>..<] = [];
var pos = 0;
var vars = dictionary<u8[..16], f64> {};

fn tokenize(line: u8[:]) {
    tokens.clear();
    var i = 0;
    while i < line.len {
        let c = line[i];
        if c == ' ' || c == '\t' { i++; continue; }
        if (c >= '0' && c <= '9') || c == '.' {
            let start = i;
            while i < line.len && ((line[i] >= '0' && line[i] <= '9') || line[i] == '.') { i++; }
            let v, ok = parse_flt(line[start..i]);
            guard ok else { return 0.0, str("bad number '", line[start..i], "'") from evaluate; }
            tokens.push(Token.Num { v: v });
            continue;
        }
        if (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') {
            let start = i;
            while i < line.len && ((line[i] >= 'a' && line[i] <= 'z') || (line[i] >= 'A' && line[i] <= 'Z')) { i++; }
            guard i - start <= 16 else { return 0.0, "name too long" from evaluate; }
            tokens.push(Token.Name { at: start as i32, len: (i - start) as i32 });
            continue;
        }
        guard find("+-*/()=^", line[i..i + 1]) >= 0 else {
            return 0.0, str("unexpected character '", line[i..i + 1], "'") from evaluate;
        }
        tokens.push(Token.Op { c: c });
        i++;
    }
    tokens.push(Token.End);
}

// The token under the cursor, and a test for an operator character.
fn peek_op(c: u8) -> bool {
    match tokens[pos] { Op o => o.c == c, _ => false }
}

fn name_key(line: u8[:], t: Token.Name) -> u8[..16] {
    var k: u8[..16] = [];
    k.append(line[t.at..t.at + t.len]);
    return k;
}

// expr := term (('+' | '-') term)*
recursive fn parse_expr(line: u8[:]) -> f64 {
    var v = parse_term(line);
    loop {
        if peek_op('+') { pos++; v += parse_term(line); }
        else if peek_op('-') { pos++; v -= parse_term(line); }
        else { break; }
    }
    return v;
}

// term := power (('*' | '/') power)*
recursive fn parse_term(line: u8[:]) -> f64 {
    var v = parse_power(line);
    loop {
        if peek_op('*') { pos++; v *= parse_power(line); }
        else if peek_op('/') {
            pos++;
            let d = parse_power(line);
            guard d != 0.0 else { return 0.0, "division by zero" from evaluate; }
            v /= d;
        }
        else { break; }
    }
    return v;
}

// power := unary ('^' power)?     (right associative)
recursive fn parse_power(line: u8[:]) -> f64 {
    let base = parse_unary(line);
    if peek_op('^') { pos++; return pow(base, parse_power(line)); }
    return base;
}

// unary := '-' unary | '(' expr ')' | number | name
recursive fn parse_unary(line: u8[:]) -> f64 {
    if peek_op('-') { pos++; return 0.0 - parse_unary(line); }
    if peek_op('(') {
        pos++;
        let v = parse_expr(line);
        guard peek_op(')') else { return 0.0, "expected ')'" from evaluate; }
        pos++;
        return v;
    }
    let t = tokens[pos];
    pos++;
    match t {
        Num n => n.v,
        Name nm => {
            let v = vars.get(name_key(line, nm));
            guard v else { return 0.0, str("unknown variable '", line[nm.at..nm.at + nm.len], "'") from evaluate; }
            v
        },
        Op o => { return 0.0, unexpected(o.c) from evaluate; },
        End => { return 0.0, "unexpected end of input" from evaluate; },
    }
}

// One line: `name = expr` or `expr`. Every deep failure returns here.
fn evaluate(line: u8[:]) -> f64, u8[] {
    tokenize(line);
    pos = 0;
    // An assignment is a name followed by '='.
    if tokens.len > 2 {
        match tokens[0] {
            Name nm => if peek_op_at(1, '=') {
                pos = 2;
                let v = parse_expr(line);
                guard peek_end() else { return 0.0, "trailing input" from evaluate; }
                vars.insert(name_key(line, nm), v);
                return v, "";
            },
            _ => {},
        }
    }
    let v = parse_expr(line);
    guard peek_end() else { return 0.0, "trailing input" from evaluate; }
    return v, "";
}

// The message for a stray operator, built outside the parsing cycle (a
// function in a recursive cycle cannot own a builder) and constructed
// straight into the error result.
fn unexpected(c: u8) -> u8[>..] {
    var msg: u8[>..] = [];
    format(msg, "unexpected '");
    msg.push(c);                         // the byte itself, where str(c) would print its number
    msg.push('\'');
    return msg;
}

fn peek_op_at(i: i64, c: u8) -> bool { match tokens[i] { Op o => o.c == c, _ => false } }
fn peek_end() -> bool { match tokens[pos] { End => true, _ => false } }

fn main() {
    vars.insert("pi", PI);
    vars.insert("e", 2.718281828459045);
    print("calc: expressions, `name = expr`, `quit`");
    var line: u8[>..] = [];
    loop {
        write_stdout("> ");
        flush_stdout();
        line.clear();                    // a local: nothing points into it here
        guard read_line(line);           // false at the end of input
        let t = trim(line);
        if t.len == 0 { continue; }
        if t == "quit" { break; }
        let v, err = evaluate(line);
        if err.len > 0 { print("error: ", err); continue; }
        var out: u8[>..] = [];
        format_flt(out, v, 6);
        // Trim trailing zeros and a dangling point, so 3 prints as 3.
        while out.len > 1 && out[out.len - 1] == '0' { out.pop(); }
        if out[out.len - 1] == '.' { out.pop(); }
        print(out);
    }
    print("bye");
}
