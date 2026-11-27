#include "test.h"
#include "rig.h"
#include "blueprint_fixtures.h"
#include "scene/world.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

using namespace nx;
using namespace nx::test;

namespace
{
    bool compile(ScriptHost& host, const char* text)
    {
        return host.compile("blueprint", "blueprint.as", text, strlen(text));
    }
}

NX_TEST(blueprintCatalogueMatchesNativeBindings)
{
    Rig r;
    NX_EXPECT(r.ok);
    for (const char* code : blueprint::catalogue)
    {
        NX_EXPECT(compile(r.host, code));
        r.host.discard("blueprint");
        r.host.collectAll();
    }
}

NX_TEST(blueprintScreenEventsAndTypedFunctions)
{
    Rig r;
    NX_EXPECT(r.ok);
    World world(r.assets, r.host, "blueprint", 64, 32);
    world.space().init(&r.solids, &r.audio);
    r.services.world = &world;
    if (!compile(r.host, blueprint::arcade)) { NX_EXPECT(false); return; }
    asITypeInfo* type = r.host.findClass("blueprint", "Screen");
    if (!type) { NX_EXPECT(false); return; }
    asIScriptObject* object = r.host.create(type);
    if (!object) { NX_EXPECT(false); return; }
    NX_EXPECT(r.host.callMethod(object, type->GetMethodByDecl("void onEnter()")));
    const u32 player = world.findNamed("Player");
    NX_EXPECT(player != 0);
    NX_EXPECT(r.host.callMethod(object, type->GetMethodByDecl("void update(float)"), 1.0f));
    const World::Entity* e = world.find(player);
    NX_EXPECT(e && fabsf(e->rotation - 45) < 0.01f);
    r.input.simulate(Action::Accept);
    r.input.update(r.platform.state, 1.0f / 60);
    NX_EXPECT(r.host.callMethod(object, type->GetMethodByDecl("void update(float)"), 1.0f / 60));
    NX_EXPECT(strcmp(r.save.get("bp_score", "missing"), "1") == 0);
    r.device->beginFrame({});
    r.canvas.begin();
    NX_EXPECT(r.host.callMethod(object, type->GetMethodByDecl("void draw(Canvas@)"), &r.canvas));
    r.canvas.end();
    r.device->endFrame();
    r.host.release(object);
    r.services.world = nullptr;
}

NX_TEST(blueprintBehaviourUsesEngineLifecycle)
{
    Rig r;
    NX_EXPECT(r.ok);
    World world(r.assets, r.host, "blueprint", 64, 32);
    world.space().init(&r.solids, &r.audio);
    r.services.world = &world;
    std::string source = blueprint::spinner;
    source += R"(
        void setup() {
            Entity@ a = Scene::spawn(Vec2(0,0));
            a.name = "spinner";
            a.circle(10, 1, 1 << 2);
            a.add(BP_Spinner());
            Entity@ b = Scene::spawn(Vec2(0,0));
            b.name = "other";
            b.circle(10, 2, 0);
        }
    )";
    if (!compile(r.host, source.c_str())) { NX_EXPECT(false); return; }
    NX_EXPECT(r.host.call(r.host.find("blueprint", "void setup()")));
    NX_EXPECT(world.update(1.0f / 60));
    NX_EXPECT(world.update(1.0f / 60));
    NX_EXPECT(strcmp(r.save.get("bp_started", "missing"), "yes") == 0);
    NX_EXPECT(strcmp(r.save.get("bp_hits", "missing"), "1") == 0);
    NX_EXPECT(strcmp(r.save.get("bp_other", "missing"), "other") == 0);
    r.services.world = nullptr;
}

