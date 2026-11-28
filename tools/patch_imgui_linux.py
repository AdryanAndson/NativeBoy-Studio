# The SDL2 ImGui backend includes SDL_syswm.h, which on Linux pulls X11 headers
# (libx11-dev) that most desktop installs do not have. The backend only needs it on
# Windows/macOS, so the copy shipped in the Linux SDK guards it (every block that uses
# SDL_SysWMinfo: the main window, and the extra windows of the docking version).
import sys
p = sys.argv[1]
s = open(p).read()
s = s.replace("#include <SDL_syswm.h>", "#if defined(_WIN32) || defined(__APPLE__)\n#include <SDL_syswm.h>\n#endif", 1)
pos = 0
n = 0
while True:
    i = s.find("    SDL_SysWMinfo info;\n", pos)
    if i < 0:
        break
    # the block ends with the closing brace of the "if (SDL_GetWindowWMInfo(...))" at the same indentation
    k = s.index("SDL_GetWindowWMInfo", i)
    j = s.index("\n    }\n", k) + len("\n    }\n")
    s = s[:i] + "#if defined(_WIN32) || defined(__APPLE__)\n" + s[i:j] + "#endif\n" + s[j:]
    pos = j + len("#if defined(_WIN32) || defined(__APPLE__)\n#endif\n")
    n += 1
if n == 0:
    sys.exit("patch_imgui_linux: SDL_SysWMinfo block not found")
open(p, "w").write(s)
print("patched", p, n, "blocks")
