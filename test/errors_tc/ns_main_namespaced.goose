// error: program needs exactly one global fn main()
// The entry point is the global main; a namespaced root file declares it as
// `fn ::main()`.
namespace app;
fn main() { print(1); }
