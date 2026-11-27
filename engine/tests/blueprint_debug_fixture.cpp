// Desktop-only executable for the visual debugger integration test.
#include "rig.h"
#include "blueprint_fixtures.h"
#include "script/debugger.h"
#include <stdlib.h>
#include <string.h>

int main(int argc, char** argv)
{
    if (argc != 2) return 2;
    const int port = atoi(argv[1]);
    if (port < 1 || port > 65535) return 2;
    nx::test::Rig r;
    if (!r.ok) return 3;
    const char* source = nx::test::blueprint::gameplay_debug;
    r.host.setSource("gameplay.as", "showcase/gameplay/main.as");
    if (!r.host.compile("blueprint", "gameplay.as", source, strlen(source))) return 4;
    asITypeInfo* type = r.host.findClass("blueprint", "Screen");
    asIScriptObject* object = type ? r.host.create(type) : nullptr;
    if (!object) return 5;
    nx::ScriptDebugger debugger;
    if (!debugger.start(r.host, static_cast<nx::u16>(port), true)) { r.host.release(object); return 6; }
    debugger.waitForReady();
    const bool entered = r.host.callMethod(object, type->GetMethodByDecl("void onEnter()"));
    const bool updated = r.host.callMethod(object, type->GetMethodByDecl("void update(float)"), 1.0f);
    debugger.update();
    const bool saved = strcmp(r.save.get("bp_dispatch_total", "missing"), "3") == 0;
    debugger.stop();
    r.host.release(object);
    return entered && updated && saved ? 0 : 7;
}
