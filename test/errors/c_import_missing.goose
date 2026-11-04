// error: cannot import the C header no_such_header.h
// `import c` reads the header where the importing file is, or in the
// sidecar's c_include directories (docs/reference.md, "import c").
import c "no_such_header.h";
fn main() {}
