// error: cis_take: not imported from c_import_skipped.h (parameter 1, a union by value)
// A C name the module left out is an error where it is used, saying why
// (docs/reference.md, "import c").
import c "c_import_skipped.h";
fn main() { cis_take(0); }
