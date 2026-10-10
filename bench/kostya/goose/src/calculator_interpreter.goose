// Calculator::Interpreter: evaluates the Calculator::Ast program's tree with
// a fresh interpreter on every run. The variables live in a hash map keyed
// by views of the names stored in the tree, so an assignment copies no name.

namespace calculator_interpreter;

import std;
import dictionary;
import bench;
import calculator_ast;

// Rust's wrapping_add/sub/mul: unsigned arithmetic wraps in every build,
// where signed arithmetic wraps only in release (spec 6.2).
fn wrapping_add(a: i64, b: i64) -> i64 { (a as! u64 + b as! u64) as! i64 }
fn wrapping_sub(a: i64, b: i64) -> i64 { (a as! u64 - b as! u64) as! i64 }
fn wrapping_mul(a: i64, b: i64) -> i64 { (a as! u64 * b as! u64) as! i64 }

fn simple_div(a: i64, b: i64) -> i64 {
    if b == 0 { return 0; }
    if (a >= 0 && b > 0) || (a < 0 && b < 0) { return a / b; }
    return -(abs(a) / abs(b));
}

fn simple_mod(a: i64, b: i64) -> i64 {
    if b == 0 { return 0; }
    return a - simple_div(a, b) * b;
}

// Evaluates every expression in order with a fresh set of variables, and
// returns the value of the last one.
fn interpret(expressions: (calculator_ast::Node..&)[:]) -> i64 {
    var variables = dictionary<const u8[:], i64> {};

    recursive fn evaluate(node: calculator_ast::Node..&) -> i64 {
        match node {
            Number &n => n.value,
            Variable &v => {
                let value = variables.get(v.name);
                if value { value } else { 0 }
            },
            BinaryOp &op => {
                let left = evaluate(op.left);
                let right = evaluate(op.right);
                match op.op {
                    '+' => wrapping_add(left, right),
                    '-' => wrapping_sub(left, right),
                    '*' => wrapping_mul(left, right),
                    '/' => simple_div(left, right),
                    '%' => simple_mod(left, right),
                    _ => 0,
                }
            },
            Assignment &a => {
                let value = evaluate(a.expr);
                variables.insert(a.name, value);
                value
            },
        }
    }

    var result = 0;
    for expr in expressions { result = evaluate(expr); }
    return result;
}

fn run(b: Bench&) -> u32 {
    let n = config_i64(b.name, "operations");
    let text = calculator_ast::generate_random_program(n);
    var nodes: calculator_ast::Node..[>..] = [];
    var expressions: (calculator_ast::Node..&)[>..] = [];
    calculator_ast::parse(text, nodes, expressions);
    var result_val: u32 = 0;
    measure(b) {
        result_val += interpret(expressions) as! u32;
    };
    return result_val;
}
