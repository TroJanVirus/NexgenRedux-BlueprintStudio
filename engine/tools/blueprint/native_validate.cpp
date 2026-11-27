// Optional desktop compiler front end: uses Nexgen's actual bindings and AngelScript engine.
// It compiles stdin, but does not create a Screen or execute its lifecycle methods.
#include "../../tests/rig.h"
#include <stdio.h>
#include <string>
#include <string.h>

int main(int argc, char** argv)
{
    const char* kind = argc > 1 ? argv[1] : "Screen";
    if (strcmp(kind, "Screen") != 0 && strcmp(kind, "Behaviour") != 0) return 2;
    std::string source;
    char buf[4096];
    size_t got;
    while ((got = fread(buf, 1, sizeof(buf), stdin)) != 0)
    {
        source.append(buf, got);
        if (source.size() > 2 * 1024 * 1024) return 2;
    }
    if (ferror(stdin)) return 2;
    nx::test::Rig r;
    if (!r.ok) return 3;
    if (!r.host.compile("blueprint", "blueprint.as", source.data(), source.size())) return 1;
    const char* base = strcmp(kind, "Screen") == 0 ? "Screen" : "IBehaviour";
    if (!r.host.findClass("blueprint", base))
    {
        fprintf(stderr, "No concrete %s class found.\n", kind);
        return 1;
    }
    printf("Nexgen native compile passed (%s).\n", kind);
    return 0;
}
