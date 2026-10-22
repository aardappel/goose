// lsp: fixture for test/lsp_protocol.py: a module with an error, which
// roots_app.goose and roots_app2.goose import, opened on its own and
// beside them as roots come and go (docs/design/language_server.md §4).
namespace roots_lib;

fn helperfn(x: i64) -> i64 { x + "s" }
