// lsp: fixture for test/lsp_protocol.py: a program importing
// roots_lib.goose, which lsp_protocol.py edits to stop importing it.
import roots_lib;

fn main() { print(roots_lib::helperfn(1)); }