NX_TEST(blueprintFlowAndStateArePerInstance)
{
    Rig r;
    NX_EXPECT(r.ok);
    if (!compile(r.host, blueprint::flow)) { NX_EXPECT(false); return; }
    asITypeInfo* type = r.host.findClass("blueprint", "Screen");
    if (!type) { NX_EXPECT(false); return; }
    asIScriptObject* first = r.host.create(type);
    asIScriptObject* second = r.host.create(type);
    if (!first || !second) { NX_EXPECT(false); r.host.release(first); r.host.release(second); return; }
    auto* enter = type->GetMethodByDecl("void onEnter()");
    NX_EXPECT(r.host.callMethod(first, enter));
    NX_EXPECT(strcmp(r.save.get("bp_count", "missing"), "4") == 0);
    NX_EXPECT(strcmp(r.save.get("bp_flip", "missing"), "A") == 0);
    r.save.set("bp_flip", "unchanged");
    NX_EXPECT(r.host.callMethod(first, enter));
    NX_EXPECT(strcmp(r.save.get("bp_count", "missing"), "8") == 0);
    NX_EXPECT(strcmp(r.save.get("bp_flip", "missing"), "unchanged") == 0);
    NX_EXPECT(r.host.callMethod(second, enter));
    NX_EXPECT(strcmp(r.save.get("bp_count", "missing"), "4") == 0);
    NX_EXPECT(strcmp(r.save.get("bp_flip", "missing"), "A") == 0);
    r.host.release(first);
    r.host.release(second);
}

NX_TEST(blueprintLatentExecutionArraysAndReusableComponents)
{
    Rig r;
    NX_EXPECT(r.ok);
    World world(r.assets, r.host, "blueprint", 64, 32);
    world.space().init(&r.solids, &r.audio);
    r.services.world = &world;
    if (!compile(r.host, blueprint::advanced)) { NX_EXPECT(false); return; }
    asITypeInfo* type = r.host.findClass("blueprint", "Screen");
    if (!type) { NX_EXPECT(false); return; }
    asIScriptObject* object = r.host.create(type);
    if (!object) { NX_EXPECT(false); return; }
    NX_EXPECT(r.host.callMethod(object, type->GetMethodByDecl("void onEnter()")));
    NX_EXPECT(world.update(1.0f / 60));
    NX_EXPECT(strcmp(r.save.get("bp_started", "missing"), "yes") == 0);
    auto* update = type->GetMethodByDecl("void update(float)");
    NX_EXPECT(strcmp(r.save.get("bp_delayed", "missing"), "missing") == 0);
    NX_EXPECT(r.host.callMethod(object, update, 0.02f));
    NX_EXPECT(strcmp(r.save.get("bp_delayed", "missing"), "missing") == 0);
    NX_EXPECT(r.host.callMethod(object, update, 0.04f));
    NX_EXPECT(strcmp(r.save.get("bp_delayed", "missing"), "7") == 0);
    r.save.set("bp_delayed", "once");
    NX_EXPECT(r.host.callMethod(object, update, 0.1f));
    NX_EXPECT(strcmp(r.save.get("bp_delayed", "missing"), "once") == 0);
    r.host.release(object);
    r.services.world = nullptr;
}

NX_TEST(blueprintScreenLoadsThroughNormalScreenManager)
{
    Rig r;
    NX_EXPECT(r.ok);
    r.platform.files["showcase/main/screen.json"] = "{\"title\":\"Blueprint Arcade\"}";
    r.platform.files["showcase/main/main.as"] = blueprint::arcade;
    if (!r.screens.init(r.services, "showcase", "main")) { NX_EXPECT(false); return; }
    r.step(3);
    NX_EXPECT(!r.screens.failed());
    r.press(Action::Accept);
    NX_EXPECT(!r.screens.failed());
    NX_EXPECT(strcmp(r.save.get("bp_score", "missing"), "1") == 0);
    r.press(Action::Accept);
    NX_EXPECT(strcmp(r.save.get("bp_score", "missing"), "2") == 0);
    r.screens.restart();
    r.step(3);
    NX_EXPECT(!r.screens.failed());
    r.press(Action::Accept);
    NX_EXPECT(strcmp(r.save.get("bp_score", "missing"), "1") == 0);
}

