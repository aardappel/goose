// Calculator::Ast: a generated program of assignments over + - * / % and
// parentheses, parsed by recursive descent into an AST on every run.
//
// The nodes are a variable-mode enum in one grow-only pool, each exactly as
// big as its variant, linked to its children by 4-byte self-relative
// references. A variable's name is stored inline in its node, so the tree
// owns its names without an allocation per name. A run's pool and
// expression list are locals of the run, so the previous run's tree goes
// away with a pointer reset rather than a walk over its nodes.

namespace calculator_ast;

import std;
import bench;
import helper;

enum Node {
    Number { value: i64 },
    Variable { name: u8[varint] },
    BinaryOp { op: u8, left: Node..&<u32>, right: Node..&<u32> },
    Assignment { name: u8[varint], expr: Node..&<u32> },
}

let EOF: u8 = 0;

fn generate_random_program(n: i64) -> u8[>..] {
    var result: u8[>..] = [];
    result.append("v0 = 1\n");
    for i in 10 {
        let v = i + 1;
        format(result, "v", v, " = v", v - 1, " + ", v, "\n");
    }
    for i in n {
        let v = i + 10;
        format(result, "v", v, " = v", v - 1, " + ");
        match next_int(10) {
            0 => format(result, "(v", v - 1, " / 3) * 4 - ", i, " / (3 + (18 - v", v - 2,
                        ")) % v", v - 3, " + 2 * ((9 - v", v - 6, ") * (v", v - 5, " + 7))"),
            1 => format(result, "v", v - 1, " + (v", v - 2, " + v", v - 3, ") * v", v - 4,
                        " - (v", v - 5, " / v", v - 6, ")"),
            2 => format(result, "(3789 - (((v", v - 7, ")))) + 1"),
            3 => format(result, "4/2 * (1-3) + v", v - 9, "/v", v - 5),
            4 => format(result, "1+2+3+4+5+6+v", v - 1),
            5 => format(result, "(99999 / v", v - 3, ")"),
            6 => format(result, "0 + 0 - v", v - 8),
            7 => format(result, "((((((((((v", v - 6, ")))))))))) * 2"),
            8 => format(result, i, " * (v", v - 1, "%6)%7"),
            9 => format(result, "(1)/(0-v", v - 5, ") + (v", v - 7, ")"),
            _ => abort("next_int(10) out of range"),
        }
        result.push('\n');
    }
    return result;
}

fn is_digit(c: u8) -> bool { c >= '0' && c <= '9' }
fn is_letter(c: u8) -> bool { (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') }
fn is_whitespace(c: u8) -> bool { c == ' ' || c == '\t' || c == '\n' || c == '\r' }

// Parses `input` into `nodes`, appending each top-level expression to
// `expressions`. The cursor and the byte under it are this function's
// locals, which the nested parsing functions share.
fn parse(input: const u8[:], nodes: Node..[>..]&, expressions: (Node..&)[>..]&) {
    var pos = 0;
    var current = if input.len > 0 { input[0] } else { EOF };

    fn advance() {
        pos++;
        current = if pos >= input.len { EOF } else { input[pos] };
    }

    fn skip_whitespace() {
        while is_whitespace(current) { advance(); }
    }

    recursive fn parse_expression() -> Node..& {
        var node: Node..& = parse_term();
        loop {
            skip_whitespace();
            guard current == '+' || current == '-' else { break; }
            let op = current;
            advance();
            let right: Node..& = parse_term();
            node .= nodes.push(Node.BinaryOp { op: op, left: node, right: right });
        }
        return node;
    }

    fn parse_term() -> Node..& {
        var node: Node..& = parse_factor();
        loop {
            skip_whitespace();
            guard current == '*' || current == '/' || current == '%' else { break; }
            let op = current;
            advance();
            let right: Node..& = parse_factor();
            node .= nodes.push(Node.BinaryOp { op: op, left: node, right: right });
        }
        return node;
    }

    fn parse_factor() -> Node..& {
        skip_whitespace();
        if is_digit(current) { return parse_number(); }
        if is_letter(current) { return parse_variable(); }
        if current == '(' {
            advance();
            let node: Node..& = parse_expression();
            skip_whitespace();
            if current == ')' { advance(); }
            return node;
        }
        advance();
        return nodes.push(Node.Number { value: 0 });
    }

    fn parse_number() -> Node..& {
        let start = pos;
        while is_digit(current) { advance(); }
        let value, ok = parse_int(input[start..pos]);
        return nodes.push(Node.Number { value: if ok { value } else { 0 } });
    }

    fn parse_variable() -> Node..& {
        let start = pos;
        while is_letter(current) || is_digit(current) { advance(); }
        let name = input[start..pos];
        skip_whitespace();
        if current == '=' {
            advance();
            let expr: Node..& = parse_expression();
            return nodes.push(Node.Assignment { name: name, expr: expr });
        }
        return nodes.push(Node.Variable { name: name });
    }

    while current != EOF {
        skip_whitespace();
        if current == EOF { break; }
        expressions.push(parse_expression());
        skip_whitespace();
        while current == '\n' {
            advance();
            skip_whitespace();
        }
    }
}

fn run(b: Bench&) -> u32 {
    let n = config_i64(b.name, "operations");
    let text = generate_random_program(n);
    var result_val: u32 = 0;
    measure(b) {
        var nodes: Node..[>..] = [];
        var expressions: (Node..&)[>..] = [];
        parse(text, nodes, expressions);
        result_val += expressions.len as! u32;
        if expressions.len > 0 {
            match expressions[expressions.len - 1] {
                Assignment &a => { result_val += checksum_str(a.name); },
                _ => {},
            }
        }
    };
    return result_val;
}
