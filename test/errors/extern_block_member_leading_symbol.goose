// error: a member's symbol follows its signature: fn name(...) = "SDL_Delay";
extern "SDL2" {
    "SDL_Delay" fn delay(ms: u32);
}
fn main() {}
