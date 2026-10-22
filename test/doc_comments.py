"""Documentation comments (Lexer::doc): the run of // comment lines standing
on lines of their own, each directly below the one before, that ends on the
line above a declaration, a field or a variant is its documentation, which
`goose --dump-docs` prints with its indentation removed. A blank line, a block
comment or any token ends a run; a comment after code on its line starts
none; a banner followed by a blank line belongs to nothing."""
import tempfile
from pathlib import Path

import toolchain as tc

SOURCE = (
    "// A banner, followed by a blank line: nobody's.\n"
    "\n"
    "// Adds two numbers.\n"
    "//   The second line keeps its text, less its indentation.\n"
    "fn add(a: i64, b: i64) -> i64 { a + b }\n"
    "\n"
    "let one = 1; // A trailing comment belongs to no one.\n"
    "// The global two.\n"
    "let two = 2;\n"
    "\n"
    "    // Indented comments count too.\n"
    "export fn hello() {}\n"
    "\n"
    "// A point.\n"
    "struct Point {\n"
    "    // Its x.\n"
    "    x: i64,\n"
    "    y: i64, // Undocumented: the comment follows code.\n"
    "    // Not z's: a blank line follows.\n"
    "\n"
    "    z: i64,\n"
    "}\n"
    "\n"
    "// A shape.\n"
    "enum Shape {\n"
    "    // Round.\n"
    "    Circle { r: i64 },\n"
    "    Square,\n"
    "}\n"
    "\n"
    "/* A block comment is no documentation. */\n"
    "type Id = i64;\n"
    "\n"
    "// Not gap's: a block comment comes between.\n"
    "/* no */\n"
    "fn gap() {}\n"
    "\n"
    "// A square root, through the extern's string.\n"
    "extern fn root(x: f64) -> f64 = \"gs_sqrt\";\n"
    "\n"
    "// Not the block's: a block is no declaration.\n"
    "extern \"m\" {\n"
    "    // The cosine, a member of an extern block.\n"
    "    fn cos(x: f64) -> f64;\n"
    "    fn sine(x: f64) -> f64 = \"sin\"; // Undocumented.\n"
    "}\n"
    "\n"
    "// Main.\r\n"
    "fn main() { print(add(one, two)); hello(); gap(); }\r\n"
)

EXPECTED = (
    "fn add\n"
    "    // Adds two numbers.\n"
    "    //   The second line keeps its text, less its indentation.\n"
    "let two\n"
    "    // The global two.\n"
    "fn hello\n"
    "    // Indented comments count too.\n"
    "struct Point\n"
    "    // A point.\n"
    "field Point.x\n"
    "    // Its x.\n"
    "enum Shape\n"
    "    // A shape.\n"
    "variant Shape.Circle\n"
    "    // Round.\n"
    "fn root\n"
    "    // A square root, through the extern's string.\n"
    "fn cos\n"
    "    // The cosine, a member of an extern block.\n"
    "fn main\n"
    "    // Main.\n"
)


def check_doc_comments(exe, base):
    """Raises RuntimeError on a failure."""
    base = Path(base).resolve()
    base.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="doc-comments-", dir=base) as tmp:
        root = Path(tmp)
        tc.write_text(root / "docs.goose", SOURCE)
        code, out, err = tc.run_capture([exe, "--dump-docs", "docs.goose"], cwd=root)
        if code != 0 or out != EXPECTED:
            raise RuntimeError(f"documentation comments: --dump-docs (exit {code}) printed:\n"
                               f"{out}{err}\nnot:\n{EXPECTED}")
        # The program parses and checks as it did without the comments: they
        # are no part of the dump the roundtrip compares.
        code, out, err = tc.run_capture([exe, "check", "--roundtrip", "docs.goose"], cwd=root)
        if code != 0:
            raise RuntimeError(f"documentation comments: the program does not check:\n{out}{err}")
