// Calculator::Ast: a generated program of assignments over + - * / % and
// parentheses, parsed by recursive descent into an AST on every run.
//
// The nodes are a variable-mode enum in a grow-only pool, each as big as its
// variant, linked to its children by 4-byte self-relative references. A
// variable's name is stored inline in its node. A run's node pool and parser
// are locals of the run, so the previous run's tree is freed when the run's
// scope ends.

namespace calculator_ast;

import std;
import bench;
import helper;

enum Node {
    Number { value: i64 },
    Variable { name: u8[] },
    BinaryOp { op: u8, left: Node..&<u32>, right: Node..&<u32> },
    Assignment { name: u8[], expr: Node..&<u32> },
}

let CHAR_EOF: u8 = 0;

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

// --- the parser -----------------------------------------------------------------

struct Parser {
    input: const u8[:],
    pos: i64,
    len: i64,
    current_byte: u8,
    expressions: (Node..&)[>..],
}

fn new_parser(input: const u8[:]) -> Parser {
    let len = input.len;
    let current_byte = if len > 0 { input[0] } else { CHAR_EOF };
    return Parser { input: input, pos: 0, len: len, current_byte: current_byte, expressions: [] };
}

// The functions that build nodes take the pool the nodes live in, which
// outlives the parser. It is a parameter rather than a field of the parser
// because inside the recursive cycle the checker accepts links stored into a
// pool handed to it that way, not into one reached through a field.

// Returns the top-level expressions, which stay in the parser.
fn parse(p: Parser&, nodes: Node..[>..]&) -> (Node..&)[:] {
    while p.current_byte != CHAR_EOF {
        p.skip_whitespace();
        if p.current_byte == CHAR_EOF { break; }

        let expr: Node..& = p.parse_expression(nodes);
        p.expressions.push(expr);

        p.skip_whitespace();
        while p.current_byte == '\n' {
            p.advance();
            p.skip_whitespace();
        }
    }
    return p.expressions;
}

recursive fn parse_expression(p: Parser&, nodes: Node..[>..]&) -> Node..& {
    let node: Node..& = p.parse_term(nodes);
    return p.parse_expression_rest(nodes, node);
}

fn parse_expression_rest(p: Parser&, nodes: Node..[>..]&, left_node: Node..&) -> Node..& {
    var current_node: Node..& = left_node;
    loop {
        p.skip_whitespace();
        if p.current_byte == '+' || p.current_byte == '-' {
            let op = p.current_byte;
            p.advance();
            let right: Node..& = p.parse_term(nodes);
            current_node .= nodes.push(Node.BinaryOp { op: op, left: current_node, right: right });
        } else {
            break;
        }
    }
    return current_node;
}

fn parse_term(p: Parser&, nodes: Node..[>..]&) -> Node..& {
    let node: Node..& = p.parse_factor(nodes);
    return p.parse_term_rest(nodes, node);
}

fn parse_term_rest(p: Parser&, nodes: Node..[>..]&, left_node: Node..&) -> Node..& {
    var current_node: Node..& = left_node;
    loop {
        p.skip_whitespace();
        if p.current_byte == '*' || p.current_byte == '/' || p.current_byte == '%' {
            let op = p.current_byte;
            p.advance();
            let right: Node..& = p.parse_factor(nodes);
            current_node .= nodes.push(Node.BinaryOp { op: op, left: current_node, right: right });
        } else {
            break;
        }
    }
    return current_node;
}

fn parse_factor(p: Parser&, nodes: Node..[>..]&) -> Node..& {
    p.skip_whitespace();
    if p.is_digit(p.current_byte) {
        return p.parse_number(nodes);
    } else if p.is_letter(p.current_byte) {
        return p.parse_variable(nodes);
    } else if p.current_byte == '(' {
        p.advance();
        let node: Node..& = p.parse_expression(nodes);
        p.skip_whitespace();
        if p.current_byte == ')' { p.advance(); }
        return node;
    } else {
        p.advance();
        return nodes.push(Node.Number { value: 0 });
    }
}

fn parse_number(p: Parser&, nodes: Node..[>..]&) -> Node..& {
    let start = p.pos;
    while p.is_digit(p.current_byte) { p.advance(); }

    let num_str = p.input[start..p.pos];
    let n, ok = parse_int(num_str);
    return nodes.push(Node.Number { value: if ok { n } else { 0 } });
}

fn parse_variable(p: Parser&, nodes: Node..[>..]&) -> Node..& {
    let start = p.pos;
    while p.is_letter(p.current_byte) || p.is_digit(p.current_byte) { p.advance(); }

    let var_name = p.input[start..p.pos];

    p.skip_whitespace();
    if p.current_byte == '=' {
        p.advance();
        let expr: Node..& = p.parse_expression(nodes);
        return nodes.push(Node.Assignment { name: var_name, expr: expr });
    }

    return nodes.push(Node.Variable { name: var_name });
}

fn advance(p: Parser&) {
    p.pos++;
    if p.pos >= p.len {
        p.current_byte = CHAR_EOF;
    } else {
        p.current_byte = p.input[p.pos];
    }
}

fn skip_whitespace(p: Parser&) {
    while p.is_whitespace(p.current_byte) { p.advance(); }
}

fn is_digit(p: Parser&, byte: u8) -> bool { byte >= '0' && byte <= '9' }

fn is_letter(p: Parser&, byte: u8) -> bool {
    (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z')
}

fn is_whitespace(p: Parser&, byte: u8) -> bool {
    byte == ' ' || byte == '\t' || byte == '\n' || byte == '\r'
}

// --- Calculator::Ast ------------------------------------------------------------

fn run(b: Bench&) -> u32 {
    let n = config_i64(b.name, "operations");
    let text = generate_random_program(n);
    var result_val: u32 = 0;
    measure(b) {
        var nodes: Node..[>..] = [];
        var parser = new_parser(text);
        let expressions = parser.parse(nodes);
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
