// A stack-machine bytecode interpreter. The program is an array of
// instructions, each a fixed-mode enum value (a 1-byte opcode plus room for
// one operand), and the dispatch loop is a `match` over it: no function
// pointers, no switch on a byte you then have to decode, and a jump target
// is just an index into the same array. Two programs are assembled by
// hand: a countdown loop and an iterative factorial.
//
// Build and run (from this directory):
//   goose -o vm.c 19_vm.goose && cl vm.c && vm

import std;

enum Op {
    Push { v: i64 },                     // push a constant
    Load { slot: u8 },                   // push local variable `slot`
    Store { slot: u8 },                  // pop into local `slot`
    Add, Sub, Mul,                       // pop two, push the result
    Dup,                                 // duplicate the top
    Jump { to: u16 },                    // jump to instruction `to`
    JumpIfZero { to: u16 },              // pop; jump if it was zero
    Print,                               // pop and print
    Halt,
}

struct Vm { stack: i64[>..<] }

fn op_name(o: Op) -> u8[:] {
    match o {
        Push p => "push", Load l => "load", Store s => "store", Add => "add", Sub => "sub",
        Mul => "mul", Dup => "dup", Jump j => "jump", JumpIfZero j => "jz",
        Print => "print", Halt => "halt",
    }
}

// A listing: index, mnemonic, and the operand if there is one.
fn disassemble(code: Op[:]) {
    for o, i in code {
        var l: u8[>..] = [];
        format_int(l, i, 10, 3, ' ');
        format(l, "  ", op_name(o));
        match o {
            Push p => format(l, " ", p.v),
            Load v => format(l, " ", v.slot),
            Store v => format(l, " ", v.slot),
            Jump j => format(l, " ", j.to),
            JumpIfZero j => format(l, " ", j.to),
            _ => {},
        }
        print(l);
    }
}

// Runs code from instruction 0 until Halt; returns the number of
// instructions executed. The operand stack is a grow-shrink array, the
// locals a small fixed array. The `match` is a switch on the tag byte with
// the operand read in place, and a jump is an index store.
fn run(code: Op[:]) -> i64 {
    var stack: i64[>..<] = [];
    var locals: i64[8] = [0; 8];
    var pc = 0;
    var steps = 0;
    loop {
        let op = code[pc];
        pc++;
        steps++;
        match op {
            Push p => { stack.push(p.v); },
            Load l => { stack.push(locals[l.slot]); },
            Store s => { locals[s.slot] = stack.pop(); },
            Add => { let b = stack.pop(); let a = stack.pop(); stack.push(a + b); },
            Sub => { let b = stack.pop(); let a = stack.pop(); stack.push(a - b); },
            Mul => { let b = stack.pop(); let a = stack.pop(); stack.push(a * b); },
            Dup => { let a = stack.pop(); stack.push(a); stack.push(a); },
            Jump j => { pc = j.to; },
            JumpIfZero j => { if stack.pop() == 0 { pc = j.to; } },
            Print => { print("  out: ", stack.pop()); },
            Halt => { break; },
        }
    }
    return steps;
}

fn main() {
    // countdown: n = 3; while n != 0 { print n; n = n - 1 }
    var countdown: Op[>..] = [];
    countdown.push(Op.Push { v: 3 });
    countdown.push(Op.Store { slot: 0 });
    countdown.push(Op.Load { slot: 0 });         // 2: loop head
    countdown.push(Op.JumpIfZero { to: 10 });
    countdown.push(Op.Load { slot: 0 });
    countdown.push(Op.Print);
    countdown.push(Op.Load { slot: 0 });
    countdown.push(Op.Push { v: 1 });
    countdown.push(Op.Sub);
    countdown.push(Op.Store { slot: 0 });
    countdown.push(Op.Jump { to: 2 });           // 10 is past this: Halt below
    countdown.push(Op.Halt);
    // A wrong target above: index 10 is the Jump, not the Halt. Patch it,
    // as an assembler's second pass would.
    countdown[3] = Op.JumpIfZero { to: 11 };
    print("countdown:");
    disassemble(countdown);
    let steps = run(countdown);
    print("ran ", steps, " instructions");

    // factorial: acc = 1; n = 10; while n != 0 { acc = acc * n; n = n - 1 }; print acc
    var fact: Op[>..] = [];
    fact.push(Op.Push { v: 1 });
    fact.push(Op.Store { slot: 1 });             // acc
    fact.push(Op.Push { v: 10 });
    fact.push(Op.Store { slot: 0 });             // n
    fact.push(Op.Load { slot: 0 });              // 4: loop head
    fact.push(Op.JumpIfZero { to: 15 });
    fact.push(Op.Load { slot: 1 });
    fact.push(Op.Load { slot: 0 });
    fact.push(Op.Mul);
    fact.push(Op.Store { slot: 1 });
    fact.push(Op.Load { slot: 0 });
    fact.push(Op.Push { v: 1 });
    fact.push(Op.Sub);
    fact.push(Op.Store { slot: 0 });
    fact.push(Op.Jump { to: 4 });
    fact.push(Op.Load { slot: 1 });              // 15
    fact.push(Op.Print);
    fact.push(Op.Halt);
    print("factorial:");
    disassemble(fact);
    let steps2 = run(fact);
    print("ran ", steps2, " instructions");
    print("an instruction is ", 1 + 8, " bytes: a tag and the largest operand");
}