NX_TEST(blueprintTimelineAndDispatcher)
{
    Rig r;
    NX_EXPECT(r.ok);
    if (!compile(r.host, blueprint::gameplay)) { NX_EXPECT(false); return; }
    asITypeInfo* type = r.host.findClass("blueprint", "Screen");
    asIScriptObject* object = type ? r.host.create(type) : nullptr;
    if (!object) { NX_EXPECT(false); return; }
    NX_EXPECT(r.host.callMethod(object, type->GetMethodByDecl("void onEnter()")));
    NX_EXPECT(strcmp(r.save.get("bp_dispatch_total", "missing"), "3") == 0);
    auto* update = type->GetMethodByDecl("void update(float)");
    NX_EXPECT(r.host.callMethod(object, update, 0.25f));
    NX_EXPECT(fabsf(static_cast<float>(atof(r.save.get("bp_progress", "-1"))) - 2.5f) < 0.001f);
    NX_EXPECT(strcmp(r.save.get("bp_timeline_done", "missing"), "missing") == 0);
    NX_EXPECT(r.host.callMethod(object, update, 0.75f));
    NX_EXPECT(fabsf(static_cast<float>(atof(r.save.get("bp_progress", "-1"))) - 10.0f) < 0.001f);
    NX_EXPECT(strcmp(r.save.get("bp_timeline_done", "missing"), "yes") == 0);
    r.save.set("bp_progress", "stopped");
    NX_EXPECT(r.host.callMethod(object, update, 0.2f));
    NX_EXPECT(strcmp(r.save.get("bp_progress", "missing"), "stopped") == 0);
    r.host.release(object);
}

NX_TEST(blueprintInheritsAndCallsParent)
{
    Rig r;
    NX_EXPECT(r.ok);
    if (!compile(r.host, blueprint::child)) { NX_EXPECT(false); return; }
    asITypeInfo* type = r.host.findClass("blueprint", "Screen");
    NX_EXPECT(type && strcmp(type->GetName(), "BP_Child") == 0);
    asIScriptObject* object = type ? r.host.create(type) : nullptr;
    if (!object) { NX_EXPECT(false); return; }
    NX_EXPECT(r.host.callMethod(object, type->GetMethodByDecl("void onEnter()")));
    NX_EXPECT(strcmp(r.save.get("bp_inherited", "missing"), "7") == 0);
    r.host.release(object);
}

NX_TEST(blueprintMacrosPureFunctionsLocalsLoopsSwitchAndCurve)
{
    Rig r;
    NX_EXPECT(r.ok);
    if (!compile(r.host, blueprint::extended)) { NX_EXPECT(false); return; }
    asITypeInfo* type = r.host.findClass("blueprint", "Screen");
    asIScriptObject* object = type ? r.host.create(type) : nullptr;
    if (!object) { NX_EXPECT(false); return; }
    NX_EXPECT(r.host.callMethod(object, type->GetMethodByDecl("void onEnter()")));
    NX_EXPECT(strcmp(r.save.get("bp_extended", "missing"), "yes") == 0);
    auto* update = type->GetMethodByDecl("void update(float)");
    NX_EXPECT(r.host.callMethod(object, update, 0.25f));
    NX_EXPECT(fabsf(static_cast<float>(atof(r.save.get("bp_curve", "-1"))) - 1.0f) < 0.001f);
    NX_EXPECT(r.host.callMethod(object, update, 0.5f));
    NX_EXPECT(fabsf(static_cast<float>(atof(r.save.get("bp_curve", "-1"))) - 6.0f) < 0.001f);
    NX_EXPECT(r.host.callMethod(object, update, 0.25f));
    NX_EXPECT(fabsf(static_cast<float>(atof(r.save.get("bp_curve", "-1"))) - 10.0f) < 0.001f);
    r.host.release(object);
}

NX_TEST(blueprintParentLatentStateSurvivesChildUpdateOverride)
{
    Rig r;
    NX_EXPECT(r.ok);
    if (!compile(r.host, blueprint::delayed_child)) { NX_EXPECT(false); return; }
    asITypeInfo* type = r.host.findClass("blueprint", "Screen");
    asIScriptObject* object = type ? r.host.create(type) : nullptr;
    if (!object) { NX_EXPECT(false); return; }
    NX_EXPECT(r.host.callMethod(object, type->GetMethodByDecl("void onEnter()")));
    auto* update = type->GetMethodByDecl("void update(float)");
    NX_EXPECT(r.host.callMethod(object, update, 0.1f));
    NX_EXPECT(strcmp(r.save.get("bp_parent_delay", "missing"), "missing") == 0);
    NX_EXPECT(r.host.callMethod(object, update, 0.15f));
    NX_EXPECT(strcmp(r.save.get("bp_parent_delay", "missing"), "yes") == 0);
    r.host.release(object);
}

