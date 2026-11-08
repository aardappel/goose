// error: expression expected, found 'const'
fn main() { let x = const if true { 1 } else { 2 }; print(x); }
