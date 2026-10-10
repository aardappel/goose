// Brainfuck::Array: a Brainfuck interpreter over a flat command array, with
// the matching bracket of every `[` and `]` looked up in a jump table built
// before the run.

namespace brainfuck_array;

import std;
import bench;

// One per command character; `,` (input) is a command the run rejects.
enum Cmd { Inc, Dec, Next, Prev, Open, Close, Print, Read }

// Runs the program in `text`. Returns 0, false for a program with no
// commands, with unbalanced brackets, or that reads input.
fn run_program(text: const u8[:]) -> u32, bool {
    var commands: Cmd[>..] = [];
    for c in text {
        match c {
            '+' => { commands.push(Cmd.Inc); },
            '-' => { commands.push(Cmd.Dec); },
            '>' => { commands.push(Cmd.Next); },
            '<' => { commands.push(Cmd.Prev); },
            '[' => { commands.push(Cmd.Open); },
            ']' => { commands.push(Cmd.Close); },
            '.' => { commands.push(Cmd.Print); },
            ',' => { commands.push(Cmd.Read); },
            _ => {},
        }
    }
    if commands.len == 0 { return 0, false; }

    // Jump targets and the program counter are unsigned, so the loop's
    // `pc < commands.len` is all the compiler needs to drop the bounds checks
    // on commands[pc] and jumps[pc].
    var jumps: u32[>..] = [];
    jumps.resize(commands.len, 0);
    var stack: i64[>..<] = [];
    for cmd, i in commands {
        match cmd {
            Open => { stack.push(i); },
            Close => {
                if stack.len == 0 { return 0, false; }
                let start = stack.pop();
                jumps[start] = i as u32;
                jumps[i] = start as u32;
            },
            _ => {},
        }
    }
    if stack.len != 0 { return 0, false; }

    var tape: u8[>..] = [];
    tape.resize(30000, 0);
    var pos = 0;
    var pc: u32 = 0;
    var result: u32 = 0;
    while pc < commands.len {
        match commands[pc] {
            Inc => { tape[pos] += 1; },
            Dec => { tape[pos] -= 1; },
            Next => {
                pos++;
                if pos >= tape.len { tape.push(0); }
            },
            Prev => { if pos > 0 { pos--; } },
            Open => { if tape[pos] == 0 { pc = jumps[pc]; } },
            Close => { if tape[pos] != 0 { pc = jumps[pc]; } },
            Print => { result = (result << 2) + tape[pos]; },
            Read => { return 0, false; },
        }
        pc++;
    }
    return result, true;
}

fn run(b: Bench&) -> u32 {
    let program = config_s(b.name, "program");
    let warmup_program = config_s(b.name, "warmup_program");
    var result: u32 = 0;
    fn warmup(i: i64) { run_program(warmup_program); }
    measure_with(b, warmup) { i =>
        let r, ok = run_program(program);
        if ok { result += r; }
    };
    return result;
}