NX_TEST(blueprintInterfacesFindMatchingBehaviourAndHandleMissing)
{
    Rig r;
    NX_EXPECT(r.ok);
    World world(r.assets, r.host, "blueprint", 64, 32);
    world.space().init(&r.solids, &r.audio);
    r.services.world = &world;
    if (!compile(r.host, blueprint::messages)) { NX_EXPECT(false); return; }
    asITypeInfo* type = r.host.findClass("blueprint", "Screen");
    asIScriptObject* object = type ? r.host.create(type) : nullptr;
    if (!object) { NX_EXPECT(false); return; }
    NX_EXPECT(r.host.callMethod(object, type->GetMethodByDecl("void onEnter()")));
    NX_EXPECT(strcmp(r.save.get("bp_health", "missing"), "75") == 0);
    NX_EXPECT(strcmp(r.save.get("bp_missing", "missing"), "yes") == 0);
    NX_EXPECT(world.update(1.0f / 60));
    NX_EXPECT(strcmp(r.save.get("bp_started", "missing"), "yes") == 0);
    r.host.release(object);
    r.services.world = nullptr;
}

NX_TEST(blueprintTypedTimelineTracksAndPlayback)
{
    Rig r;
    NX_EXPECT(r.ok);
    if (!compile(r.host, blueprint::tracks)) { NX_EXPECT(false); return; }
    asITypeInfo* type = r.host.findClass("blueprint", "Screen");
    asIScriptObject* object = type ? r.host.create(type) : nullptr;
    if (!object) { NX_EXPECT(false); return; }
    NX_EXPECT(r.host.callMethod(object, type->GetMethodByDecl("void onEnter()")));
    auto* update = type->GetMethodByDecl("void update(float)");
    NX_EXPECT(r.host.callMethod(object, update, 0.25f));
    NX_EXPECT(fabsf(static_cast<float>(atof(r.save.get("bp_track_y", "-1"))) - 25.0f) < 0.001f);
    NX_EXPECT(fabsf(static_cast<float>(atof(r.save.get("bp_pingpong", "-1"))) - 2.5f) < 0.001f);
    NX_EXPECT(r.host.callMethod(object, update, 1.0f));
    NX_EXPECT(fabsf(static_cast<float>(atof(r.save.get("bp_track_y", "-1"))) - 10.0f) < 0.001f);
    NX_EXPECT(strcmp(r.save.get("bp_reverse_done", "missing"), "yes") == 0);
    NX_EXPECT(fabsf(static_cast<float>(atof(r.save.get("bp_pingpong", "-1"))) - 7.5f) < 0.001f);
    NX_EXPECT(r.host.callMethod(object, update, 2.25f));
    NX_EXPECT(fabsf(static_cast<float>(atof(r.save.get("bp_pingpong", "-1"))) - 5.0f) < 0.001f);
    NX_EXPECT(strcmp(r.save.get("bp_pingpong_done", "missing"), "missing") == 0);
    r.host.release(object);
}

