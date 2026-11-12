#include <cstdio>
#include <cstdlib>
int nb_selftest(int frames, const char* out_path);
int main(int argc, char** argv) {
    int r = nb_selftest(argc > 1 ? atoi(argv[1]) : 600, argc > 2 ? argv[2] : "selftest.json");
    return r;
}
