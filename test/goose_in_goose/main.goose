import .platform;
import .command;
import .context;
import .model;
import .lexer;
import .parser;
import .driver;
import .types;
import .generics;
import .checker;
import .runtime;
import .emitter;
import .render;

fn compile_command(ctx: Compiler&, mode: i64, path: const u8[:], output: const u8[:]) {
    if mode == COMMAND_TOKENS {
        load_file(ctx, path);
        for t in ctx.tokens { print(t.line, ":", t.column, " ", t.kind, " ", t.text); }
        return;
    }
    load_program(ctx, path, "stdlib");
    register_declarations(ctx);

    if mode == COMMAND_EMIT {
        check_program(ctx); generate_c(ctx);
        if !write_file(output, ctx.c_final) { write_stderr("cannot write C output\n"); exit(1); }
        return;
    }

    if mode == COMMAND_CHECK {
        check_program(ctx);
        print("checked ", ctx.specializations.len, " specializations, ", ctx.types.len, " types");
        return;
    }
    print("parsed ", ctx.sources.len, " files, ", ctx.tokens.len - ctx.sources.len, " tokens, ", ctx.nodes.len, " nodes, ", ctx.declarations.len, " declarations");
}

fn main() { run_commands(); }
