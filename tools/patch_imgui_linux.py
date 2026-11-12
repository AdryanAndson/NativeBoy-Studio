# The SDL2 ImGui backend includes SDL_syswm.h, which on Linux pulls X11 headers
# (libx11-dev) that most desktop installs do not have. The backend only needs it on
# Windows/macOS, so the copy shipped in the Linux SDK guards it.
import sys
p = sys.argv[1]
s = open(p).read()
s = s.replace("#include <SDL_syswm.h>", "#if defined(_WIN32) || defined(__APPLE__)\n#include <SDL_syswm.h>\n#endif", 1)
old = """    SDL_SysWMinfo info;
    SDL_VERSION(&info.version);
    if (SDL_GetWindowWMInfo(window, &info))
    {"""
i = s.index(old)
j = s.index("    }\n", i) + len("    }\n")
s = s[:i] + "#if defined(_WIN32) || defined(__APPLE__)\n" + s[i:j] + "#endif\n" + s[j:]
open(p, "w").write(s)
print("patched", p)
