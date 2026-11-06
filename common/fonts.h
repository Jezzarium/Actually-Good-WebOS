#pragma once

#include <string>

#include <sys/stat.h>

// The UI font: SF Pro Display when it's in the app's assets/fonts, LG's own UI
// font otherwise. Apple's license doesn't allow redistributing SF Pro, so it
// isn't in git; tools/get-sf-pro.sh downloads it into each app's assets.
// (Display rather than Text: every size here is above Apple's 20 pt cutoff.)
namespace fonts {

enum class Weight { Regular, Semibold };

inline std::string ui(const std::string &assets, Weight weight) {
    const bool semi = weight == Weight::Semibold;
    std::string sf = assets + "/fonts/SF-Pro-Display-" + (semi ? "Semibold" : "Regular") + ".otf";
    struct stat st;
    if (stat(sf.c_str(), &st) == 0) return sf;
#ifdef LGTV_WEBOS
    return std::string("/usr/share/fonts/LG_Smart_UI-") + (semi ? "SemiBold" : "Regular") + ".ttf";
#else
    return std::string("/usr/share/fonts/liberation/LiberationSans-") + (semi ? "Bold" : "Regular") + ".ttf";
#endif
}

}  // namespace fonts
