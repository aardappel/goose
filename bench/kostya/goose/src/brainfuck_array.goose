// Brainfuck::Array: a Brainfuck interpreter over a flat command array, with
// the matching bracket of every `[` and `]` looked up in a jump table built
// before the run.

namespace brainfuck_array;

import std;
import bench;

// The tape grows to the right as the head reaches its end. A growable array
// can only be a struct's last field, so it follows `pos`.
struct Tape { pos: i64, tape: u8[>..] }

fn new_tape() -> Tape { Tape { pos: 0, tape: [0; 30000] } }

fn get(t: Tape&) -> u8 { t.tape[t.pos] }

fn inc(t: Tape&) { t.tape[t.pos] += 1; }

fn dec(t: Tape&) { t.tape[t.pos] -= 1; }

fn advance(t: Tape&) {
    t.pos++;
    if t.pos >= t.tape.len { t.tape.push(0); }
}

fn devance(t: Tape&) {
    if t.pos > 0 { t.pos--; }
}

// The commands are fixed once parsed; the jump table is the growable tail.
struct Program { commands: u8[], jumps: i64[>..] }

// The program in `text`, or false for one with no commands or with
// unbalanced brackets.
fn new_program(text: const u8[:]) -> Program, bool {
    var program = Program {
        commands: text.filter() { c =>
            match c { '+', '-', '>', '<', '[', ']', '.', ',' => true, _ => false }
        },
        jumps: [],
    };
    if program.commands.len == 0 { return program, false; }

    // A zero per command: resize and push_n take the array's own variable,
    // and a struct's growable field grows by push.
    for c in program.commands { program.jumps.push(0); }
    var stack: i64[>..<] = [];
    for cmd, i in program.commands {
        match cmd {
            '[' => { stack.push(i); },
            ']' => {
                if stack.len == 0 { return program, false; }
                let start = stack.pop();
                program.jumps[start] = i;
                program.jumps[i] = start;
            },
            _ => {},
        }
    }
    return program, stack.len == 0;
}

// The program's output folded into a u32, or false for a program that reads
// input.
fn run(p: Program&) -> u32, bool {
    var tape = new_tape();
    var pc = 0;
    var result: u32 = 0;
    while pc < p.commands.len {
        match p.commands[pc] {
            '+' => { tape.inc(); },
            '-' => { tape.dec(); },
            '>' => { tape.advance(); },
            '<' => { tape.devance(); },
            '[' => { if tape.get() == 0 { pc = p.jumps[pc]; } },
            ']' => { if tape.get() != 0 { pc = p.jumps[pc]; } },
            '.' => { result = (result << 2) + tape.get(); },
            _ => { return 0, false; },
        }
        pc++;
    }
    return result, true;
}

fn run_program(source: const u8[:]) -> u32, bool {
    let program, ok = new_program(source);
    if !ok { return 0, false; }
    return program.run();
}

fn run(b: Bench&) -> u32 {
    let program_text = config_s(b.name, "program");
    let warmup_text = config_s(b.name, "warmup_program");
    var result_val: u32 = 0;
    fn warmup(i: i64) { run_program(warmup_text); }
    measure_with(b, warmup) { i =>
        let result, ok = run_program(program_text);
        if ok { result_val += result; }
    };
    return result_val;
}
