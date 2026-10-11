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

// The tree's top-level expressions are the growable last field. Its nodes lie
// in a node pool: a struct holds one growable array, so the pool is a local
// of the code that makes the benchmark, declared before it and handed to
// prepare.
struct CalculatorInterpreter { n: i64, result_val: u32, ast: (calculator_ast::Node..&)[>..] }

fn new_calculator_interpreter() -> CalculatorInterpreter {
    CalculatorInterpreter { n: config_i64("Calculator::Interpreter", "operations"), result_val: 0,
                            ast: [] }
}

// The tree a Calculator::Ast with this many operations builds in its prepare
// and first run. Its run keeps the tree in locals of the run, so the program
// is parsed here, into `nodes`.
fn prepare(s: CalculatorInterpreter&, nodes: calculator_ast::Node..[>..]&) {
    var ast_bench = calculator_ast::new_calculator_ast();
    ast_bench.n = s.n;
    calculator_ast::prepare(ast_bench);
    var parser = calculator_ast::new_parser(ast_bench.text);
    let expressions = calculator_ast::parse(parser, nodes);
    s.ast = expressions;
}

fn run(s: CalculatorInterpreter&, iteration_id: i64) {
    var interpreter = new_interpreter();
    let result = interpreter.run(s.ast);
    s.result_val += result as! u32;
}

fn checksum(s: CalculatorInterpreter&) -> u32 { s.result_val }