NX_TEST(blueprintAnimationStateMachineControlsNativeSprites)
{
    Rig r;
    NX_EXPECT(r.ok);
    World world(r.assets, r.host, "blueprint", 64, 32);
    world.space().init(&r.solids, &r.audio);
    r.services.world = &world;
    if (!compile(r.host, blueprint::animation)) { NX_EXPECT(false); return; }
    asITypeInfo* type = r.host.findClass("blueprint", "Screen");
    asIScriptObject* object = type ? r.host.create(type) : nullptr;
    if (!object) { NX_EXPECT(false); return; }
    NX_EXPECT(r.host.callMethod(object, type->GetMethodByDecl("void onEnter()")));
    auto* update = type->GetMethodByDecl("void update(float)");
    NX_EXPECT(r.host.callMethod(object, update, 1.0f / 60));
    const u32 actor = world.findNamed("AnimatedActor");
    NX_EXPECT(actor != 0);
    NX_EXPECT(strcmp(r.save.get("bp_anim_state", "missing"), "Idle") == 0);
    const Animation* anim = world.getAnimation(actor);
    NX_EXPECT(anim && anim->first == 0 && anim->last == 0);
    r.input.simulate(Action::Accept);
    r.input.update(r.platform.state, 1.0f / 60);
    NX_EXPECT(r.host.callMethod(object, update, 1.0f / 60));
    NX_EXPECT(strcmp(r.save.get("bp_anim_state", "missing"), "Attack") == 0);
    anim = world.getAnimation(actor);
    NX_EXPECT(anim && anim->first == 1 && anim->last == 3 && !anim->loop);
    NX_EXPECT(world.update(0.5f));
    NX_EXPECT(world.getAnimation(actor) && world.getAnimation(actor)->done);
    r.input.update(r.platform.state, 1.0f / 60);
    NX_EXPECT(r.host.callMethod(object, update, 0.1f));
    NX_EXPECT(world.getAnimation(actor) && world.getAnimation(actor)->done);
    NX_EXPECT(strcmp(r.save.get("bp_anim_state", "missing"), "Attack") == 0);
    NX_EXPECT(fabsf(static_cast<float>(atof(r.save.get("bp_anim_elapsed", "-1"))) - 0.1f) < 0.001f);
    r.host.release(object);
    r.services.world = nullptr;
}

NX_TEST(blueprintModelAnimationMachineCompiles)
{
    Rig r;
    NX_EXPECT(r.ok);
    NX_EXPECT(compile(r.host, blueprint::model_animation));
}

NX_TEST(blueprintHierarchySpawnsNative2DAnd3DParentsAndBehaviours)
{
    Rig r;
    NX_EXPECT(r.ok);
    World world(r.assets, r.host, "blueprint", 64, 32);
    world.space().init(&r.solids, &r.audio);
    r.services.world = &world;
    if (!compile(r.host, blueprint::hierarchies)) { NX_EXPECT(false); return; }
    asITypeInfo* type = r.host.findClass("blueprint", "Screen");
    asIScriptObject* object = type ? r.host.create(type) : nullptr;
    if (!object) { NX_EXPECT(false); return; }
    NX_EXPECT(r.host.callMethod(object, type->GetMethodByDecl("void onEnter()")));
    NX_EXPECT(strcmp(r.save.get("bp_hierarchy", "missing"), "yes") == 0);
    const u32 player = world.findNamed("Player2D"), weapon = world.findNamed("WeaponRenamed"), muzzle = world.findNamed("Muzzle2D");
    NX_EXPECT(player != 0 && weapon != 0 && muzzle != 0);
    NX_EXPECT(world.parentOf(weapon) == player && world.parentOf(muzzle) == weapon);
    const Vec2 worldMuzzle = world.worldPosition(muzzle);
    NX_EXPECT(fabsf(worldMuzzle.x - 160.0f) < 0.001f && fabsf(worldMuzzle.y - 220.0f) < 0.001f);
    NX_EXPECT(world.behaviourAt(weapon, 0) != nullptr && world.behaviourAt(weapon, 1) == nullptr);
    const u32 root = world.findNamed("Root3D"), child = world.findNamed("Child3D");
    NX_EXPECT(root != 0 && child != 0 && world.parentOf(child) == root);
    const Vec3 worldChild = world.space().worldPlace(child);
    NX_EXPECT(fabsf(worldChild.x - 14.0f) < 0.001f && fabsf(worldChild.y - 22.0f) < 0.001f && fabsf(worldChild.z - 33.0f) < 0.001f);
    NX_EXPECT(world.space().getAppearance(root) != nullptr && world.space().getAppearance(child) != nullptr);
    NX_EXPECT(world.update(1.0f / 60));
    NX_EXPECT(strcmp(r.save.get("bp_started", "missing"), "yes") == 0);
    r.host.release(object);
    r.services.world = nullptr;
}
