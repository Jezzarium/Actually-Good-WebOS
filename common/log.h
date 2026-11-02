#pragma once

#include <cstdio>

// LG's SDL port swallows SDL_Log; write straight to stderr.
#define LOG(...) (std::fprintf(stderr, __VA_ARGS__), std::fputc('\n', stderr))
