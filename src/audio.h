// Compiler integration for the optional SDL3 audio module.
#pragma once

namespace goose {

#ifdef GOOSE_HAVE_AUDIO
inline constexpr bool have_audio = true;
#else
inline constexpr bool have_audio = false;
#endif

inline const char *no_audio_error = "this compiler was built without SDL3 audio; "
                                    "enable GOOSE_AUDIO, check out third_party/SDL and reconfigure";

inline string AudioLinkFile(const string &exedir, const string &style) {
    #ifdef GOOSE_AUDIO_LINK_PATH
        const char *built = GOOSE_AUDIO_LINK_PATH;
    #else
        const char *built = nullptr;
    #endif
    return NativeLinkFile("--audio-link", have_audio, no_audio_error, "audio",
                          "GOOSE_AUDIO_LINK", built, exedir, style);
}

}  // namespace goose
