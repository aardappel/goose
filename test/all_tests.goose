// Every runnable test in one program. Imported files' `fn main` entries are
// ignored (§11.1), so each test stays runnable on its own while this root
// calls the bodies in order. Top-level names are globally unique across the
// test files to allow this merge.
import typecheck;
import roots;
import spec_examples;
import codegen_exec;
import codegen_threads;
import parser_all;
import optimize;

fn main() {
    main_typecheck();
    main_roots();
    main_spec_examples();
    main_codegen_exec();
    main_codegen_threads();
    main_parser_all();
    main_optimize();     // Last: it ends in a deliberate runtime abort.
}
