// Calculator::Interpreter: evaluates the Calculator::Ast program's tree with
// a fresh interpreter on every run. The variables are a hash map keyed by
// views of the names stored in the tree.

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

// --- the interpreter ------------------------------------------------------------

struct Interpreter { variables: dictionary<const u8[:], i64> }

fn new_interpreter() -> Interpreter {
    return Interpreter { variables: dictionary<const u8[:], i64> {} };
}

recursive fn evaluate(interp: Interpreter&, node: calculator_ast::Node..&) -> i64 {
    match node {
        Number &n => n.value,
        Variable &v => {
            let value = interp.variables.get(v.name);
            if value { value } else { 0 }
        },
        BinaryOp &op => {
            let left_val = interp.evaluate(op.left);
            let right_val = interp.evaluate(op.right);
            match op.op {
                '+' => wrapping_add(left_val, right_val),
                '-' => wrapping_sub(left_val, right_val),
                '*' => wrapping_mul(left_val, right_val),
                '/' => simple_div(left_val, right_val),
                '%' => simple_mod(left_val, right_val),
                _ => 0,
            }
        },
        Assignment &a => {
            let value = interp.evaluate(a.expr);
            interp.variables.insert(a.name, value);
            value
        },
    }
}

fn run(interp: Interpreter&, expressions: (calculator_ast::Node..&)[:]) -> i64 {
    var result = 0;
    for expr in expressions { result = interp.evaluate(expr); }
    return result;
}

// --- Calculator::Interpreter ----------------------------------------------------

fn run(b: Bench&) -> u32 {
    let n = config_i64(b.name, "operations");
    // The tree a Calculator::Ast with this many operations builds in its
    // prepare and first run.
    let text = calculator_ast::generate_random_program(n);
    var nodes: calculator_ast::Node..[>..] = [];
    var parser = calculator_ast::new_parser(text);
    let ast = calculator_ast::parse(parser, nodes);
    var result_val: u32 = 0;
    measure(b) {
        var interpreter = new_interpreter();
        let result = interpreter.run(ast);
        result_val += result as! u32;
    };
    return result_val;
}
