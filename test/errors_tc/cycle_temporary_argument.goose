// error: spell calls into its recursive cycle with the non-fixed-size temporary copy(letter) as an argument
// The copy is a temporary of the statement, held on a data stack of this
// activation while the call runs, so every level would take one (§7.8).
var letter: u8[>..] = "a";
recursive fn spell(s: const u8[:], n: i64) -> i64 {
    if n == 0 { return 0; }
    letter[0] = letter[0] + 1;
    return s[0] + spell(copy(letter), n - 1);
}
fn main() { print(spell(copy(letter), 3)); }
