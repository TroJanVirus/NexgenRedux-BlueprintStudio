#include "test.h"
#include "rig.h"
#include "blueprint_fixtures.h"
#include "scene/world.h"
#include "tools/blueprint/native_compile.h"
#include "tools/blueprint/native_import.h"
#ifdef NX_DESKTOP_BLUEPRINT
#include "app/blueprint_workspace.h"
#include "script/debugger.h"
#include "tools/blueprint/native_assets.h"
#endif

#include <math.h>
#include <chrono>
#include <thread>
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

#include "blueprint_ui_fixture.h"
#include "ui/ui.h"

NX_TEST(blueprintDesignedMenuUsesNativeWidgets)
{
    Rig r;
    NX_EXPECT(r.ok);
    r.platform.files["showcase/main/screen.json"] = "{\"title\":\"Blueprint Menu\"}";
    r.platform.files["showcase/main/main.as"] = blueprint_ui::script;
    r.platform.files["showcase/main/asset/ui/menu.json"] = blueprint_ui::page;
    r.platform.files["showcase/main/asset/ui/theme.json"] = blueprint_ui::theme;
    if (!r.screens.init(r.services, "showcase", "main")) { NX_EXPECT(false); return; }
    r.step(3);
    NX_EXPECT(!r.screens.failed());
    if (!r.services.ui) { NX_EXPECT(false); return; }
    NX_EXPECT(r.services.ui->find("play") != nullptr);
    NX_EXPECT(r.services.ui->find("volume") != nullptr);
    r.press(Action::Accept);
    NX_EXPECT(!r.screens.failed());
    NX_EXPECT(strcmp(r.save.get("ui_clicked", "missing"), "yes") == 0);
}

#include "blueprint_media_fixture.h"

NX_TEST(blueprintSharedMediaReadsFromBothScreens)
{
    for (const char* script : {blueprint_media::screen0, blueprint_media::screen1})
    {
        Rig r;
        NX_EXPECT(r.ok);
        r.platform.files["showcase/main/screen.json"] = "{\"title\":\"Shared media\"}";
        r.platform.files["showcase/main/main.as"] = script;
        r.platform.files["global/blueprint_demo/data/message.txt"] = "Shared message";
        if (!r.screens.init(r.services, "showcase", "main")) { NX_EXPECT(false); continue; }
        r.step(2);
        NX_EXPECT(!r.screens.failed());
        NX_EXPECT(strcmp(r.save.get("shared_result", "missing"), "Shared message") == 0);
    }
}

#include "blueprint_scene_fixture.h"

NX_TEST(blueprintDesignedSceneLoadsNativeObjects)
{
    Rig r;
    NX_EXPECT(r.ok);
    r.platform.files["showcase/main/screen.json"] = "{\"title\":\"Blueprint scene\"}";
    r.platform.files["showcase/main/main.as"] = blueprint_scene::script;
    r.platform.files["showcase/main/asset/levels/demo.json"] = blueprint_scene::level;
    if (!r.screens.init(r.services, "showcase", "main")) { NX_EXPECT(false); return; }
    r.step(3);
    NX_EXPECT(!r.screens.failed());
    if (!r.services.world) { NX_EXPECT(false); return; }
    World& world = *r.services.world;
    const u32 crate = world.findNamed("Crate"), lamp = world.findNamed("Lamp"), floor = world.findNamed("Floor");
    NX_EXPECT(crate && lamp && floor);
    if (!crate || !lamp || !floor) return;
    const Spatial* spatial = world.space().getSpatial(crate);
    NX_EXPECT(spatial && fabsf(spatial->place.x - 3) < 0.001f && fabsf(spatial->place.y - 2) < 0.001f && fabsf(spatial->place.z + 4) < 0.001f);
    NX_EXPECT(spatial && fabsf(spatial->size.x - 2) < 0.001f && fabsf(spatial->size.z - 4) < 0.001f);
    NX_EXPECT(spatial && fabsf(spatial->turn.rotate({1, 0, 0}).z + 1) < 0.001f);
    const Appearance* appearance = world.space().getAppearance(crate);
    NX_EXPECT(appearance && appearance->solid == Solid::Box);
    const Collider3* collider = world.space().getCollider(crate);
    NX_EXPECT(collider && collider->shape == Shape3::Box);
    const Light3* light = world.space().light(lamp);
    NX_EXPECT(light && fabsf(light->range - 12) < 0.001f);
    const Appearance* ground = world.space().getAppearance(floor);
    NX_EXPECT(ground && ground->solid == Solid::Plane);
    NX_EXPECT(world.behaviourAt(crate, 0) != nullptr);
    NX_EXPECT(strcmp(r.save.get("bp_started", "missing"), "yes") == 0);
}

#include "blueprint_mixed_fixture.h"

NX_TEST(blueprintMixedScriptsKeepOriginalBehaviour)
{
    for (const char* script : {blueprint_mixed::original, blueprint_mixed::converted})
    {
        Rig r;
        NX_EXPECT(r.ok);
        r.platform.files["showcase/main/screen.json"] = "{\"title\":\"Mixed script\"}";
        r.platform.files["showcase/main/main.as"] = script;
        if (!r.screens.init(r.services, "showcase", "main")) { NX_EXPECT(false); continue; }
        r.step(3);
        NX_EXPECT(!r.screens.failed());
        NX_EXPECT(strcmp(r.save.get("mixed_result", "missing"), "yes") == 0);
    }
}

NX_TEST(nativeBlueprintCompilerRunsThroughReduxScreenLifecycle)
{
    BlueprintDocument doc;doc.create();
    std::string error,source;
    const bool authored=doc.addNode(0,"api:Save:Save::set:string,string",300,50,error)&&
        doc.addNode(0,"literal:string",20,200,error)&&doc.addNode(0,"literal:string",20,300,error)&&
        doc.setLiteral(0,2,"native_graph",error)&&doc.setLiteral(0,3,"ran",error)&&
        doc.connect(0,0,"then",1,"in",error)&&doc.connect(0,2,"value",1,"key",error)&&
        doc.connect(0,3,"value",1,"value",error)&&compileBlueprint(doc,source,error);
    NX_EXPECT(authored);
    if(!authored)return;
    Rig r;
    NX_EXPECT(r.ok);
    if(!r.ok||!compile(r.host,source.c_str())){NX_EXPECT(false);return;}
    auto* type=r.host.findClass("blueprint","Screen");
    auto* object=type?r.host.create(type):nullptr;
    NX_EXPECT(object!=nullptr);
    if(!object)return;
    NX_EXPECT(r.host.callMethod(object,type->GetMethodByDecl("void onEnter()")));
    NX_EXPECT(strcmp(r.save.get("native_graph","missing"),"ran")==0);
    r.host.release(object);
}

NX_TEST(nativeBlueprintStateSurvivesAcrossRealScreenEvents)
{
    BlueprintDocument doc;doc.create();std::string error,source;
    const bool authored=doc.addNode(0,"var:set:string",0,0,error)&&doc.addNode(0,"literal:string",0,0,error)&&
        doc.setReference(0,1,"message",error)&&doc.setLiteral(0,2,"persisted",error)&&
        doc.connect(0,0,"then",1,"in",error)&&doc.connect(0,2,"value",1,"value",error)&&
        doc.addNode(1,"api:Save:Save::set:string,string",0,0,error)&&doc.addNode(1,"literal:string",0,0,error)&&
        doc.addNode(1,"var:get:string",0,0,error)&&doc.setLiteral(1,2,"native_state",error)&&doc.setReference(1,3,"message",error)&&
        doc.connect(1,0,"then",1,"in",error)&&doc.connect(1,2,"value",1,"key",error)&&doc.connect(1,3,"value",1,"value",error)&&
        compileBlueprint(doc,source,error);
    NX_EXPECT(authored);if(!authored)return;
    Rig r;NX_EXPECT(r.ok);
    if(!r.ok||!compile(r.host,source.c_str())){NX_EXPECT(false);return;}
    auto* type=r.host.findClass("blueprint","Screen");auto* object=type?r.host.create(type):nullptr;
    auto* fresh=type?r.host.create(type):nullptr;
    NX_EXPECT(object&&fresh);
    if(object&&fresh)
    {
        NX_EXPECT(r.host.callMethod(object,type->GetMethodByDecl("void onEnter()")));
        NX_EXPECT(r.host.callMethod(object,type->GetMethodByDecl("void update(float)"),1.0f/60));
        NX_EXPECT(strcmp(r.save.get("native_state","missing"),"persisted")==0);
        NX_EXPECT(r.host.callMethod(fresh,type->GetMethodByDecl("void update(float)"),1.0f/60));
        NX_EXPECT(strcmp(r.save.get("native_state","missing"),"")==0);
    }
    if(object)r.host.release(object);
    if(fresh)r.host.release(fresh);
}

#ifdef NX_DESKTOP_BLUEPRINT
NX_TEST(nativeBlueprintPreviewUsesOriginalScreenStackAndIsolatedSave)
{
    Rig r; NX_EXPECT(r.ok); if(!r.ok)return;
    r.platform.files["showcase/main/screen.json"]="{\"title\":\"Home\"}";
    r.platform.files["showcase/main/main.as"]="class Home : Screen {}";
    if(!r.screens.init(r.services,"showcase","main")){NX_EXPECT(false);return;}
    r.step(3);
    r.save.set("preview_key","original");
    r.audio.setMasterVolume(0.65f);
    r.screens.setDevelopmentPaused(true);
    const std::string code="class Preview : Screen { void onEnter() { Save::set(\"preview_key\", \"temporary\"); } void update(float dt) { Save::set(\"updated\", \"yes\"); } }";
    std::string error;
    NX_EXPECT(r.screens.startPreview(code,error));
    if(!r.screens.previewing())return;
    r.step(3);
    NX_EXPECT(r.screens.depth()==2 && r.services.save!=&r.save);
    NX_EXPECT(strcmp(r.services.save->get("preview_key","missing"),"temporary")==0);
    NX_EXPECT(strcmp(r.services.save->get("updated","missing"),"yes")==0);
    r.audio.setMasterVolume(0.15f);
    r.screens.home();r.step(1);
    NX_EXPECT(r.screens.depth()==2);
    r.screens.stopPreview();
    NX_EXPECT(r.screens.depth()==1 && r.services.save==&r.save && r.screens.menuOpen());
    NX_EXPECT(strcmp(r.save.get("preview_key","missing"),"original")==0);
    NX_EXPECT(strcmp(r.save.get("updated","missing"),"missing")==0);
    NX_EXPECT(fabsf(r.audio.masterVolume()-0.65f)<0.001f);
    NX_EXPECT(!r.screens.startPreview("not valid AngelScript",error));
    NX_EXPECT(!r.screens.previewing() && r.services.save==&r.save && r.screens.depth()==1);
}
#endif

NX_TEST(nativeBlueprintExportLoadsAsOrdinaryReduxScreen)
{
    BlueprintDocument doc;doc.create();doc.name="Exported game";
    std::string error,source,manifest;
    const bool authored=doc.addNode(0,"api:Save:Save::set:string,string",0,0,error)&&
        doc.addNode(0,"literal:string",0,0,error)&&doc.addNode(0,"literal:string",0,0,error)&&
        doc.setLiteral(0,2,"export_test",error)&&doc.setLiteral(0,3,"ran",error)&&
        doc.connect(0,0,"then",1,"in",error)&&doc.connect(0,2,"value",1,"key",error)&&
        doc.connect(0,3,"value",1,"value",error)&&compileBlueprintScreen(doc,source,manifest,error);
    NX_EXPECT(authored);if(!authored)return;
    Rig r;NX_EXPECT(r.ok);if(!r.ok)return;
    r.platform.files["showcase/exported/screen.json"]=manifest;
    r.platform.files["showcase/exported/main.as"]=source;
    NX_EXPECT(r.screens.init(r.services,"showcase","exported"));
    r.step(3);
    NX_EXPECT(!r.screens.failed()&&strcmp(r.save.get("export_test","missing"),"ran")==0);
}

NX_TEST(nativeBlueprintInlineInputsRunOnRealEngine)
{
    BlueprintDocument doc;doc.create();std::string error,source;
    const bool authored=doc.addNode(0,"api:Save:Save::set:string,string",0,0,error)&&
        doc.connect(0,0,"then",1,"in",error)&&doc.setInputValue(0,1,"key","inline_test",error)&&
        doc.setInputValue(0,1,"value","works",error)&&compileBlueprint(doc,source,error);
    NX_EXPECT(authored);if(!authored)return;
    Rig r;NX_EXPECT(r.ok);if(!r.ok)return;
    r.platform.files["showcase/inline_values/screen.json"]="{\"title\":\"Inline values\"}";
    r.platform.files["showcase/inline_values/main.as"]=source;
    if(!r.screens.init(r.services,"showcase","inline_values")){NX_EXPECT(false);return;}
    r.step(3);NX_EXPECT(!r.screens.failed()&&strcmp(r.save.get("inline_test","missing"),"works")==0);
}

#ifdef NX_DESKTOP_BLUEPRINT
NX_TEST(nativeBlueprintWorkspaceAuthorsSavesAndRunsThroughMouseAndKeyboard)
{
    Rig r;NX_EXPECT(r.ok);if(!r.ok)return;
    r.platform.workspaceFiles=true;
    r.platform.files["showcase/main/screen.json"]="{\"title\":\"Home\"}";
    r.platform.files["showcase/main/main.as"]="class Home : Screen {}";
    if(!r.screens.init(r.services,"showcase","main")){NX_EXPECT(false);return;}
    r.step(3);r.screens.setDevelopmentPaused(true);
    BlueprintWorkspace studio;studio.open(r.services);
    NX_EXPECT(studio.active());if(!studio.active())return;
    auto& state=r.platform.state;state.hasMouse=true;
    auto frame=[&]()
    {
        r.assets.finish();r.input.update(state,1.0f/60);r.platform.now+=1.0/60;
        if(studio.previewing())studio.updatePreview(r.services,1.0f/60);
        else studio.update(r.services);
        studio.checkpoint(r.services);
        r.device->beginFrame({});r.canvas.begin();
        if(!studio.previewing()||studio.tracePaused())studio.draw(r.canvas);else r.screens.draw();
        if(studio.previewing())studio.drawPreviewOverlay(r.canvas);
        r.canvas.end();r.device->endFrame();
    };
    auto click=[&](float x,float y)
    {
        state.mouseX=x;state.mouseY=y;state.mouseButtons[0]=true;frame();
        state.mouseButtons[0]=false;frame();
    };
    auto type=[&](const char* value)
    {
        for(const char* p=value;*p;++p){state.type(static_cast<u32>(*p));frame();}
        state.type(13);frame();
    };
    frame();
    // Search the real library, insert its first result, and connect it to onEnter.
    click(1100,130);type("api:Save:Save::set:string,string");click(1100,167);
    click(468,138);click(630,353);
    click(630,375);type("workspace_key");
    click(630,397);type("first");
    click(900,18);type("Workspace Test");
    const std::string path="own/blueprint-project-Workspace Test.bp.json";
    auto found=r.platform.files.find(path);
    NX_EXPECT(found!=r.platform.files.end());
    if(found==r.platform.files.end()){studio.close(r.services);return;}
    BlueprintDocument doc;std::string source,error;
    NX_EXPECT(doc.load(found->second.data(),found->second.size(),error));
    NX_EXPECT(compileBlueprint(doc,source,error)&&source.find("Save::set(\"workspace_key\", \"first\")")!=std::string::npos);
    // Import an ordinary native asset through the real workspace and export it with the project.
    NX_EXPECT(!doc.assetRoot.empty());
    r.platform.files["own/test_scene.json"]="{\"objects\":[]}";
    click(1027,18);r.platform.dropped="test_scene.json";frame();
    const auto assetPath="own/"+doc.assetRoot+"/Screen/asset/test_scene.json";
    NX_EXPECT(r.platform.files.count(assetPath)&&r.platform.files[assetPath]=="{\"objects\":[]}");
    // The real UI launches the installed editor session and requires confirmation before returning.
    r.platform.files["showcase/editor/main.as"]="class Editor : Screen { void onEnter() { Assets::write(\"asset:scene.json\", \"{}\"); } }";
    click(614,133);
    NX_EXPECT(studio.previewing()&&r.services.sceneEditorAssets);
    if(studio.previewing())
    {
        for(int i=0;i<4;++i)frame();
        state.keys[static_cast<size_t>(Key::Escape)]=true;frame();
        state.keys[static_cast<size_t>(Key::Escape)]=false;frame();
        NX_EXPECT(studio.previewing());
        state.keys[static_cast<size_t>(Key::Enter)]=true;frame();
        state.keys[static_cast<size_t>(Key::Enter)]=false;frame();
        NX_EXPECT(!studio.previewing()&&!r.services.sceneEditorAssets);
    }
    NX_EXPECT(r.platform.files["own/"+doc.assetRoot+"/Screen/asset/scene.json"]=="{}");
    click(930,133);click(675,53);
    bool exported=false;
    for(const auto& item:r.platform.files)
        if(item.first.rfind("own/bp-",0)==0&&item.first.ends_with("/Screen/asset/test_scene.json")&&item.second=="{\"objects\":[]}")
        {
            const auto prefix=item.first.substr(0,item.first.size()-strlen("asset/test_scene.json"));
            exported=r.platform.files.count(prefix+"main.as")&&r.platform.files.count(prefix+"screen.json");
        }
    NX_EXPECT(exported);
    // Run what the user actually authored, through the workspace Play button.
    click(553,53);
    NX_EXPECT(studio.previewing());
    if(studio.previewing())
    {
        for(int i=0;i<4;++i)frame();
        NX_EXPECT(strcmp(r.services.save->get("workspace_key","missing"),"first")==0);
        state.keys[static_cast<size_t>(Key::Escape)]=true;frame();
        state.keys[static_cast<size_t>(Key::Escape)]=false;frame();
        NX_EXPECT(!studio.previewing());
        NX_EXPECT(strcmp(r.save.get("workspace_key","missing"),"missing")==0);
    }
    // Modify the saved file out-of-band, edit a pin, and ensure Save never overwrites it.
    const std::string external=found->second+"\n";
    r.platform.files[path]=external;
    click(630,397);
    for(int i=0;i<5;++i){state.type(8);frame();}
    type("second");
    click(800,53);
    NX_EXPECT(r.platform.files[path]==external);
    studio.close(r.services);
    const auto recovered=r.platform.files.find("own/blueprint-recovery.bp.json");
    NX_EXPECT(recovered!=r.platform.files.end()&&!recovered->second.empty());
    if(recovered!=r.platform.files.end())
    {
        BlueprintDocument draft;
        NX_EXPECT(draft.load(recovered->second.data(),recovered->second.size(),error)&&
            compileBlueprint(draft,source,error)&&source.find("\"second\"")!=std::string::npos);
    }
}
#endif

NX_TEST(nativeScriptCompileErrorRetainsSourceLine)
{
    Rig r;NX_EXPECT(r.ok);if(!r.ok)return;
    const char invalid[]="class Broken : Screen {\n void onEnter() {\n invalid_call();\n }\n}";
    NX_EXPECT(!r.host.compile("broken","blueprint.as",invalid,strlen(invalid)));
    NX_EXPECT(blueprintErrorLine(r.host.lastError(),"blueprint.as")==3);
    NX_EXPECT(compile(r.host,"class Valid : Screen {}"));
    NX_EXPECT(r.host.lastError()[0]==0);
}

NX_TEST(nativeBuiltinOperatorsCompileThroughAngelScript)
{
    Rig r;NX_EXPECT(r.ok);if(!r.ok)return;
    for(const auto& spec:blueprintCatalogue())
    {
        if(spec.operation.empty())continue;
        BlueprintDocument doc;doc.create();std::string source,error;
        bool authored=doc.addNode(0,spec.id,0,0,error)&&doc.addNode(0,"var:set:"+spec.result,0,0,error)&&
            doc.connect(0,0,"then",2,"in",error)&&doc.connect(0,1,"result",2,"value",error);
        for(const auto& p:spec.inputs)
            authored=authored&&doc.setInputValue(0,1,p.name,p.type=="string"?"test":p.type=="bool"?"true":"2",error);
        NX_EXPECT(authored&&compileBlueprint(doc,source,error));
        if(authored&&!source.empty())
        {
            NX_EXPECT(compile(r.host,source.c_str()));
            r.host.discard("blueprint");
        }
    }
}

#ifdef NX_DESKTOP_BLUEPRINT
NX_TEST(nativeBlueprintProjectPreviewNavigatesWithoutTouchingHostStack)
{
    Rig r;NX_EXPECT(r.ok);if(!r.ok)return;
    r.platform.files["showcase/main/screen.json"]="{\"title\":\"Home\"}";
    r.platform.files["showcase/main/main.as"]="class Home : Screen {}";
    if(!r.screens.init(r.services,"showcase","main")){NX_EXPECT(false);return;}
    r.step(3);r.screens.setDevelopmentPaused(true);
    BlueprintDocument doc;doc.create();std::string error;
    const bool authored=doc.addScreen("level",error)&&
        doc.addNode(0,"api:Screens:Screens::push:string",0,0,error)&&doc.setInputValue(0,1,"id","level",error)&&doc.connect(0,0,"then",1,"in",error)&&
        doc.addNode(6,"api:Save:Save::set:string,string",0,0,error)&&doc.setInputValue(6,1,"key","level",error)&&doc.setInputValue(6,1,"value","entered",error)&&doc.connect(6,0,"then",1,"in",error);
    std::vector<BlueprintScreenOutput> output;
    NX_EXPECT(doc.importScreenManifest("Screen","{\"title\":\"Start menu\",\"description\":\"Preview description\",\"category\":\"Game\",\"demo\":\"Press A\",\"seconds\":12.5,\"order\":20}",error));
    NX_EXPECT(doc.importScreenManifest("level","{\"title\":\"Level one\",\"order\":1}",error));
    NX_EXPECT(authored&&compileBlueprintProject(doc,output,error));if(output.size()!=2)return;
    std::vector<Screens::PreviewScreen> preview;
    for(const auto& o:output)
    {
        Screens::PreviewScreen p;snprintf(p.info.id,sizeof(p.info.id),"%s",o.id.c_str());p.source=o.source;p.manifest=o.manifest;preview.push_back(p);
    }
    auto broken=preview;broken[0].manifest="{\"title\":false}";
    NX_EXPECT(!r.screens.startProjectPreview(broken,"Screen",error));
    NX_EXPECT(!r.screens.previewing()&&r.screens.depth()==1&&r.services.save==&r.save);
    NX_EXPECT(r.screens.startProjectPreview(preview,"Screen",error));if(!r.screens.previewing())return;
    NX_EXPECT(r.screens.info(0)&&strcmp(r.screens.info(0)->id,"level")==0);
    const auto* meta=r.screens.info(1);
    NX_EXPECT(meta&&strcmp(meta->id,"Screen")==0&&strcmp(meta->title,"Start menu")==0&&
        strcmp(meta->description,"Preview description")==0&&strcmp(meta->category,"Game")==0&&
        strcmp(meta->demo,"Press A")==0&&meta->seconds==12.5f&&meta->order==20);
    r.step(5);
    NX_EXPECT(r.screens.depth()==3&&strcmp(r.screens.current(),"level")==0&&r.screens.count()==2);
    NX_EXPECT(strcmp(r.services.save->get("level","missing"),"entered")==0);
    r.screens.push("main");r.step(1);NX_EXPECT(strcmp(r.screens.current(),"level")==0);
    r.screens.home();r.step(1);NX_EXPECT(r.screens.depth()==2&&strcmp(r.screens.current(),"Screen")==0);
    r.screens.pop();r.step(1);NX_EXPECT(r.screens.depth()==2);
    r.screens.stopPreview();
    NX_EXPECT(r.screens.depth()==1&&r.services.save==&r.save&&strcmp(r.screens.current(),"main")==0);
    NX_EXPECT(strcmp(r.save.get("level","missing"),"missing")==0);
}
#endif

NX_TEST(nativeBlueprintProjectExportRunsWithoutStudio)
{
    BlueprintDocument doc;doc.create();std::string error;
    const bool authored=doc.addScreen("level",error)&&
        doc.addNode(0,"api:Screens:Screens::push:string",0,0,error)&&doc.setInputValue(0,1,"id","level",error)&&doc.connect(0,0,"then",1,"in",error)&&
        doc.addNode(6,"api:Save:Save::set:string,string",0,0,error)&&doc.setInputValue(6,1,"key","export_level",error)&&
        doc.setInputValue(6,1,"value","entered",error)&&doc.connect(6,0,"then",1,"in",error);
    std::vector<BlueprintScreenOutput> output;
    NX_EXPECT(authored&&compileBlueprintProject(doc,output,error));if(output.size()!=2)return;
    Rig r;NX_EXPECT(r.ok);if(!r.ok)return;
    for(const auto& screen:output)
    {
        r.platform.files["project/"+screen.id+"/main.as"]=screen.source;
        r.platform.files["project/"+screen.id+"/screen.json"]=screen.manifest;
    }
    NX_EXPECT(r.screens.init(r.services,"project","Screen"));
    r.step(5);
    NX_EXPECT(r.screens.depth()==2&&strcmp(r.screens.current(),"level")==0);
    NX_EXPECT(strcmp(r.save.get("export_level","missing"),"entered")==0);
}

NX_TEST(nativeGeneratedBehaviourAttachesAndRunsInOriginalWorld)
{
    BlueprintDocument doc;doc.create();std::string error,source;
    const bool authored=doc.addBehaviour("Spinner",error)&&
        doc.addNode(0,"api:Scene:Scene::spawn:Vec2",0,0,error)&&doc.addNode(0,"make:Vec2",0,0,error)&&
        doc.setInputValue(0,2,"x","0",error)&&doc.setInputValue(0,2,"y","0",error)&&doc.connect(0,2,"result",1,"position",error)&&
        doc.addNode(0,"attach:behaviour",0,0,error)&&doc.connect(0,0,"then",1,"in",error)&&doc.connect(0,1,"then",3,"in",error)&&
        doc.connect(0,1,"result",3,"target",error)&&
        doc.addNode(6,"api:Save:Save::set:string,string",0,0,error)&&doc.setInputValue(6,1,"key","native_behaviour",error)&&
        doc.setInputValue(6,1,"value","started",error)&&doc.connect(6,0,"then",1,"in",error)&&compileBlueprint(doc,source,error);
    NX_EXPECT(authored);if(!authored)return;
    Rig r;NX_EXPECT(r.ok);if(!r.ok)return;
    r.platform.files["showcase/behaviour/screen.json"]="{\"title\":\"Behaviour\"}";
    r.platform.files["showcase/behaviour/main.as"]=source;
    if(!r.screens.init(r.services,"showcase","behaviour")){NX_EXPECT(false);return;}
    r.step(5);
    NX_EXPECT(!r.screens.failed()&&strcmp(r.save.get("native_behaviour","missing"),"started")==0);
}

NX_TEST(nativeBehaviourStateAndCollisionValuesArePerEntity)
{
    BlueprintDocument doc;doc.create();std::string error,source;
    bool ok=doc.addBehaviour("Counter",error);
    // start: increment the instance field; check that every instance begins at one.
    auto add=[&](size_t g,const char* kind){ok=ok&&doc.addNode(g,kind,0,0,error);};
    auto wire=[&](size_t g,size_t a,const char* out,size_t b,const char* in)
    {ok=ok&&doc.connect(g,a,out,b,in,error);};
    auto input=[&](size_t g,size_t n,const char* pin,const char* value)
    {ok=ok&&doc.setInputValue(g,n,pin,value,error);};
    add(6,"var:get:int");add(6,"add:int");add(6,"var:set:int");
    add(6,"equal:int");add(6,"branch");add(6,"event:entity");
    add(6,"api:Entity:get_name:Entity@");add(6,"api:Save:Save::set:string,string");
    wire(6,1,"value",2,"a");input(6,2,"b","1");wire(6,2,"result",3,"value");
    wire(6,0,"then",3,"in");wire(6,3,"then",5,"in");
    wire(6,1,"value",4,"a");input(6,4,"b","1");wire(6,4,"result",5,"condition");
    wire(6,6,"value",7,"target");wire(6,7,"result",8,"key");
    wire(6,5,"true",8,"in");input(6,8,"value","started");
    // onHit: store the owning entity name under the collision partner's name.
    add(8,"event:entity");add(8,"event:other");
    add(8,"api:Entity:get_name:Entity@");add(8,"api:Entity:get_name:Entity@");
    add(8,"api:Save:Save::set:string,string");
    wire(8,1,"value",3,"target");wire(8,2,"value",4,"target");
    wire(8,4,"result",5,"key");wire(8,3,"result",5,"value");wire(8,0,"then",5,"in");
    NX_EXPECT(ok&&compileBlueprint(doc,source,error));if(!ok||source.empty())return;
    source+=R"(
        void setup() {
            Entity@ a = Scene::spawn(Vec2(0,0));
            a.name = "first";
            a.circle(10, 1, 1 << 2);
            a.add(BP_Behaviour_Counter());
            Entity@ b = Scene::spawn(Vec2(100,0));
            b.name = "second";
            b.circle(10, 1, 1 << 2);
            b.add(BP_Behaviour_Counter());
            Entity@ c = Scene::spawn(Vec2(0,0));
            c.name = "partner_a";
            c.circle(10, 2, 0);
            Entity@ d = Scene::spawn(Vec2(100,0));
            d.name = "partner_b";
            d.circle(10, 2, 0);
        }
    )";
    Rig r;NX_EXPECT(r.ok);if(!r.ok)return;
    World world(r.assets,r.host,"blueprint",64,32);world.space().init(&r.solids,&r.audio);
    r.services.world=&world;
    if(!compile(r.host,source.c_str())){NX_EXPECT(false);r.services.world=nullptr;return;}
    NX_EXPECT(r.host.call(r.host.find("blueprint","void setup()")));
    NX_EXPECT(world.update(1.0f/60));NX_EXPECT(world.update(1.0f/60));
    NX_EXPECT(strcmp(r.save.get("first","missing"),"started")==0);
    NX_EXPECT(strcmp(r.save.get("second","missing"),"started")==0);
    NX_EXPECT(strcmp(r.save.get("partner_a","missing"),"first")==0);
    NX_EXPECT(strcmp(r.save.get("partner_b","missing"),"second")==0);
    r.services.world=nullptr;
}

NX_TEST(nativeTypedFunctionCallsExecuteThroughAngelScript)
{
    BlueprintDocument doc;doc.create();std::string error,source;
    bool ok=doc.addFunction("Join",error)&&doc.editFunctionSignature(6,"string(a:string, b:string)",error);
    auto add=[&](size_t g,const char* kind){ok=ok&&doc.addNode(g,kind,0,0,error);};
    auto wire=[&](size_t g,size_t a,const char* out,size_t b,const char* in){ok=ok&&doc.connect(g,a,out,b,in,error);};
    auto input=[&](size_t g,size_t n,const char* pin,const char* value){ok=ok&&doc.setInputValue(g,n,pin,value,error);};
    add(6,"add:string");add(6,"return:function");
    wire(6,0,"arg_a",1,"a");wire(6,0,"arg_b",1,"b");wire(6,0,"then",2,"in");wire(6,1,"result",2,"value");
    add(0,"call:function");add(0,"api:Save:Save::set:string,string");
    input(0,1,"arg_a","hello ");input(0,1,"arg_b","world");input(0,2,"key","function_result");
    wire(0,0,"then",1,"in");wire(0,1,"then",2,"in");wire(0,1,"result",2,"value");
    NX_EXPECT(ok&&compileBlueprint(doc,source,error));if(!ok||source.empty())return;
    Rig r;NX_EXPECT(r.ok);if(!r.ok)return;
    r.platform.files["showcase/functions/screen.json"]="{\"title\":\"Functions\"}";
    r.platform.files["showcase/functions/main.as"]=source;
    if(!r.screens.init(r.services,"showcase","functions")){NX_EXPECT(false);return;}
    r.step(3);NX_EXPECT(!r.screens.failed()&&strcmp(r.save.get("function_result","missing"),"hello world")==0);
}

NX_TEST(nativeFunctionEarlyReturnAndBranchContinuationExecuteCorrectly)
{
    BlueprintDocument doc;doc.create();std::string error,source;
    bool ok=doc.addFunction("Choose",error)&&doc.editFunctionSignature(6,"int(flag:bool)",error);
    auto add=[&](const char* kind){ok=ok&&doc.addNode(6,kind,0,0,error);};
    auto wire=[&](size_t a,const char* out,size_t b,const char* in){ok=ok&&doc.connect(6,a,out,b,in,error);};
    add("branch");add("return:function");add("return:function");
    wire(0,"then",1,"in");wire(0,"arg_flag",1,"condition");
    wire(1,"true",2,"in");wire(1,"then",3,"in");
    ok=ok&&doc.setInputValue(6,2,"value","7",error)&&doc.setInputValue(6,3,"value","8",error);
    NX_EXPECT(ok&&compileBlueprint(doc,source,error));if(!ok||source.empty())return;
    source+=R"(
        void check() {
            BP_NativeScreen screen;
            Save::set("branch_true", screen.bp_fn_Choose(true)==7?"yes":"no");
            Save::set("branch_false", screen.bp_fn_Choose(false)==8?"yes":"no");
        }
    )";
    Rig r;NX_EXPECT(r.ok);if(!r.ok)return;
    if(!compile(r.host,source.c_str())){NX_EXPECT(false);return;}
    NX_EXPECT(r.host.call(r.host.find("blueprint","void check()")));
    NX_EXPECT(strcmp(r.save.get("branch_true","missing"),"yes")==0);
    NX_EXPECT(strcmp(r.save.get("branch_false","missing"),"yes")==0);
}

#ifdef NX_DESKTOP_BLUEPRINT
NX_TEST(nativeProjectAssetsAreCopiedWithoutChangingOriginals)
{
    Rig r;NX_EXPECT(r.ok);if(!r.ok)return;
    r.platform.workspaceFiles=true;
    std::string root,clone,error;
    NX_EXPECT(blueprintCreateAssetRoot(r.platform,root,error));
    NX_EXPECT(blueprintCreateAssetRoot(r.platform,clone,error));
    r.platform.files["own/scene.json"]="{\"scene\":[]}";
    NX_EXPECT(blueprintImportAsset(r.platform,root,"Screen","scene.json",error));
    NX_EXPECT(!blueprintImportAsset(r.platform,root,"Screen","scene.json",error));
    NX_EXPECT(!blueprintImportAsset(r.platform,root,"Screen","../scene.json",error));
    const auto base="own/"+root+"/Screen/asset/";
    const std::string binary("a\0b",3);
    r.platform.files[base+"model/mesh.glb"]=binary;
    std::vector<std::string> files;
    NX_EXPECT(blueprintListAssets(r.platform,root,"Screen",files,error)&&files.size()==2);
    NX_EXPECT(blueprintCopyAssets(r.platform,root,clone,"Screen",error));
    NX_EXPECT(r.platform.files["own/"+clone+"/Screen/asset/model/mesh.glb"]==binary);
    NX_EXPECT(r.platform.files["own/"+clone+"/Screen/asset/scene.json"]=="{\"scene\":[]}");
    NX_EXPECT(r.platform.files["own/scene.json"]=="{\"scene\":[]}");
    NX_EXPECT(!blueprintCopyAssets(r.platform,root,clone,"Screen",error));
}
NX_TEST(nativeProjectPreviewReadsItsOwnAssetsNotSavedHostOverlays)
{
    Rig r;NX_EXPECT(r.ok);if(!r.ok)return;
    r.platform.workspaceFiles=true;
    r.platform.files["showcase/main/screen.json"]="{\"title\":\"Home\"}";
    r.platform.files["showcase/main/main.as"]="class Home : Screen {}";
    if(!r.screens.init(r.services,"showcase","main")){NX_EXPECT(false);return;}
    r.step(3);r.screens.setDevelopmentPaused(true);
    std::string root,error;
    NX_EXPECT(blueprintCreateAssetRoot(r.platform,root,error));
    r.platform.files["own/"+root+"/Screen/asset/level.json"]="project";
    r.platform.kept["Screen.level"]="stale-host";
    Screens::PreviewScreen p;
    snprintf(p.info.id,sizeof(p.info.id),"Screen");
    p.source=R"(class Preview : Screen { void onEnter() {
        Save::set("asset_value", Assets::read("asset:level.json"));
        Assets::write("asset:level.json", "changed");
    } })";
    NX_EXPECT(r.screens.startProjectPreview({p},"Screen",error,root));
    if(!r.screens.previewing())return;
    r.step(3);
    NX_EXPECT(!r.screens.failed()&&strcmp(r.services.save->get("asset_value","missing"),"project")==0);
    NX_EXPECT(r.platform.files["own/"+root+"/Screen/asset/level.json"]=="project");
    NX_EXPECT(r.platform.kept["Screen.level"]=="stale-host");
    r.screens.stopPreview();
    NX_EXPECT(!r.services.previewAssets&&r.services.save==&r.save);
}
#endif

#ifdef NX_DESKTOP_BLUEPRINT
NX_TEST(nativeSceneEditorSessionUsesInstalledScriptAndWritesOnlyProjectScenes)
{
    Rig r;NX_EXPECT(r.ok);if(!r.ok)return;
    r.platform.workspaceFiles=true;
    r.platform.files["showcase/main/screen.json"]="{\"title\":\"Home\"}";
    r.platform.files["showcase/main/main.as"]="class Home : Screen {}";
    // A small stand-in for the installed tool exercises the identical session and binding path.
    r.platform.files["showcase/editor/main.as"]=R"(class Editor : Screen {
        void onEnter() {
            Save::set("scene_loaded", Assets::read("asset:scene.json"));
            Save::set("allowed", Assets::write("asset:scene.json", "{\"ground\":2}")?"yes":"no");
            Save::set("blocked", Assets::write("asset:other.json", "{}")?"no":"yes");
            Save::set("loose", Assets::list("own:").length()==0?"hidden":"visible");
            Assets::forget("asset:scene.json");
        }
    })";
    if(!r.screens.init(r.services,"showcase","main")){NX_EXPECT(false);return;}
    r.step(3);r.screens.setDevelopmentPaused(true);
    std::string root,error;
    NX_EXPECT(blueprintCreateAssetRoot(r.platform,root,error)&&blueprintPrepareSceneEditor(r.platform,root,"Screen",error));
    const auto base="own/"+root+"/Screen/asset/";
    r.platform.kept["Screen.scene"]="old-host";
    NX_EXPECT(r.screens.startSceneEditor(root,"Screen",error));
    if(!r.screens.previewing())return;
    r.step(3);
    NX_EXPECT(!r.screens.failed()&&r.services.sceneEditorAssets&&r.services.previewAssets);
    NX_EXPECT(strcmp(r.services.save->get("allowed",""),"yes")==0&&strcmp(r.services.save->get("blocked",""),"yes")==0);
    NX_EXPECT(strcmp(r.services.save->get("loose",""),"hidden")==0);
    NX_EXPECT(strstr(r.services.save->get("scene_loaded",""),"entities")!=nullptr);
    NX_EXPECT(r.platform.files[base+"scene.json"]=="{\"ground\":2}");
    NX_EXPECT(r.platform.files.count(base+".scene.json.backup")&&r.platform.files.count(base+"other.json")==0);
    NX_EXPECT(r.platform.kept["Screen.scene"]=="old-host");
    r.screens.stopPreview();
    NX_EXPECT(!r.services.sceneEditorAssets&&!r.services.previewAssets&&r.services.save==&r.save);
    NX_EXPECT(blueprintPrepareSceneEditor(r.platform,root,"Screen",error));
    r.platform.files[base+"scenes.json"]="{\"scenes\":[{\"name\":\"bad\",\"file\":\"../other\"}]}";
    NX_EXPECT(!blueprintPrepareSceneEditor(r.platform,root,"Screen",error));
    NX_EXPECT(r.platform.files[base+"scene.json"]=="{\"ground\":2}");
}
#endif

#ifdef NX_DESKTOP_BLUEPRINT
NX_TEST(nativeInstalledSceneEditorOpensAndSavesRealScene)
{
    Rig r(128*1024*1024);NX_EXPECT(r.ok);if(!r.ok)return;
    r.host.setStatementLimit(5000000);
    r.platform.workspaceFiles=true;
    r.platform.files["showcase/main/screen.json"]="{\"title\":\"Home\"}";
    r.platform.files["showcase/main/main.as"]="class Home : Screen {}";
    if(!r.screens.init(r.services,"showcase","main")){NX_EXPECT(false);return;}
    r.step(3);r.screens.setDevelopmentPaused(true);
    std::string root,error;
    if(!blueprintCreateAssetRoot(r.platform,root,error)||!blueprintPrepareSceneEditor(r.platform,root,"Screen",error))
    {NX_EXPECT(false);return;}
    // Do not substitute the script or theme: use the media submodule selected by Redux.
    NX_EXPECT(r.screens.startSceneEditor(root,"Screen",error));
    if(!r.screens.previewing())return;
    for(int i=0;i<1000&&r.screens.loading()&&!r.screens.failed();++i)
    {
        r.step();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    NX_EXPECT(!r.screens.failed()&&!r.screens.loading());
    if(!r.screens.failed()&&!r.screens.loading())
    {
        r.step(2);
        auto* object=r.screens.object();
        auto* method=object?object->GetObjectType()->GetMethodByDecl("void onSave(Widget@)"):nullptr;
        NX_EXPECT(method&&r.host.callMethod(object,method,static_cast<void*>(nullptr)));
        const auto path="own/"+root+"/Screen/asset/scene.json";
        NX_EXPECT(r.platform.files.count(path)&&r.platform.files[path].find("\"entities\"")!=std::string::npos);
        NX_EXPECT(r.platform.files.count("own/"+root+"/Screen/asset/.scene.json.backup"));
    }
    r.screens.stopPreview();
    NX_EXPECT(r.screens.depth()==1&&r.services.save==&r.save&&!r.services.sceneEditorAssets);
}
#endif

#ifdef NX_DESKTOP_BLUEPRINT
NX_TEST(nativeLevelGraphLoadsAProjectSceneThroughOriginalLevelHelper)
{
    Rig r(128*1024*1024);NX_EXPECT(r.ok);if(!r.ok)return;
    r.host.setStatementLimit(5000000);
    r.platform.workspaceFiles=true;
    r.platform.files["showcase/main/screen.json"]="{\"title\":\"Home\"}";
    r.platform.files["showcase/main/main.as"]="class Home : Screen {}";
    if(!r.screens.init(r.services,"showcase","main")){NX_EXPECT(false);return;}
    r.step(3);r.screens.setDevelopmentPaused(true);
    std::string root,error,source;
    if(!blueprintCreateAssetRoot(r.platform,root,error)||!blueprintPrepareSceneEditor(r.platform,root,"Screen",error))
    {NX_EXPECT(false);return;}
    BlueprintDocument doc;doc.create();
    bool ok=doc.addNode(0,"level:load",0,0,error)&&doc.setInputValue(0,1,"file","asset:scene.json",error)&&doc.connect(0,0,"then",1,"in",error)&&
        doc.addNode(0,"branch",0,0,error)&&doc.connect(0,1,"then",2,"in",error)&&doc.connect(0,1,"result",2,"condition",error)&&
        doc.addNode(0,"api:Save:Save::set:string,string",0,0,error)&&doc.connect(0,2,"true",3,"in",error)&&
        doc.setInputValue(0,3,"key","level_loaded",error)&&doc.setInputValue(0,3,"value","yes",error)&&
        doc.addNode(1,"level:update",0,0,error)&&doc.addNode(1,"event:delta",0,0,error)&&doc.connect(1,0,"then",1,"in",error)&&doc.connect(1,2,"value",1,"dt",error)&&
        compileBlueprint(doc,source,error);
    NX_EXPECT(ok);if(!ok)return;
    // Missing include must not contaminate the next compile.
    NX_EXPECT(!r.screens.validateBlueprintSource("#include \"global:scene.as\"\n#include \"global:missing-test.as\"\nclass Test : Screen {}",error));
    NX_EXPECT(r.screens.validateBlueprintSource(source,error));
    Screens::PreviewScreen p;snprintf(p.info.id,sizeof(p.info.id),"Screen");p.source=source;
    NX_EXPECT(r.screens.startProjectPreview({p},"Screen",error,root));if(!r.screens.previewing())return;
    for(int i=0;i<1000&&r.screens.loading()&&!r.screens.failed();++i)
    {r.step();std::this_thread::sleep_for(std::chrono::milliseconds(10));}
    r.step(2);
    NX_EXPECT(!r.screens.failed()&&!r.screens.loading());
    NX_EXPECT(strcmp(r.services.save->get("level_loaded","missing"),"yes")==0);
    r.screens.stopPreview();
}
#endif

#ifdef NX_DESKTOP_BLUEPRINT
NX_TEST(nativeLevelTriggerDelegatesAndNamedBehaviourRunInOriginalLevel)
{
    Rig r(128*1024*1024);NX_EXPECT(r.ok);if(!r.ok)return;
    r.host.setStatementLimit(5000000);r.platform.workspaceFiles=true;
    r.platform.files["showcase/main/screen.json"]="{\"title\":\"Home\"}";
    r.platform.files["showcase/main/main.as"]="class Home : Screen {}";
    if(!r.screens.init(r.services,"showcase","main")){NX_EXPECT(false);return;}
    r.step(3);r.screens.setDevelopmentPaused(true);
    std::string root,error,source;
    if(!blueprintCreateAssetRoot(r.platform,root,error)||!blueprintEnsureAssetFolder(r.platform,root,"Screen",error))
    {NX_EXPECT(false);return;}
    r.platform.files["own/"+root+"/Screen/asset/scene.json"]=R"({"ground":0,"entities":[{"name":"door","shape":"box","place":[0,0,0],"size":[1,1,1]},{"name":"target","trigger":true,"when":"shot","once":true,"place":[0,0,0],"size":[4,4,4]}]})";
    BlueprintDocument doc;doc.create();bool ok=doc.addLevelEvents("Screen",error)&&doc.addBehaviour("Door",error);
    auto add=[&](size_t g,const char* kind){ok=ok&&doc.addNode(g,kind,0,0,error);};
    auto wire=[&](size_t g,size_t a,const char* out,size_t b,const char* in){ok=ok&&doc.connect(g,a,out,b,in,error);};
    auto input=[&](size_t g,size_t n,const char* pin,const char* value){ok=ok&&doc.setInputValue(g,n,pin,value,error);};
    add(0,"level:load");input(0,1,"file","asset:scene.json");wire(0,0,"then",1,"in");
    add(0,"level:attach");input(0,2,"name","door");wire(0,1,"then",2,"in");
    add(0,"level:attach");input(0,3,"name","missing");wire(0,2,"then",3,"in");
    add(0,"branch");wire(0,3,"then",4,"in");wire(0,3,"result",4,"condition");
    add(0,"api:Save:Save::set:string,string");input(0,5,"key","missing_attach");input(0,5,"value","safe");wire(0,4,"false",5,"in");
    add(1,"level:update");input(1,1,"dt","0.016");input(1,1,"shot","true");wire(1,0,"then",1,"in");
    add(6,"event:levelName");add(6,"api:Save:Save::set:string,string");wire(6,0,"then",2,"in");wire(6,1,"value",2,"key");input(6,2,"value","hit");
    add(8,"api:Save:Save::set:string,string");wire(8,0,"then",1,"in");input(8,1,"key","door_behaviour");input(8,1,"value","started");
    NX_EXPECT(ok&&compileBlueprint(doc,source,error));if(!ok||source.empty())return;
    NX_EXPECT(r.screens.validateBlueprintSource(source,error));
    Screens::PreviewScreen p;snprintf(p.info.id,sizeof(p.info.id),"Screen");p.source=source;
    NX_EXPECT(r.screens.startProjectPreview({p},"Screen",error,root));if(!r.screens.previewing())return;
    for(int i=0;i<1000&&r.screens.loading()&&!r.screens.failed();++i)
    {r.step();std::this_thread::sleep_for(std::chrono::milliseconds(10));}
    r.step(3);
    NX_EXPECT(!r.screens.failed());
    NX_EXPECT(strcmp(r.services.save->get("door_behaviour","missing"),"started")==0);
    NX_EXPECT(strcmp(r.services.save->get("missing_attach","missing"),"safe")==0);
    NX_EXPECT(strcmp(r.services.save->get("target","missing"),"hit")==0);
    r.screens.stopPreview();
}
#endif

NX_TEST(nativeBehaviourAttachmentWithNullEntityReturnsFalse)
{
    BlueprintDocument doc;doc.create();std::string error,source;
    bool ok=doc.addBehaviour("Actor",error)&&doc.addNode(0,"var:get:Entity@",0,0,error)&&
        doc.addNode(0,"attach:behaviour",0,0,error)&&doc.connect(0,1,"value",2,"target",error)&&doc.connect(0,0,"then",2,"in",error)&&
        doc.addNode(0,"branch",0,0,error)&&doc.connect(0,2,"then",3,"in",error)&&doc.connect(0,2,"result",3,"condition",error)&&
        doc.addNode(0,"api:Save:Save::set:string,string",0,0,error)&&doc.connect(0,3,"false",4,"in",error)&&
        doc.setInputValue(0,4,"key","null_attachment",error)&&doc.setInputValue(0,4,"value","safe",error)&&compileBlueprint(doc,source,error);
    NX_EXPECT(ok);if(!ok)return;
    Rig r;NX_EXPECT(r.ok);if(!r.ok)return;
    r.platform.files["showcase/attach/screen.json"]="{\"title\":\"Attach\"}";
    r.platform.files["showcase/attach/main.as"]=source;
    if(!r.screens.init(r.services,"showcase","attach")){NX_EXPECT(false);return;}
    r.step(3);NX_EXPECT(!r.screens.failed()&&strcmp(r.save.get("null_attachment","missing"),"safe")==0);
}

#ifdef NX_DESKTOP_BLUEPRINT
NX_TEST(nativeDebuggerLocalTraceUsesRealStatementCallbacksWithoutNetwork)
{
    Rig r;NX_EXPECT(r.ok);if(!r.ok)return;
    ScriptDebugger debugger;
    struct Trace { ScriptDebugger* debugger; unsigned hits=0; bool found=false,global=false,stack=false; } trace{&debugger};
    NX_EXPECT(debugger.startLocalTrace(r.host,[](void* user,const char* section,int line,u32,asIScriptContext* context)
    {
        auto& state=*static_cast<Trace*>(user);
        if(strcmp(section,"blueprint.as")||line<=0)return;
        ++state.hits;std::vector<std::string> values;state.debugger->traceValues(context,values);
        for(const auto& v:values){if(v=="amount = 42")state.found=true;if(v=="global counter = 7")state.global=true;}
        std::vector<std::string> frames;state.debugger->traceStack(context,frames);
        if(frames.size()>=2&&frames[0].find("sample")!=std::string::npos&&frames[1].find("check")!=std::string::npos)state.stack=true;
    },&trace));
    NX_EXPECT(!debugger.listening()&&!debugger.connected());
    NX_EXPECT(compile(r.host,"int bp_global_counter=7; void sample() { int amount = 42;\n Save::set(\"traced\", amount==42 ? \"yes\" : \"no\");\n } void check(){sample();}"));
    NX_EXPECT(r.host.call(r.host.find("blueprint","void check()")));
    NX_EXPECT(trace.hits>0&&trace.found&&trace.global&&trace.stack&&strcmp(r.save.get("traced","missing"),"yes")==0);
    debugger.stopLocalTrace();const auto hits=trace.hits;
    NX_EXPECT(r.host.call(r.host.find("blueprint","void check()"))&&trace.hits==hits);
}
#endif

#ifdef NX_DESKTOP_BLUEPRINT
NX_TEST(nativeWorkspaceWatchPausesAtBoundaryAndStepsWithoutRunningWhilePaused)
{
    Rig r;NX_EXPECT(r.ok);if(!r.ok)return;
    ScriptDebugger debugger;r.services.debugger=&debugger;
    r.platform.workspaceFiles=true;
    r.platform.files["showcase/main/screen.json"]="{\"title\":\"Home\"}";
    r.platform.files["showcase/main/main.as"]="class Home : Screen {}";
    if(!r.screens.init(r.services,"showcase","main")){NX_EXPECT(false);return;}
    r.step(3);r.screens.setDevelopmentPaused(true);
    BlueprintDocument doc;doc.create();std::string error,bytes;
    bool ok=doc.addNode(0,"api:Save:Save::set:string,string",350,240,error)&&
        doc.setInputValue(0,1,"key","watch_test",error)&&doc.setInputValue(0,1,"value","executed",error)&&
        doc.connect(0,0,"then",1,"in",error)&&doc.toggleWatch(0,1)&&
        doc.addNode(1,"var:set:int",350,240,error)&&doc.setInputValue(1,1,"value","5",error)&&doc.connect(1,0,"then",1,"in",error)&&
        doc.serialize(bytes,error);
    NX_EXPECT(ok);if(!ok)return;
    r.platform.files["own/trace.bp.json"]=bytes;
    BlueprintWorkspace studio;studio.open(r.services);
    NX_EXPECT(studio.active());if(!studio.active())return;
    auto& state=r.platform.state;state.hasMouse=true;
    auto frame=[&]()
    {
        r.assets.finish();r.input.update(state,1.0f/60);r.platform.now+=1.0/60;
        if(studio.previewing())studio.updatePreview(r.services,1.0f/60);else studio.update(r.services);
        r.device->beginFrame({});r.canvas.begin();
        if(!studio.previewing()||studio.tracePaused())studio.draw(r.canvas);else r.screens.draw();
        if(studio.previewing())studio.drawPreviewOverlay(r.canvas);
        r.canvas.end();r.device->endFrame();
    };
    auto click=[&](float x,float y)
    {
        state.mouseX=x;state.mouseY=y;state.mouseButtons[0]=true;frame();state.mouseButtons[0]=false;frame();
    };
    frame();r.platform.dropped="trace.bp.json";frame();
    click(1224,18);click(553,53);
    for(int i=0;i<10&&studio.previewing()&&!studio.tracePaused();++i)frame();
    NX_EXPECT(studio.previewing()&&studio.tracePaused());
    if(studio.tracePaused())
    {
        // This is deliberately an update-boundary watch: the watched action has already run.
        NX_EXPECT(strcmp(r.services.save->get("watch_test","missing"),"executed")==0);
        const auto stopped=r.host.runs();
        frame();frame();NX_EXPECT(r.host.runs()==stopped);
        click(934,398);click(120,435);click(1140,398);click(720,398);NX_EXPECT(r.host.runs()==stopped&&studio.tracePaused());
        click(566,640);
        for(int i=0;i<3&&!studio.tracePaused();++i)frame();
        NX_EXPECT(studio.tracePaused()&&r.host.runs()>stopped);
        const auto stepped=r.host.runs();frame();NX_EXPECT(r.host.runs()==stepped);
        click(360,640);frame();NX_EXPECT(studio.previewing()&&!studio.tracePaused()&&r.host.runs()>stepped);
    }
    studio.stopPreview(r.services);studio.close(r.services);
    NX_EXPECT(!studio.previewing()&&!studio.tracePaused()&&r.services.save==&r.save);
    NX_EXPECT(strcmp(r.save.get("watch_test","missing"),"missing")==0);
}
#endif

NX_TEST(nativeImportedScreenAndOriginalProduceSameState)
{
    const std::string original=R"(class Original : Screen {
        int score;
        void onEnter() { score = 1; Save::set("start", "yes"); }
        void update(float elapsed) {
            if (score < 3 && elapsed > 0.0f) { score = score + 1; }
            else { Save::set("result", "done"); }
            Save::set("after", "yes");
        }
    })";
    BlueprintDocument doc;std::string error,generated;
    NX_EXPECT(importBlueprintScreen(original,doc,error)&&compileBlueprint(doc,generated,error));if(generated.empty())return;
    for(const std::string* code:std::vector<const std::string*>{&original,&generated})
    {
        Rig r;NX_EXPECT(r.ok);if(!r.ok)return;
        r.platform.files["showcase/imported/screen.json"]="{\"title\":\"Import\"}";
        r.platform.files["showcase/imported/main.as"]=*code;
        if(!r.screens.init(r.services,"showcase","imported")){NX_EXPECT(false);return;}
        r.step(8);
        NX_EXPECT(!r.screens.failed()&&strcmp(r.save.get("start",""),"yes")==0&&strcmp(r.save.get("result",""),"done")==0&&strcmp(r.save.get("after",""),"yes")==0);
    }
}

#ifdef NX_DESKTOP_BLUEPRINT
NX_TEST(nativeWorkspaceImportsSourceWithoutOverwritingOriginal)
{
    Rig r;NX_EXPECT(r.ok);if(!r.ok)return;
    r.platform.workspaceFiles=true;
    r.platform.files["showcase/main/screen.json"]="{\"title\":\"Home\"}";
    r.platform.files["showcase/main/main.as"]="class Home : Screen {}";
    if(!r.screens.init(r.services,"showcase","main")){NX_EXPECT(false);return;}
    r.step(3);r.screens.setDevelopmentPaused(true);
    const std::string original="class Imported : Screen { int amount; void onEnter() { amount = Pick(true); if (amount == 7) { Save::set(\"imported\", \"yes\"); } } int Pick(bool flag) { if(flag) {return 7;} return 0; } }";
    r.platform.files["own/original.as"]=original;
    BlueprintWorkspace studio;studio.open(r.services);NX_EXPECT(studio.active());if(!studio.active())return;
    auto& state=r.platform.state;state.hasMouse=true;
    auto frame=[&]()
    {
        r.assets.finish();r.input.update(state,1.0f/60);r.platform.now+=1.0/60;
        if(studio.previewing())studio.updatePreview(r.services,1.0f/60);else studio.update(r.services);
        studio.checkpoint(r.services);
        r.device->beginFrame({});r.canvas.begin();
        if(!studio.previewing()||studio.tracePaused())studio.draw(r.canvas);else r.screens.draw();
        if(studio.previewing())studio.drawPreviewOverlay(r.canvas);
        r.canvas.end();r.device->endFrame();
    };
    auto click=[&](float x,float y){state.mouseX=x;state.mouseY=y;state.mouseButtons[0]=true;frame();state.mouseButtons[0]=false;frame();};
    r.platform.dropped="original.as";frame();
    const auto recovery=r.platform.files.find("own/blueprint-recovery.bp.json");
    NX_EXPECT(recovery!=r.platform.files.end());
    if(recovery!=r.platform.files.end())
    {
        BlueprintDocument imported;std::string error;
        NX_EXPECT(imported.load(recovery->second.data(),recovery->second.size(),error)&&imported.function("Screen","Pick"));
    }
    click(553,53);
    NX_EXPECT(studio.previewing());
    if(studio.previewing())
    {
        for(int i=0;i<4;++i)frame();
        NX_EXPECT(strcmp(r.services.save->get("imported","missing"),"yes")==0);
        studio.stopPreview(r.services);
    }
    NX_EXPECT(r.platform.files["own/original.as"]==original&&strcmp(r.save.get("imported","missing"),"missing")==0);
    studio.close(r.services);
}
#endif

NX_TEST(nativeImportedLocalSnapshotsAndActionResultsMatchOriginal)
{
    const std::string original=R"(class Original : Screen {
        int score;
        void onEnter() {
            score = 3;
            int initial = score;
            score = 8;
            string label = Save::get("original", "old");
            Save::set("original", "new");
            if(initial == 3) { Save::set("captured", label); }
            Entity@ actor = Scene::spawn(Vec2(0.0f, 0.0f));
            actor.name = "imported";
            Save::set("actor", actor.name);
        }
    })";
    BlueprintDocument doc;std::string error,generated;
    const bool imported=importBlueprintScreen(original,doc,error);
    if(!imported)NX_ERROR("Import diagnostic: %s",error.c_str());
    NX_EXPECT(imported&&compileBlueprint(doc,generated,error));if(generated.empty())return;
    for(const std::string* source:std::vector<const std::string*>{&original,&generated})
    {
        Rig r;NX_EXPECT(r.ok);if(!r.ok)return;
        r.platform.files["showcase/imported/screen.json"]="{\"title\":\"Import\"}";
        r.platform.files["showcase/imported/main.as"]=*source;
        if(!r.screens.init(r.services,"showcase","imported")){NX_EXPECT(false);return;}
        r.step(5);
        NX_EXPECT(!r.screens.failed()&&strcmp(r.save.get("captured",""),"old")==0&&strcmp(r.save.get("original",""),"new")==0);
        NX_EXPECT(strcmp(r.save.get("actor",""),"imported")==0);
    }
}

NX_TEST(nativeImportedMutableLocalsAndHandleReassignmentMatchOriginal)
{
    const std::string original=R"(class Example : Screen {
        void onEnter() {
            int total = 1;
            if(true) { total += 2; } else { total = 8; }
            total++;
            if(total == 4) { Save::set("local", "correct"); }
            Entity@ actor = Scene::spawn(Vec2(0.0f, 0.0f));
            actor.name = "first";
            Entity@ previous = actor;
            @actor = Scene::spawn(Vec2(10.0f, 0.0f));
            actor.name = "second";
            Save::set("first", previous.name);
            Save::set("second", actor.name);
        }
    })";
    BlueprintDocument doc;std::string error,generated;
    const bool imported=importBlueprintScreen(original,doc,error);
    if(!imported)NX_ERROR("Import diagnostic: %s",error.c_str());
    NX_EXPECT(imported&&compileBlueprint(doc,generated,error));if(generated.empty())return;
    for(const std::string* source:std::vector<const std::string*>{&original,&generated})
    {
        Rig r;NX_EXPECT(r.ok);if(!r.ok)return;
        r.platform.files["showcase/imported/screen.json"]="{\"title\":\"Import\"}";
        r.platform.files["showcase/imported/main.as"]=*source;
        if(!r.screens.init(r.services,"showcase","imported")){NX_EXPECT(false);return;}
        r.step(5);
        NX_EXPECT(!r.screens.failed()&&strcmp(r.save.get("local",""),"correct")==0);
        NX_EXPECT(strcmp(r.save.get("first",""),"first")==0&&strcmp(r.save.get("second",""),"second")==0);
    }
}

NX_TEST(nativeImportedBoundedLoopsMatchOriginalExecution)
{
    const std::string original=R"(class Example : Screen {
        void onEnter() {
            int sum = 0;
            for(int i=0; i<3; ++i) {
                for(int j=0; j<2; j++) { sum += i+j; }
            }
            for(int k=0; k<0; ++k) { sum = 999; }
            if(sum == 9) { Save::set("loops", "correct"); }
        }
    })";
    BlueprintDocument doc;std::string error,generated;
    const bool imported=importBlueprintScreen(original,doc,error);
    if(!imported)NX_ERROR("Import diagnostic: %s",error.c_str());
    NX_EXPECT(imported&&compileBlueprint(doc,generated,error));if(generated.empty())return;
    for(const std::string* source:std::vector<const std::string*>{&original,&generated})
    {
        Rig r;NX_EXPECT(r.ok);if(!r.ok)return;
        r.platform.files["showcase/imported/screen.json"]="{\"title\":\"Import\"}";
        r.platform.files["showcase/imported/main.as"]=*source;
        if(!r.screens.init(r.services,"showcase","imported")){NX_EXPECT(false);return;}
        r.step(3);NX_EXPECT(!r.screens.failed()&&strcmp(r.save.get("loops",""),"correct")==0);
    }
}

NX_TEST(nativeImportedElseIfChainsMatchOriginalExecution)
{
    const std::string original=R"(class Example : Screen {
        int Pick(int score) {
            if(score < 0) { return 10; }
            else if(score == 0) { return 20; }
            else if(score == 1) { return 30; }
            else { return 40; }
        }
        int Optional(int score) {
            if(score == 1) { return 5; }
            else if(score == 2) { return 6; }
            return 7;
        }
        void onEnter() {
            int first = Pick(-1);
            int second = Pick(0);
            int third = Pick(1);
            int fourth = Pick(2);
            int fallback = Optional(3);
            int other = Optional(2);
            int total = 0;
            if(first == 10) { total = 1; }
            else if(second == 20) { total = 99; }
            if(second == 0) { total = 99; }
            else if(third == 30) { total += 2; }
            else { total = 99; }
            if(fourth == 40 && fallback == 7 && other == 6 && total == 3) {
                Save::set("else_if", "correct");
            }
        }
    })";
    BlueprintDocument doc;std::string error,generated;
    const bool imported=importBlueprintScreen(original,doc,error);
    if(!imported)NX_ERROR("Import diagnostic: %s",error.c_str());
    NX_EXPECT(imported&&compileBlueprint(doc,generated,error));if(generated.empty())return;
    for(const std::string* source:std::vector<const std::string*>{&original,&generated})
    {
        Rig r;NX_EXPECT(r.ok);if(!r.ok)return;
        r.platform.files["showcase/imported/screen.json"]="{\"title\":\"Import\"}";
        r.platform.files["showcase/imported/main.as"]=*source;
        if(!r.screens.init(r.services,"showcase","imported")){NX_EXPECT(false);return;}
        r.step(3);
        NX_EXPECT(!r.screens.failed()&&strcmp(r.save.get("else_if",""),"correct")==0);
    }
}

NX_TEST(nativeImportedSingleStatementControlFlowMatchesOriginal)
{
    const std::string original=R"(class Example : Screen {
        int score;
        int Pick(bool flag) {
            if(flag) return 3;
            else return 4;
        }
        void onEnter() {
            int total = 0;
            if(true)
                if(false) total = 99;
                else ++total;
            if(false)
                if(true) total = 99;
                else total = 99;
            for(int i=0;i<3;++i) total += i;
            for(int j=0;j<0;++j) total = 99;
            ++score;
            --score;
            int picked = Pick(true);
            int other = Pick(false);
            if(total == 4 && score == 0 && picked == 3 && other == 4)
                Save::set("single_statement", "correct");
        }
    })";
    BlueprintDocument doc;std::string error,generated;
    const bool imported=importBlueprintScreen(original,doc,error);
    if(!imported)NX_ERROR("Import diagnostic: %s",error.c_str());
    NX_EXPECT(imported&&compileBlueprint(doc,generated,error));if(generated.empty())return;
    for(const std::string* source:std::vector<const std::string*>{&original,&generated})
    {
        Rig r;NX_EXPECT(r.ok);if(!r.ok)return;
        r.platform.files["showcase/imported/screen.json"]="{\"title\":\"Import\"}";
        r.platform.files["showcase/imported/main.as"]=*source;
        if(!r.screens.init(r.services,"showcase","imported")){NX_EXPECT(false);return;}
        r.step(3);
        NX_EXPECT(!r.screens.failed()&&strcmp(r.save.get("single_statement",""),"correct")==0);
    }
}

NX_TEST(nativeImportedExplicitInstanceReferencesMatchOriginal)
{
    const std::string original=R"(class Example : Screen {
        int score;
        void Set(int score) { this.score = score; }
        int Read(int score) { return this.score + score; }
        void onEnter() {
            this.Set(4);
            ++this.score;
            this.score += 2;
            int result = this.Read(3);
            if(this.score == 7 && result == 10)
                Save::set("instance", "correct");
        }
    })";
    BlueprintDocument doc;std::string error,generated;
    const bool imported=importBlueprintScreen(original,doc,error);
    if(!imported)NX_ERROR("Import diagnostic: %s",error.c_str());
    NX_EXPECT(imported&&compileBlueprint(doc,generated,error));if(generated.empty())return;
    for(const std::string* source:std::vector<const std::string*>{&original,&generated})
    {
        Rig r;NX_EXPECT(r.ok);if(!r.ok)return;
        r.platform.files["showcase/imported/screen.json"]="{\"title\":\"Import\"}";
        r.platform.files["showcase/imported/main.as"]=*source;
        if(!r.screens.init(r.services,"showcase","imported")){NX_EXPECT(false);return;}
        r.step(3);
        NX_EXPECT(!r.screens.failed()&&strcmp(r.save.get("instance",""),"correct")==0);
    }
}

NX_TEST(nativeImportedStandaloneBlocksAndInclusiveLoopsMatchOriginal)
{
    const std::string original=R"(class Example : Screen {
        void onEnter() {
            int total=1;
            { int local=2; total+=local; }
            { int local=3; total+=local; }
            for(int i=0;i<=2;++i) total+=i;
            if(total==9) Save::set("scoped","yes");
        }
    })";
    BlueprintDocument doc;std::string error,generated;
    NX_EXPECT(importBlueprintScreen(original,doc,error)&&compileBlueprint(doc,generated,error));if(generated.empty())return;
    for(const std::string* source:std::vector<const std::string*>{&original,&generated})
    {
        Rig r;NX_EXPECT(r.ok);if(!r.ok)return;
        r.platform.files["showcase/imported/screen.json"]="{\"title\":\"Import\"}";
        r.platform.files["showcase/imported/main.as"]=*source;
        if(!r.screens.init(r.services,"showcase","imported")){NX_EXPECT(false);return;}
        r.step(3);NX_EXPECT(!r.screens.failed()&&strcmp(r.save.get("scoped",""),"yes")==0);
    }
}

#ifdef NX_DESKTOP_BLUEPRINT
NX_TEST(nativeSourceCompanionAssetsCopyOriginalTree)
{
    Rig r;NX_EXPECT(r.ok);if(!r.ok)return;
    r.platform.workspaceFiles=true;
    NX_EXPECT(r.platform.createOwnFolder("original"));
    NX_EXPECT(r.platform.createOwnFolder("original/asset"));
    const std::string binary("x\0y",3);
    r.platform.files["own/original/asset/model/mesh.glb"]=binary;
    r.platform.files["own/original/asset/level.json"]="data";
    std::string root,error;
    NX_EXPECT(blueprintImportSourceAssets(r.platform,"original.as",root,error)&&!root.empty());
    NX_EXPECT(r.platform.files["own/"+root+"/Screen/asset/model/mesh.glb"]==binary);
    NX_EXPECT(r.platform.files["own/"+root+"/Screen/asset/level.json"]=="data");
    NX_EXPECT(r.platform.files["own/original/asset/model/mesh.glb"]==binary);
    std::string none;
    NX_EXPECT(blueprintImportSourceAssets(r.platform,"missing.as",none,error)&&none.empty());
    r.platform.files["own/original/asset/Bad.bin"]="invalid";
    NX_EXPECT(!blueprintImportSourceAssets(r.platform,"original.as",none,error)&&none.empty());
}
#endif

#ifdef NX_DESKTOP_BLUEPRINT
NX_TEST(nativeWorkspaceRepeatedRecoveryPreviewAndFailedCheckpointRetry)
{
    Rig r;NX_EXPECT(r.ok);if(!r.ok)return;
    r.platform.workspaceFiles=true;
    r.platform.files["showcase/main/screen.json"]="{\"title\":\"Home\"}";
    r.platform.files["showcase/main/main.as"]="class Home : Screen {}";
    if(!r.screens.init(r.services,"showcase","main")){NX_EXPECT(false);return;}
    r.step(3);r.screens.setDevelopmentPaused(true);
    const std::string original=R"(class Original : Screen {
        void onEnter() { Save::set("recovery_cycle","ok"); }
    })";
    r.platform.files["own/original.as"]=original;
    const std::string recovery="own/blueprint-recovery.bp.json";
    auto& state=r.platform.state;state.hasMouse=true;
    std::string saved;
    for(int cycle=0;cycle<8;++cycle)
    {
        // Construct a fresh workspace each time so no in-memory document survives.
        BlueprintWorkspace studio;studio.open(r.services);
        NX_EXPECT(studio.active());if(!studio.active())return;
        auto frame=[&]()
        {
            r.assets.finish();r.input.update(state,1.0f/60);r.platform.now+=1.0/60;
            if(studio.previewing())studio.updatePreview(r.services,1.0f/60);
            else studio.update(r.services);
            studio.checkpoint(r.services);
            r.device->beginFrame({});r.canvas.begin();
            if(!studio.previewing()||studio.tracePaused())studio.draw(r.canvas);else r.screens.draw();
            if(studio.previewing())studio.drawPreviewOverlay(r.canvas);
            r.canvas.end();r.device->endFrame();
        };
        auto click=[&](float x,float y)
        {
            state.mouseX=x;state.mouseY=y;state.mouseButtons[0]=true;frame();
            state.mouseButtons[0]=false;frame();
        };
        frame();
        if(cycle==0)
        {
            r.platform.failOwnWrites=true;
            r.platform.dropped="original.as";frame();
            NX_EXPECT(!r.platform.files.count(recovery));
            NX_EXPECT(!studio.recoveryProtected());
            click(1200,53);
            NX_EXPECT(studio.active()&&!studio.recoveryProtected());
            r.platform.failOwnWrites=false;
            studio.checkpoint(r.services,true);
            NX_EXPECT(r.platform.files.count(recovery));
            NX_EXPECT(studio.recoveryProtected());
            if(!r.platform.files.count(recovery)){studio.close(r.services);return;}
            saved=r.platform.files[recovery];
        }
        NX_EXPECT(r.platform.files[recovery]==saved);
        click(553,53);
        NX_EXPECT(studio.previewing());
        if(studio.previewing())
        {
            for(int i=0;i<8;++i)frame();
            NX_EXPECT(!r.screens.failed());
            NX_EXPECT(strcmp(r.services.save->get("recovery_cycle","missing"),"ok")==0);
            studio.stopPreview(r.services);
        }
        NX_EXPECT(r.services.save==&r.save&&strcmp(r.save.get("recovery_cycle","missing"),"missing")==0);
        studio.close(r.services);
        NX_EXPECT(!studio.active()&&!studio.previewing());
        NX_EXPECT(r.platform.files[recovery]==saved&&r.platform.files["own/original.as"]==original);
    }
    BlueprintDocument draft;std::string error,source;
    NX_EXPECT(draft.load(saved.data(),saved.size(),error)&&compileBlueprint(draft,source,error));
    NX_EXPECT(source.find("recovery_cycle")!=std::string::npos);
}
#endif

#ifdef NX_DESKTOP_BLUEPRINT
NX_TEST(nativeWorkspaceArchivesDamagedRecoveryBeforeReplacingIt)
{
    Rig r;NX_EXPECT(r.ok);if(!r.ok)return;
    r.platform.workspaceFiles=true;
    r.platform.files["showcase/main/screen.json"]="{\"title\":\"Home\"}";
    r.platform.files["showcase/main/main.as"]="class Home : Screen {}";
    if(!r.screens.init(r.services,"showcase","main")){NX_EXPECT(false);return;}
    r.step(3);r.screens.setDevelopmentPaused(true);
    const std::string path="own/blueprint-recovery.bp.json";
    const std::string damaged="{\"version\":2,\"graphs\":[";
    r.platform.files[path]=damaged;
    r.platform.files["own/new.as"]="class NewScreen : Screen { void onEnter(){ Save::set(\"new\",\"ok\"); } }";
    BlueprintWorkspace studio;studio.open(r.services);
    NX_EXPECT(studio.active());if(!studio.active())return;
    NX_EXPECT(r.platform.files[path]==damaged);
    r.platform.failOwnWrites=true;
    r.platform.dropped="new.as";
    r.input.update(r.platform.state,1.0f/60);studio.update(r.services);
    NX_EXPECT(r.platform.files[path]==damaged);
    studio.checkpoint(r.services,true);
    NX_EXPECT(r.platform.files[path]==damaged);
    r.platform.failOwnWrites=false;
    studio.checkpoint(r.services,true);
    NX_EXPECT(r.platform.files[path]!=damaged);
    size_t archives=0;
    for(const auto& item:r.platform.files)
        if(item.first.rfind("own/blueprint-recovery-damaged-",0)==0)
        {++archives;NX_EXPECT(item.second==damaged);}
    NX_EXPECT(archives==1);
    BlueprintDocument doc;std::string error,code;
    const auto& bytes=r.platform.files[path];
    NX_EXPECT(doc.load(bytes.data(),bytes.size(),error)&&compileBlueprint(doc,code,error));
    NX_EXPECT(code.find("Save::set(\"new\", \"ok\")")!=std::string::npos);
    studio.close(r.services);
}
#endif

#ifdef NX_DESKTOP_BLUEPRINT
NX_TEST(nativeWorkspacePreservesExternallyReplacedRecoveryAcrossEditAndSave)
{
    Rig r;NX_EXPECT(r.ok);if(!r.ok)return;
    r.platform.workspaceFiles=true;
    r.platform.files["showcase/main/screen.json"]="{\"title\":\"Home\"}";
    r.platform.files["showcase/main/main.as"]="class Home : Screen {}";
    if(!r.screens.init(r.services,"showcase","main")){NX_EXPECT(false);return;}
    r.step(3);r.screens.setDevelopmentPaused(true);
    const std::string path="own/blueprint-recovery.bp.json";
    r.platform.files["own/original.as"]="class Example : Screen { void onEnter(){ Save::set(\"a\",\"b\"); } }";
    BlueprintWorkspace studio;studio.open(r.services);
    NX_EXPECT(studio.active());if(!studio.active())return;
    auto& state=r.platform.state;state.hasMouse=true;
    auto frame=[&]()
    {
        r.assets.finish();r.input.update(state,1.0f/60);r.platform.now+=1.0/60;
        studio.update(r.services);
        r.device->beginFrame({});r.canvas.begin();studio.draw(r.canvas);r.canvas.end();r.device->endFrame();
    };
    auto click=[&](float x,float y)
    {
        state.mouseX=x;state.mouseY=y;state.mouseButtons[0]=true;frame();
        state.mouseButtons[0]=false;frame();
    };
    auto type=[&](const char* value)
    {
        for(const char* p=value;*p;++p){state.type(static_cast<u32>(*p));frame();}
        state.type(13);frame();
    };
    frame();r.platform.dropped="original.as";frame();
    NX_EXPECT(r.platform.files.count(path));
    NX_EXPECT(studio.recoveryProtected());
    const auto originalCheckpoint=r.platform.files[path];
    // A missing checkpoint must be detected even with no new graph edit.
    r.platform.files.erase(path);
    r.platform.failOwnWrites=true;
    studio.checkpoint(r.services,true);
    NX_EXPECT(!studio.recoveryProtected()&&!r.platform.files.count(path));
    r.platform.failOwnWrites=false;
    studio.checkpoint(r.services,true);
    NX_EXPECT(studio.recoveryProtected()&&r.platform.files[path]==originalCheckpoint);
    BlueprintDocument external;external.create();external.name="External Draft";
    std::string other,error;
    NX_EXPECT(external.serialize(other,error));
    r.platform.files[path]=other;
    r.platform.failOwnWrites=true;
    studio.checkpoint(r.services,true);
    NX_EXPECT(!studio.recoveryProtected()&&r.platform.files[path]==other);
    r.platform.failOwnWrites=false;
    studio.checkpoint(r.services,true);
    NX_EXPECT(studio.recoveryProtected()&&r.platform.files[path]==originalCheckpoint);
    click(1100,130);type("literal:int");click(1100,167);
    studio.checkpoint(r.services,true);
    size_t archives=0;
    for(const auto& item:r.platform.files)
        if(item.first.rfind("own/blueprint-recovery-conflict-",0)==0)
        {++archives;NX_EXPECT(item.second==other);}
    NX_EXPECT(archives==1&&r.platform.files[path]!=other);
    BlueprintDocument current;
    const auto& bytes=r.platform.files[path];
    NX_EXPECT(current.load(bytes.data(),bytes.size(),error)&&current.graphs[0].nodes.size()==3);
    external.name="Another Draft";NX_EXPECT(external.serialize(other,error));
    r.platform.files[path]=other;
    click(900,18);type("Conflict Save");
    NX_EXPECT(r.platform.files.count("own/blueprint-project-Conflict Save.bp.json"));
    NX_EXPECT(r.platform.files[path]==other);
    studio.close(r.services);
    NX_EXPECT(r.platform.files[path]==other);
}
#endif

#ifdef NX_DESKTOP_BLUEPRINT
NX_TEST(nativeWorkspaceRejectsConcurrentLeaseAndReleasesOnClose)
{
    Rig r;NX_EXPECT(r.ok);if(!r.ok)return;
    r.platform.workspaceFiles=true;
    BlueprintWorkspace first,second;
    first.open(r.services);NX_EXPECT(first.active()&&r.platform.blueprintLease);
    if(!first.active())return;
    second.open(r.services);NX_EXPECT(!second.active()&&r.platform.blueprintLease);
    second.recover(r.services);second.close(r.services);
    NX_EXPECT(r.platform.blueprintLease);
    first.close(r.services);NX_EXPECT(!r.platform.blueprintLease);
    second.open(r.services);NX_EXPECT(second.active()&&r.platform.blueprintLease);
    second.close(r.services);NX_EXPECT(!r.platform.blueprintLease);
}
#endif

#ifdef NX_DESKTOP_BLUEPRINT
NX_TEST(nativeBlueprintDropNamesProtectManagedFilesAndKeepExtensions)
{
    NX_EXPECT(protectedBlueprintDrop("blueprint-recovery.bp.json"));
    NX_EXPECT(protectedBlueprintDrop("blueprint-project-test.bp.json"));
    NX_EXPECT(protectedBlueprintDrop("example.bp.json"));
    NX_EXPECT(!protectedBlueprintDrop("example.as"));
    NX_EXPECT(blueprintDropName("blueprint-recovery.bp.json",0)=="import-blueprint-recovery.bp.json");
    NX_EXPECT(blueprintFileName(blueprintDropName("blueprint-recovery.bp.json",0)));
    NX_EXPECT(blueprintDropName("blueprint-project-test.bp.json",1)=="import-blueprint-project-test-2.bp.json");
    NX_EXPECT(blueprintDropName("example.bp.json",2)=="example-3.bp.json");
    NX_EXPECT(blueprintDropName("example.as",1)=="example-2.as");
    NX_EXPECT(blueprintDropName("texture.png",1)=="texture-2.png");
}
#endif

#ifdef NX_DESKTOP_BLUEPRINT
NX_TEST(nativeWorkspaceBrowserRecoversArchivesAndImportsSource)
{
    Rig r;NX_EXPECT(r.ok);if(!r.ok)return;
    r.platform.workspaceFiles=true;
    r.platform.files["showcase/main/screen.json"]="{\"title\":\"Home\"}";
    r.platform.files["showcase/main/main.as"]="class Home : Screen {}";
    if(!r.screens.init(r.services,"showcase","main")){NX_EXPECT(false);return;}
    r.step(3);r.screens.setDevelopmentPaused(true);
    const std::string archive="own/blueprint-recovery-conflict-123-1.bak";
    const std::string corrupt="own/blueprint-recovery-damaged-123-2.bak";
    const std::string source="class Source : Screen { void onEnter(){ Save::set(\"source\",\"ok\"); } }";
    BlueprintDocument original;std::string error,bytes,code;
    NX_EXPECT(importBlueprintScreen("class Archived : Screen { void onEnter(){ Save::set(\"archive\",\"ok\"); } }",original,error));
    NX_EXPECT(original.serialize(bytes,error));
    r.platform.files[archive]=bytes;r.platform.files[corrupt]="broken";
    r.platform.files["own/example.as"]=source;
    BlueprintWorkspace studio;studio.open(r.services);
    NX_EXPECT(studio.active());if(!studio.active())return;
    auto& state=r.platform.state;state.hasMouse=true;
    auto frame=[&]()
    {
        r.assets.finish();r.input.update(state,1.0f/60);r.platform.now+=1.0/60;
        studio.update(r.services);studio.checkpoint(r.services);
        r.device->beginFrame({});r.canvas.begin();studio.draw(r.canvas);r.canvas.end();r.device->endFrame();
    };
    auto click=[&](float x,float y)
    {
        state.mouseX=x;state.mouseY=y;state.mouseButtons[0]=true;frame();
        state.mouseButtons[0]=false;frame();
    };
    auto type=[&](const char* value)
    {
        for(const char* p=value;*p;++p){state.type(static_cast<u32>(*p));frame();}
        state.type(13);frame();
    };
    frame();click(794,18);
    // A malformed archive does not replace the current document or close the browser.
    click(500,216);NX_EXPECT(r.platform.files[corrupt]=="broken");
    click(500,180);
    const std::string recovery="own/blueprint-recovery.bp.json";
    NX_EXPECT(r.platform.files.count(recovery)&&studio.recoveryProtected());
    BlueprintDocument restored;
    const auto restoredBytes=r.platform.files[recovery];
    NX_EXPECT(restored.load(restoredBytes.data(),restoredBytes.size(),error)&&compileBlueprint(restored,code,error));
    NX_EXPECT(code.find("Save::set(\"archive\", \"ok\")")!=std::string::npos);
    click(900,18);type("Restored Archive");
    NX_EXPECT(r.platform.files.count("own/blueprint-project-Restored Archive.bp.json"));
    NX_EXPECT(r.platform.files[archive]==bytes&&r.platform.files[corrupt]=="broken");
    // Documents sort first, then recovery archives, then standalone source.
    click(794,18);click(500,288);
    const auto importedBytes=r.platform.files[recovery];
    BlueprintDocument imported;
    NX_EXPECT(imported.load(importedBytes.data(),importedBytes.size(),error)&&compileBlueprint(imported,code,error));
    NX_EXPECT(code.find("Save::set(\"source\", \"ok\")")!=std::string::npos);
    NX_EXPECT(r.platform.files["own/example.as"]==source&&r.platform.files[archive]==bytes);
    studio.close(r.services);
}
#endif

#ifdef NX_DESKTOP_BLUEPRINT
NX_TEST(nativeWorkspaceImportsNativeExportScreensAndTheirAssets)
{
    // Ordinary IDs, the default Screen, a single default Screen, native main and Behaviour conflicts.
    for(int scenario=0;scenario<6;++scenario)
    {
        Rig r;NX_EXPECT(r.ok);if(!r.ok)return;
        r.platform.workspaceFiles=true;
        r.platform.files["showcase/main/screen.json"]="{\"title\":\"Home\"}";
        r.platform.files["showcase/main/main.as"]="class Home : Screen {}";
        if(!r.screens.init(r.services,"showcase","main")){NX_EXPECT(false);return;}
        r.step(3);r.screens.setDevelopmentPaused(true);
        NX_EXPECT(blueprintImportProjectFolderName("project-demo"));
        NX_EXPECT(blueprintImportProjectFolderName("bp-1790000000-1"));
        NX_EXPECT(!blueprintImportProjectFolderName("bp-1790000000-"));
        NX_EXPECT(!blueprintImportProjectFolderName("bp-1790000000-1-2"));
        NX_EXPECT(!blueprintImportProjectFolderName("project-"));
        NX_EXPECT(!blueprintImportProjectFolderName("bpa-1790000000-1"));
        const std::string folder="own/bp-1790000000-1";
        const std::string firstId=scenario==0?"intro":scenario>=3?"main":"Screen";
        const bool multiple=scenario!=2;
        const std::vector<std::string> expected=multiple?std::vector<std::string>{firstId,"level"}:std::vector<std::string>{firstId};
        const std::string first=scenario>=4?"class Actor : Behaviour {void start(){Save::set(\"actor\",\"first\");}} class Start : Screen {void onEnter(){Entity@ a=Scene::spawn(Vec2(0.0f,0.0f));a.add(Actor());}}":scenario==3?"class Start : Screen { void onEnter(){ LaunchLevel(); } }":multiple?"class Start : Screen { void onEnter(){ Screens::push(\"level\"); } }":"class Start : Screen { void onEnter(){ Save::set(\"start\",\"ok\"); } }";
        const std::string second=scenario>=4?std::string("class Actor : Behaviour {void start(){Save::set(\"actor\",\"")+(scenario==4?"first":"second")+"\");}} class LevelScreen : Screen {void onEnter(){Entity@ a=Scene::spawn(Vec2(0.0f,0.0f));a.add(Actor());}}":"class LevelScreen : Screen { void onEnter(){ Save::set(\"level\",\"ok\"); } }";
        const std::string originalManifest="{\n  \"title\": \"Original title\", \"category\": \"Game\", \"order\": 7, \"custom\": [1,2]\n}\n";
        r.platform.files[folder+"/"+firstId+"/screen.json"]=originalManifest;
        r.platform.ownFolders.insert(folder+"/_global");
        r.platform.files[folder+"/_global/shared/data.bin"]=std::string("shared\0bytes",12);
        const std::string entry=scenario==3?"#include \"global:shared/entry.as\"\n":first;
        if(scenario==3)r.platform.files[folder+"/_global/shared/entry.as"]="#include \"asset:entry.as\"\nvoid LaunchLevel(){ Save::set(\"helper\",\"ran\"); Screens::push(\"level\"); }\n";
        if(scenario==3)r.platform.files[folder+"/"+firstId+"/asset/entry.as"]=first;
        r.platform.files[folder+"/"+firstId+"/main.as"]=entry;
        if(multiple)r.platform.files[folder+"/level/main.as"]=second;
        for(const auto& screen:expected)
        {
            r.platform.ownFolders.insert(folder+"/"+screen+"/asset");
            r.platform.files[folder+"/"+screen+"/asset/data.bin"]=std::string(screen)+std::string("\0\xff",2);
        }
        BlueprintWorkspace studio;studio.open(r.services);
        NX_EXPECT(studio.active());if(!studio.active())return;
        auto& state=r.platform.state;state.hasMouse=true;
        auto frame=[&]()
        {
            r.assets.finish();r.input.update(state,1.0f/60);r.platform.now+=1.0/60;
            studio.update(r.services);studio.checkpoint(r.services);
            r.device->beginFrame({});r.canvas.begin();studio.draw(r.canvas);r.canvas.end();r.device->endFrame();
        };
        auto click=[&](float x,float y)
        {
            state.mouseX=x;state.mouseY=y;state.mouseButtons[0]=true;frame();
            state.mouseButtons[0]=false;frame();
        };
        frame();click(794,18);click(500,180);
        const std::string recovery="own/blueprint-recovery.bp.json";
        NX_EXPECT(r.platform.files.count(recovery)&&studio.recoveryProtected());
        BlueprintDocument project;std::string error;
        const auto bytes=r.platform.files[recovery];
        NX_EXPECT(project.load(bytes.data(),bytes.size(),error));
        std::vector<BlueprintScreenOutput> output;
        NX_EXPECT(compileBlueprintProject(project,output,error)&&output.size()==expected.size());
        NX_EXPECT(project.screens()==expected);
        NX_EXPECT(project.screenManifest(firstId)&&*project.screenManifest(firstId)==originalManifest);
        if(!output.empty())NX_EXPECT(output[0].manifest==originalManifest);
        NX_EXPECT(r.platform.files[folder+"/"+firstId+"/screen.json"]==originalManifest);
        NX_EXPECT(project.name=="bp-1790000000-1");
        const auto shared="own/"+project.assetRoot+"/_global/shared/data.bin";
        NX_EXPECT(r.platform.files[shared]==r.platform.files[folder+"/_global/shared/data.bin"]);
        std::string cloned;NX_EXPECT(blueprintCreateAssetRoot(r.platform,cloned,error)&&blueprintCopySharedAssets(r.platform,project.assetRoot,cloned,error));
        NX_EXPECT(r.platform.files["own/"+cloned+"/_global/shared/data.bin"]==r.platform.files[shared]);
        for(const auto& screen:expected)
            NX_EXPECT(r.platform.files["own/"+project.assetRoot+"/"+screen+"/asset/data.bin"]==r.platform.files[folder+"/"+screen+"/asset/data.bin"]);
        NX_EXPECT(r.platform.files[folder+"/"+firstId+"/main.as"]==entry);
        if(multiple)NX_EXPECT(r.platform.files[folder+"/level/main.as"]==second);
        if(scenario>=4&&output.size()==2)
        {
            NX_EXPECT(project.behaviours().size()==(scenario==4?1u:2u));
            std::vector<Screens::PreviewScreen> preview;
            for(const auto& o:output)
            {
                Screens::PreviewScreen p;snprintf(p.info.id,sizeof(p.info.id),"%s",o.id.c_str());p.source=o.source;p.manifest=o.manifest;preview.push_back(p);
            }
            NX_EXPECT(r.screens.startProjectPreview(preview,"level",error,project.assetRoot));
            if(r.screens.previewing())
            {
                r.step(5);NX_EXPECT(!r.screens.failed()&&strcmp(r.services.save->get("actor","missing"),scenario==4?"first":"second")==0);
                r.screens.stopPreview();
            }
        }
        if(scenario==3&&output.size()==2)
        {
            NX_EXPECT(project.function("main","LaunchLevel"));
            // Play an imported main while the real Redux main is still on the host stack.
            std::vector<Screens::PreviewScreen> preview;
            for(const auto& o:output)
            {
                Screens::PreviewScreen p;snprintf(p.info.id,sizeof(p.info.id),"%s",o.id.c_str());p.source=o.source;p.manifest=o.manifest;preview.push_back(p);
            }
            NX_EXPECT(r.screens.startProjectPreview(preview,"main",error,project.assetRoot));
            if(r.screens.previewing())
            {
                char resolved[192];
                NX_EXPECT(resolveAsset(r.services,"global:shared/data.bin",resolved,sizeof(resolved))&&shared==resolved);
                NX_EXPECT(resolveAsset(r.services,"global:freesans.sfn",resolved,sizeof(resolved))&&strcmp(resolved,"global/freesans.sfn")==0);
                r.step(5);NX_EXPECT(r.screens.depth()==3&&strcmp(r.screens.current(),"level")==0);
                NX_EXPECT(strcmp(r.services.save->get("level","missing"),"ok")==0);
                NX_EXPECT(strcmp(r.services.save->get("helper","missing"),"ran")==0);
                r.screens.home();r.step(1);NX_EXPECT(r.screens.depth()==2&&strcmp(r.screens.current(),"main")==0);
                r.screens.stopPreview();
                NX_EXPECT(r.screens.depth()==1&&r.services.save==&r.save&&strcmp(r.screens.current(),"main")==0);
                NX_EXPECT(strcmp(r.save.get("level","missing"),"missing")==0);
                NX_EXPECT(resolveAsset(r.services,"global:shared/data.bin",resolved,sizeof(resolved))&&strcmp(resolved,"global/shared/data.bin")==0);
            }
        }
        studio.close(r.services);
    }
}
#endif

NX_TEST(nativeImportedBehaviourExecutesInOriginalWorld)
{
    BlueprintDocument doc;std::string error,source;
    const char* code=R"(class Counter : Behaviour {
        int count;
        void start(){count=1;Record();}
        void Record(){if(count==1){Save::set(entity.name,"started");}}
        void onHit(Entity@ partner){Save::set(partner.name,entity.name);}
    }
    class Example : Screen {})";
    if(!importBlueprintScreen(code,doc,error)||!compileBlueprint(doc,source,error)){NX_EXPECT(false);return;}
    source+=R"(void setup(){
        Entity@ a=Scene::spawn(Vec2(0,0)); a.name="imported"; a.circle(10,1,1 << 2); a.add(BP_Behaviour_Counter());
        Entity@ b=Scene::spawn(Vec2(0,0)); b.name="partner"; b.circle(10,2,0);
    })";
    Rig r;NX_EXPECT(r.ok);if(!r.ok)return;
    World world(r.assets,r.host,"blueprint",64,32);world.space().init(&r.solids,&r.audio);r.services.world=&world;
    if(!compile(r.host,source.c_str())){NX_EXPECT(false);r.services.world=nullptr;return;}
    NX_EXPECT(r.host.call(r.host.find("blueprint","void setup()")));
    NX_EXPECT(world.update(1.0f/60));NX_EXPECT(world.update(1.0f/60));
    NX_EXPECT(strcmp(r.save.get("imported","missing"),"started")==0);
    NX_EXPECT(strcmp(r.save.get("partner","missing"),"imported")==0);
    r.services.world=nullptr;
}

NX_TEST(nativeImportedDirectAttachmentRunsThroughScreenLifecycle)
{
    BlueprintDocument doc;std::string error,source;
    const char* original=R"(class Actor : Behaviour {void start(){Save::set("actor","started");}}
        class Example : Screen {void onEnter(){Entity@ a=Scene::spawn(Vec2(0.0f,0.0f));a.add(Actor());}})";
    if(!importBlueprintScreen(original,doc,error)||!compileBlueprint(doc,source,error)){NX_ERROR("Attachment import: %s",error.c_str());NX_EXPECT(false);return;}
    for(const auto& code:std::vector<std::string>{original,source})
    {
        Rig r;NX_EXPECT(r.ok);if(!r.ok)return;
        r.platform.files["project/main/screen.json"]="{\"title\":\"Attachment\"}";
        r.platform.files["project/main/main.as"]=code;
        if(!r.screens.init(r.services,"project","main")){NX_EXPECT(false);return;}
        r.step(5);
        NX_EXPECT(!r.screens.failed()&&strcmp(r.save.get("actor","missing"),"started")==0);
    }
}

NX_TEST(nativeImportedInitializersRunAtInstanceConstruction)
{
    BlueprintDocument doc;std::string error,source;
    const char* original=R"(class Actor : Behaviour {int count=5;void start(){count++;if(count==6){Save::set(entity.name,"ok");}}}
        class Example : Screen {int count=7;void onEnter(){if(count==7){Save::set("screen","ok");}count++;Entity@ a=Scene::spawn(Vec2(0.0f,0.0f));a.name="first";a.add(Actor());Entity@ b=Scene::spawn(Vec2(1.0f,0.0f));b.name="second";b.add(Actor());}})";
    if(!importBlueprintScreen(original,doc,error)||!compileBlueprint(doc,source,error)){NX_ERROR("Initializer import: %s",error.c_str());NX_EXPECT(false);return;}
    for(const auto& code:std::vector<std::string>{original,source})
    {
        Rig r;NX_EXPECT(r.ok);if(!r.ok)return;
        r.platform.files["project/main/screen.json"]="{\"title\":\"Initializers\"}";
        r.platform.files["project/main/main.as"]=code;
        if(!r.screens.init(r.services,"project","main")){NX_EXPECT(false);return;}
        r.step(5);
        NX_EXPECT(!r.screens.failed()&&strcmp(r.save.get("screen","missing"),"ok")==0&&strcmp(r.save.get("first","missing"),"ok")==0&&strcmp(r.save.get("second","missing"),"ok")==0);
    }
}

NX_TEST(nativeImportedModuleGlobalsShareStateAcrossBehaviourInstances)
{
    BlueprintDocument doc;std::string error,source;
    const char* original=R"(int count=2; void Bump(){count++;}
        class Actor : Behaviour {void start(){count++;if(count==4){Save::set("first","ok");}if(count==5){Save::set("second","ok");}}}
        class Example : Screen {void onEnter(){Bump();Entity@ a=Scene::spawn(Vec2(0.0f,0.0f));a.add(Actor());Entity@ b=Scene::spawn(Vec2(1.0f,0.0f));b.add(Actor());}})";
    if(!importBlueprintScreen(original,doc,error)||!compileBlueprint(doc,source,error)){NX_ERROR("Global import: %s",error.c_str());NX_EXPECT(false);return;}
    for(const auto& code:std::vector<std::string>{original,source})
    {
        Rig r;NX_EXPECT(r.ok);if(!r.ok)return;
        r.platform.files["project/main/screen.json"]="{\"title\":\"Globals\"}";
        r.platform.files["project/main/main.as"]=code;
        if(!r.screens.init(r.services,"project","main")){NX_EXPECT(false);return;}
        r.step(5);
        NX_EXPECT(!r.screens.failed()&&strcmp(r.save.get("first","missing"),"ok")==0&&strcmp(r.save.get("second","missing"),"ok")==0);
    }
}

NX_TEST(nativeImportedMultiScreenGlobalsRunInSeparateModules)
{
    BlueprintDocument project;project.name="Modules";std::string error;
    for(const auto& owner:std::vector<std::string>{"main","level"})
    {
        const auto code=std::string("int count=")+(owner=="main"?"1":"9")+"; class Actor : Behaviour {void start(){count++;if(count==3){Save::set(\"main_count\",\"ok\");}if(count==11){Save::set(\"level_count\",\"ok\");}}} class Example : Screen {void onEnter(){count++;Entity@ a=Scene::spawn(Vec2(0.0f,0.0f));a.add(Actor());}}";
        BlueprintDocument doc;if(!importBlueprintScreen(code,doc,error)||!mergeImportedBlueprintScreen(project,std::move(doc),owner,error)){NX_ERROR("Module import: %s",error.c_str());NX_EXPECT(false);return;}
    }
    std::vector<BlueprintScreenOutput> output;if(!compileBlueprintProject(project,output,error)){NX_EXPECT(false);return;}
    Rig r;NX_EXPECT(r.ok);if(!r.ok)return;
    for(const auto& o:output){r.platform.files["project/"+o.id+"/screen.json"]=o.manifest;r.platform.files["project/"+o.id+"/main.as"]=o.source;}
    if(!r.screens.init(r.services,"project","main")){NX_EXPECT(false);return;}
    r.step(5);NX_EXPECT(strcmp(r.save.get("main_count","missing"),"ok")==0&&strcmp(r.save.get("level_count","missing"),"missing")==0);
    r.screens.push("level");r.step(5);NX_EXPECT(!r.screens.failed()&&strcmp(r.save.get("level_count","missing"),"ok")==0);
}

#ifdef NX_DESKTOP_BLUEPRINT
NX_TEST(nativeWorkspaceGraphicalVariableEditorCreatesAndRemovesDeclarations)
{
    Rig r;NX_EXPECT(r.ok);if(!r.ok)return;
    r.platform.workspaceFiles=true;
    r.platform.files["showcase/main/screen.json"]="{\"title\":\"Home\"}";r.platform.files["showcase/main/main.as"]="class Home : Screen {}";
    if(!r.screens.init(r.services,"showcase","main")){NX_EXPECT(false);return;}
    r.step(3);r.screens.setDevelopmentPaused(true);
    BlueprintWorkspace studio;studio.open(r.services);if(!studio.active()){NX_EXPECT(false);return;}
    auto& state=r.platform.state;state.hasMouse=true;
    auto frame=[&](){r.assets.finish();r.input.update(state,1.0f/60);r.platform.now+=1.0/60;studio.update(r.services);studio.checkpoint(r.services);r.device->beginFrame({});r.canvas.begin();studio.draw(r.canvas);r.canvas.end();r.device->endFrame();};
    auto click=[&](float x,float y){state.mouseX=x;state.mouseY=y;state.mouseButtons[0]=true;frame();state.mouseButtons[0]=false;frame();};
    auto type=[&](const char* s){for(;*s;++s){state.typed[state.typedCount%InputState::kTyped]=static_cast<u32>(*s);++state.typedCount;}frame();};
    frame();click(120,464);click(260,170);type("score");click(800,339);type("12");click(750,481);click(1040,132);studio.checkpoint(r.services,true);
    const std::string recovery="own/blueprint-recovery.bp.json";std::string error;
    BlueprintDocument doc;auto bytes=r.platform.files[recovery];
    NX_EXPECT(doc.load(bytes.data(),bytes.size(),error)&&doc.graphs[0].fields.size()==1);
    if(!doc.graphs.empty()&&!doc.graphs[0].fields.empty())NX_EXPECT(doc.graphs[0].fields[0].name=="score"&&doc.graphs[0].fields[0].value=="12");
    // Drag the declared field from My Blueprint, choose Get, then undo just that placement.
    state.mouseX=120;state.mouseY=365;state.mouseButtons[0]=true;frame();state.mouseX=500;state.mouseY=280;frame();state.mouseButtons[0]=false;frame();
    click(560,320);studio.checkpoint(r.services,true);
    bytes=r.platform.files[recovery];NX_EXPECT(doc.load(bytes.data(),bytes.size(),error)&&doc.graphs[0].nodes.size()==2);
    if(!doc.graphs.empty()&&doc.graphs[0].nodes.size()==2)NX_EXPECT(doc.graphs[0].nodes[1].kind=="var:get:int"&&doc.graphs[0].nodes[1].ref=="score");
    click(900,53);
    click(120,464);click(300,205);click(980,481);click(1040,132);studio.checkpoint(r.services,true);
    bytes=r.platform.files[recovery];NX_EXPECT(doc.load(bytes.data(),bytes.size(),error)&&doc.graphs[0].fields.empty());
    studio.close(r.services);
}
#endif

#ifdef NX_DESKTOP_BLUEPRINT
NX_TEST(nativeWorkspaceRightClickSearchPlacesNodeAndUndoRemovesIt)
{
    Rig r;NX_EXPECT(r.ok);if(!r.ok)return;
    r.platform.workspaceFiles=true;r.platform.files["showcase/main/screen.json"]="{\"title\":\"Home\"}";r.platform.files["showcase/main/main.as"]="class Home : Screen {}";
    if(!r.screens.init(r.services,"showcase","main")){NX_EXPECT(false);return;}
    r.step(3);r.screens.setDevelopmentPaused(true);BlueprintWorkspace studio;studio.open(r.services);if(!studio.active()){NX_EXPECT(false);return;}
    auto& state=r.platform.state;state.hasMouse=true;
    auto frame=[&](){r.assets.finish();r.input.update(state,1.0f/60);r.platform.now+=1.0/60;studio.update(r.services);studio.checkpoint(r.services);r.device->beginFrame({});r.canvas.begin();studio.draw(r.canvas);r.canvas.end();r.device->endFrame();};
    auto click=[&](float x,float y){state.mouseX=x;state.mouseY=y;state.mouseButtons[0]=true;frame();state.mouseButtons[0]=false;frame();};
    frame();state.mouseX=500;state.mouseY=200;state.mouseButtons[1]=true;frame();state.mouseButtons[1]=false;frame();
    for(char ch:std::string("branch")){state.typed[state.typedCount%InputState::kTyped]=static_cast<u32>(ch);++state.typedCount;}frame();
    click(550,275);studio.checkpoint(r.services,true);
    BlueprintDocument doc;std::string error;const std::string recovery="own/blueprint-recovery.bp.json";auto bytes=r.platform.files[recovery];
    NX_EXPECT(doc.load(bytes.data(),bytes.size(),error)&&doc.graphs[0].nodes.size()==2);
    if(!doc.graphs.empty()&&doc.graphs[0].nodes.size()==2)NX_EXPECT(doc.graphs[0].nodes[1].kind=="branch");
    // Undo to the original clean document deliberately clears the recovery draft.
    click(900,53);studio.checkpoint(r.services,true);NX_EXPECT(r.platform.files[recovery].empty());
    click(990,53);studio.checkpoint(r.services,true);bytes=r.platform.files[recovery];NX_EXPECT(doc.load(bytes.data(),bytes.size(),error)&&doc.graphs[0].nodes.size()==2);
    click(900,53);studio.checkpoint(r.services,true);NX_EXPECT(r.platform.files[recovery].empty());
    // Fit leaves the entry node at (272,100), with its execution output at (468,138).
    auto drag=[&](float x1,float y1,float x2,float y2){state.mouseX=x1;state.mouseY=y1;state.mouseButtons[0]=true;frame();state.mouseX=x2;state.mouseY=y2;frame();state.mouseButtons[0]=false;frame();};
    auto type=[&](const char* value){for(;*value;++value){state.typed[state.typedCount%InputState::kTyped]=static_cast<u32>(*value);++state.typedCount;}frame();};
    drag(468,138,600,200);type("branch");type("\r");studio.checkpoint(r.services,true);
    bytes=r.platform.files[recovery];NX_EXPECT(doc.load(bytes.data(),bytes.size(),error)&&doc.graphs[0].nodes.size()==2&&doc.graphs[0].links.size()==1);
    if(!doc.graphs.empty()&&doc.graphs[0].nodes.size()==2)NX_EXPECT(doc.graphs[0].nodes[1].kind=="branch");
    // The newly placed branch begins at (600,200); its condition is the second input.
    drag(600,260,420,380);type("bool");type("\r");studio.checkpoint(r.services,true);
    bytes=r.platform.files[recovery];NX_EXPECT(doc.load(bytes.data(),bytes.size(),error)&&doc.graphs[0].nodes.size()==3&&doc.graphs[0].links.size()==2);
    click(900,53);studio.checkpoint(r.services,true);bytes=r.platform.files[recovery];NX_EXPECT(doc.load(bytes.data(),bytes.size(),error)&&doc.graphs[0].nodes.size()==2&&doc.graphs[0].links.size()==1);
    state.keys[static_cast<size_t>(Key::Control)]=true;state.mouseX=534;state.mouseY=180;state.mouseButtons[1]=true;frame();state.mouseButtons[1]=false;frame();state.keys[static_cast<size_t>(Key::Control)]=false;frame();
    studio.checkpoint(r.services,true);bytes=r.platform.files[recovery];NX_EXPECT(doc.load(bytes.data(),bytes.size(),error)&&doc.graphs[0].links.size()==1);
    if(doc.graphs[0].links.size()==1)NX_EXPECT(doc.graphs[0].links[0].route.size()==1);
    click(320,115);state.keys[static_cast<size_t>(Key::Shift)]=true;click(650,215);state.keys[static_cast<size_t>(Key::Shift)]=false;frame();
    click(352,18);click(410,140);type("\r");studio.checkpoint(r.services,true);bytes=r.platform.files[recovery];
    NX_EXPECT(doc.load(bytes.data(),bytes.size(),error)&&doc.graphs[0].comments.size()==1);
    if(doc.graphs[0].comments.size()==1)NX_EXPECT(doc.graphs[0].comments[0].members.size()==2);
    click(352,18);click(410,50);studio.checkpoint(r.services,true);bytes=r.platform.files[recovery];
    NX_EXPECT(doc.load(bytes.data(),bytes.size(),error)&&doc.graphs[0].links.size()==1&&doc.graphs[0].links[0].route.empty());
    click(900,53);studio.checkpoint(r.services,true);bytes=r.platform.files[recovery];
    NX_EXPECT(doc.load(bytes.data(),bytes.size(),error)&&doc.graphs[0].links.size()==1&&doc.graphs[0].links[0].route.size()==1);
    studio.close(r.services);
}
#endif

NX_TEST(nativeCollapsedFunctionExecutesLikeOriginalSource)
{
    const std::string original=R"(class Example : Screen {int score=4;void onEnter(){score++;score=score+2;if(score==7){Save::set("collapse","ok");}}})";
    BlueprintDocument doc;std::string error,uncollapsed,collapsed;
    if(!importBlueprintScreen(original,doc,error)||!compileBlueprint(doc,uncollapsed,error)){NX_ERROR("Collapse source: %s",error.c_str());NX_EXPECT(false);return;}
    // Select the first two assignments and their data dependencies, excluding the branch.
    const auto& g=doc.graphs[0];std::vector<size_t> selected;std::vector<bool> seen(g.nodes.size(),false);
    for(size_t i=0;i<g.nodes.size();++i)if(g.nodes[i].kind=="var:set:int"){selected.push_back(i);seen[i]=true;}
    for(size_t i=0;i<selected.size();++i)for(const auto& l:g.links)if(l.to==selected[i]&&l.input!="in"&&!seen[l.from]){seen[l.from]=true;selected.push_back(l.from);}
    if(!doc.collapseToFunction(0,selected,"ApplyScore",error)||!compileBlueprint(doc,collapsed,error)){NX_ERROR("Collapse function: %s",error.c_str());NX_EXPECT(false);return;}
    for(const auto& code:std::vector<std::string>{original,uncollapsed,collapsed})
    {
        Rig r;NX_EXPECT(r.ok);if(!r.ok)return;
        r.platform.files["project/main/screen.json"]="{\"title\":\"Collapse\"}";r.platform.files["project/main/main.as"]=code;
        if(!r.screens.init(r.services,"project","main")){NX_EXPECT(false);return;}
        r.step(5);NX_EXPECT(!r.screens.failed()&&strcmp(r.save.get("collapse","missing"),"ok")==0);
    }
}

NX_TEST(nativeInlineMacrosPreserveRuntimeAcrossRepeatedCalls)
{
    const std::string original=R"(class Example : Screen {int score=1;void Add(int amount){score=score+amount;}void onEnter(){Add(2);Add(4);if(score==7){Save::set("macro","ok");}}})";
    BlueprintDocument doc;std::string error,source;
    if(!importBlueprintScreen(original,doc,error)){NX_ERROR("Macro import: %s",error.c_str());NX_EXPECT(false);return;}
    const auto* fn=doc.function("Screen","Add");NX_EXPECT(fn);if(!fn)return;
    if(!doc.setInlineMacro(static_cast<size_t>(fn-doc.graphs.data()),true,error)||!compileBlueprint(doc,source,error)){NX_ERROR("Macro compile: %s",error.c_str());NX_EXPECT(false);return;}
    for(const auto& code:std::vector<std::string>{original,source})
    {
        Rig r;NX_EXPECT(r.ok);if(!r.ok)return;
        r.platform.files["project/main/screen.json"]="{\"title\":\"Macro\"}";r.platform.files["project/main/main.as"]=code;
        if(!r.screens.init(r.services,"project","main")){NX_EXPECT(false);return;}
        r.step(5);NX_EXPECT(!r.screens.failed()&&strcmp(r.save.get("macro","missing"),"ok")==0);
    }
}

NX_TEST(nativeImportedArraysExecuteLikeOriginalScript)
{
    const std::string original=R"(array<int> shared;class Example : Screen {array<int> scores;void onEnter(){scores.insertLast(3);scores.resize(2);scores[1]=7;shared.insertLast(scores[1]);if(shared.length()==uint(1)&&shared[0]==7){Save::set("arrays","ok");}shared.removeAt(0);if(shared.length()==uint(0)){Save::set("empty","ok");}}})";
    BlueprintDocument doc;std::string error,source;
    if(!importBlueprintScreen(original,doc,error)||!compileBlueprint(doc,source,error)){NX_ERROR("Array import: %s",error.c_str());NX_EXPECT(false);return;}
    for(const auto& code:std::vector<std::string>{original,source})
    {
        Rig r;NX_EXPECT(r.ok);if(!r.ok)return;
        r.platform.files["project/main/screen.json"]="{\"title\":\"Arrays\"}";r.platform.files["project/main/main.as"]=code;
        if(!r.screens.init(r.services,"project","main")){NX_EXPECT(false);return;}
        r.step(5);NX_EXPECT(!r.screens.failed()&&strcmp(r.save.get("arrays","missing"),"ok")==0&&strcmp(r.save.get("empty","missing"),"ok")==0);
    }
}

#include "blueprint_orbit_fixture.h"
NX_TEST(nativeBlueprintCameraOrbitKeepsCubeCenteredAndReturnsToStart)
{
    for(const int variant:{0,1,2})
    {
    BlueprintDocument doc;std::string error,source,manifest;
    if(!blueprint_orbit::author(doc,error,variant)||!compileBlueprintScreen(doc,source,manifest,error)){NX_ERROR("Orbit compile: %s",error.c_str());NX_EXPECT(false);return;}
    Rig r;NX_EXPECT(r.ok);if(!r.ok)return;
    r.platform.files["project/main/screen.json"]=manifest;r.platform.files["project/main/main.as"]=source;
    if(!r.screens.init(r.services,"project","main")){NX_EXPECT(false);return;}
    // Screens::draw deliberately skips the loading frame until the first update.
    r.step(2);if(r.screens.failed()||!r.services.world){NX_EXPECT(false);return;}
    auto& world=*r.services.world;auto& space=world.space();
    const auto cube=world.findNamed("Cube"),floor=world.findNamed("Floor");NX_EXPECT(cube&&floor);if(!cube||!floor)return;
    const auto* box=space.getAppearance(cube);const auto* plane=space.getAppearance(floor);
    NX_EXPECT(box&&box->solid==Solid::Box&&plane&&plane->solid==Solid::Plane);
    const auto* cubeSpatial=space.getSpatial(cube);const auto* floorSpatial=space.getSpatial(floor);
    NX_EXPECT(cubeSpatial&&fabsf(cubeSpatial->place.y-0.5f)<0.001f&&floorSpatial&&fabsf(floorSpatial->place.y)<0.001f&&fabsf(floorSpatial->size.x-20)<0.001f);
    auto check=[&](float x,float z,float tolerance)
    {
        const Vec3 eye=space.cameraPlace();Vec2 projected{};
        const bool visible=space.toScreen({0,0.5f,0},projected);
        NX_INFO("Orbit variant %d: expected (%.3f, %.3f), actual (%.3f, %.3f, %.3f), projected (%.2f, %.2f), drawn %u",variant,x,z,eye.x,eye.y,eye.z,projected.x,projected.y,static_cast<unsigned>(space.stats().drawn));
        NX_EXPECT(fabsf(eye.x-x)<tolerance&&fabsf(eye.y-2.5f)<0.001f&&fabsf(eye.z-z)<tolerance);
        NX_EXPECT(fabsf(sqrtf(eye.x*eye.x+eye.z*eye.z)-6)<0.001f);
        NX_EXPECT(visible&&fabsf(projected.x-640)<1&&fabsf(projected.y-360)<1);
        NX_EXPECT(space.stats().drawn>=2&&r.device->stats().rejected==0);
    };
    check(0,6,0.20f);r.step(120);check(6,0,0.20f);r.step(120);check(0,-6,0.20f);r.step(120);check(-6,0,0.20f);r.step(122);check(0,6,0.001f);
    const auto stopped=space.cameraPlace();r.step(120);const auto later=space.cameraPlace();
    NX_EXPECT(fabsf(stopped.x-later.x)<0.00001f&&fabsf(stopped.z-later.z)<0.00001f&&!r.screens.failed());
    if(variant==2)
    {
        // The completed action must still follow its live target and tolerate deletion.
        auto* spatial=space.getSpatial(cube);NX_EXPECT(spatial!=nullptr);if(!spatial)return;
        spatial->place={2,1.5f,-3};r.step(2);
        const Vec3 moved=space.cameraPlace();Vec2 projected{};
        NX_EXPECT(fabsf(moved.x-2)<0.001f&&fabsf(moved.y-3.5f)<0.001f&&fabsf(moved.z-3)<0.001f);
        NX_EXPECT(space.toScreen({2,1.5f,-3},projected)&&fabsf(projected.x-640)<1&&fabsf(projected.y-360)<1);
        world.destroyEntity(cube);r.step(3);const Vec3 after=space.cameraPlace();
        NX_EXPECT(!r.screens.failed()&&fabsf(after.x-moved.x)<0.001f&&fabsf(after.y-moved.y)<0.001f&&fabsf(after.z-moved.z)<0.001f);
    }
    NX_INFO("Blueprint orbit variant %d: completed position, projection and endpoint assertions.",variant);
    }
}

NX_TEST(nativeLinkedWidgetPageRunsWithoutSnapshotFile)
{
    Rig r;NX_EXPECT(r.ok);if(!r.ok)return;
    BlueprintDocument doc;doc.create();std::string error,id,source;
    const bool authored=doc.createVisualAsset("widget",id,error)&&
        doc.appendVisualWidget(id,"label","Health",error)&&
        doc.appendVisualWidget(id,"progress","",error)&&
        doc.connectVisualWidget("Screen",id,error)&&compileBlueprint(doc,source,error);
    NX_EXPECT(authored);if(!authored)return;
    r.platform.files["showcase/main/screen.json"]="{\"title\":\"Linked HUD\"}";
    r.platform.files["showcase/main/main.as"]=source;
    if(!r.screens.init(r.services,"showcase","main")){NX_EXPECT(false);return;}
    r.step(3);
    NX_EXPECT(!r.screens.failed());
    if(!r.services.ui){NX_EXPECT(false);return;}
    const auto* label=r.services.ui->find("item-1");
    const auto* progress=r.services.ui->find("item-2");
    NX_EXPECT(label&&label->text=="Health");
    NX_EXPECT(progress&&progress->value==0.75f);
}

NX_TEST(nativeUpstreamBlueprintFactoriesAndAnimationArrayPinsCompile)
{
    Rig r;NX_EXPECT(r.ok);if(!r.ok)return;
    BlueprintDocument doc;doc.create();std::string error,source;
    // Compile, but do not execute a real network connection.
    const bool authored=doc.editVariableDeclaration("Screen","field music:Playlist@",error)&&
        doc.editVariableDeclaration("Screen","global socket:WebSocket@",error)&&
        doc.editVariableDeclaration("Screen","field names:array<string>",error)&&
        doc.editVariableDeclaration("Screen","field points:array<float>",error)&&
        doc.addNode(0,"create:playlist",0,0,error)&&doc.addVariableNode(0,"music",false,true,0,0,error)&&
        doc.connect(0,0,"then",1,"in",error)&&doc.connect(0,1,"then",2,"in",error)&&doc.connect(0,1,"result",2,"value",error)&&
        doc.addNode(0,"create:websocket",0,0,error)&&doc.setInputValue(0,3,"url","ws://127.0.0.1:9",error)&&
        doc.addVariableNode(0,"socket",true,true,0,0,error)&&doc.connect(0,2,"then",3,"in",error)&&doc.connect(0,3,"then",4,"in",error)&&doc.connect(0,3,"result",4,"value",error)&&
        doc.addNode(1,"spawn:box",0,0,error)&&doc.addVariableNode(1,"names",false,false,0,0,error)&&
        doc.addVariableNode(1,"points",false,false,0,0,error)&&
        doc.addNode(1,"api:Entity:animateBlend:Entity@,array<string>@,array<float>@,float",0,0,error)&&
        doc.connect(1,0,"then",1,"in",error)&&doc.connect(1,1,"then",4,"in",error)&&doc.connect(1,1,"result",4,"target",error)&&
        doc.connectCompatible(1,2,"value",4,"names",error)&&doc.connectCompatible(1,3,"value",4,"at",error);
    NX_EXPECT(authored&&compileBlueprint(doc,source,error));
    if(!source.empty())NX_EXPECT(compile(r.host,source.c_str()));
}

NX_TEST(nativeImportedUpstreamPlaylistGraphCompilesAndRuns)
{
    Rig r;NX_EXPECT(r.ok);if(!r.ok)return;
    const char* script=R"AS(
class Example : Screen {
    Playlist@ music;
    void onEnter() {
        @music = Playlist();
        music.shuffle = true;
        if (music.shuffle) Save::set("playlist_ok","yes");
        music.stop();
        @music = null;
    }
}
)AS";
    BlueprintDocument doc;std::string error,source;
    const bool ok=importBlueprintScreen(script,doc,error)&&compileBlueprint(doc,source,error);
    NX_EXPECT(ok);if(!ok)return;
    r.platform.files["showcase/main/screen.json"]="{\"title\":\"Playlist graph\"}";
    r.platform.files["showcase/main/main.as"]=source;
    if(!r.screens.init(r.services,"showcase","main")){NX_EXPECT(false);return;}
    r.step(3);NX_EXPECT(!r.screens.failed());
    NX_EXPECT(strcmp(r.save.get("playlist_ok","missing"),"yes")==0);
}

NX_TEST(nativeLinkedActorsInstantiateIndependentEntities)
{
    Rig r;NX_EXPECT(r.ok);if(!r.ok)return;
    BlueprintDocument doc;doc.create();std::string error,id,source;
    const bool ok=doc.createVisualAsset("actor",id,error)&&doc.setVisualActorShape(id,"sphere",error)&&
        doc.placeVisualActor("Screen",id,"1,2,3",error)&&doc.placeVisualActor("Screen",id,"4,5,6",error)&&
        compileBlueprint(doc,source,error);
    NX_EXPECT(ok);if(!ok)return;
    r.platform.files["showcase/main/screen.json"]="{\"title\":\"Reusable actor\"}";
    r.platform.files["showcase/main/main.as"]=source;
    if(!r.screens.init(r.services,"showcase","main")){NX_EXPECT(false);return;}
    r.step(3);NX_EXPECT(!r.screens.failed());
    NX_EXPECT(r.services.world!=nullptr);if(!r.services.world)return;
    unsigned first=0,second=0;
    auto& world=*r.services.world;
    world.eachEntity([&](u32 id,const World::Entity& e)
    {
        if(strcmp(e.name,"Root")!=0)return;
        const auto* spatial=world.space().getSpatial(id);if(!spatial)return;
        if(fabsf(spatial->place.x-1)<0.01f&&fabsf(spatial->place.y-2)<0.01f&&fabsf(spatial->place.z-3)<0.01f)++first;
        if(fabsf(spatial->place.x-4)<0.01f&&fabsf(spatial->place.y-5)<0.01f&&fabsf(spatial->place.z-6)<0.01f)++second;
    });
    NX_EXPECT(first==1&&second==1);
}

NX_TEST(nativeActorHierarchySpawnsAndDestroysChildren)
{
    Rig r;NX_EXPECT(r.ok);if(!r.ok)return;
    BlueprintDocument doc;doc.create();std::string error,id,source;
    const bool ok=doc.createVisualAsset("actor",id,error)&&
        doc.appendVisualActorComponent(id,0,"box","0,1.5,0",error)&&
        doc.appendVisualActorComponent(id,1,"sphere","1,0,0",error)&&
        doc.placeVisualActor("Screen",id,"2,0,0",error)&&compileBlueprint(doc,source,error);
    NX_EXPECT(ok);if(!ok)return;
    r.platform.files["showcase/main/screen.json"]="{\"title\":\"Actor hierarchy\"}";
    r.platform.files["showcase/main/main.as"]=source;
    if(!r.screens.init(r.services,"showcase","main")){NX_EXPECT(false);return;}
    r.step(3);NX_EXPECT(!r.screens.failed());
    if(!r.services.world){NX_EXPECT(false);return;}
    auto& world=*r.services.world;
    const u32 root=world.findNamed("Root"),child=world.findNamed("Component1"),grandchild=world.findNamed("Component2");
    NX_EXPECT(root&&child&&grandchild&&world.parentOf(child)==root&&world.parentOf(grandchild)==child);
    const auto* spatial=world.space().getSpatial(child);
    NX_EXPECT(spatial&&fabsf(spatial->place.y-1.5f)<0.01f);
    world.destroyEntity(root);r.step(3);
    NX_EXPECT(!world.findNamed("Root")&&!world.findNamed("Component1")&&!world.findNamed("Component2"));
}

NX_TEST(nativeActorComponentsReceiveIndependentBlueprintBehaviours)
{
    Rig r;NX_EXPECT(r.ok);if(!r.ok)return;
    BlueprintDocument doc;doc.create();std::string error,id,source;
    const bool ok=doc.createVisualAsset("actor",id,error)&&doc.addBehaviour("Movement",error)&&
        doc.appendVisualActorComponent(id,0,"sphere","0,1,0",error)&&
        doc.setVisualActorProperty(id,1,"behaviour","Movement",error)&&
        doc.setVisualActorProperty(id,1,"place","0,2,0",error)&&
        doc.placeVisualActor("Screen",id,"0,0,0",error)&&doc.placeVisualActor("Screen",id,"5,0,0",error)&&
        compileBlueprint(doc,source,error);
    NX_EXPECT(ok);if(!ok)return;
    r.platform.files["showcase/main/screen.json"]="{\"title\":\"Actor behaviours\"}";
    r.platform.files["showcase/main/main.as"]=source;
    if(!r.screens.init(r.services,"showcase","main")){NX_EXPECT(false);return;}
    r.step(3);NX_EXPECT(!r.screens.failed());
    if(!r.services.world){NX_EXPECT(false);return;}
    auto& world=*r.services.world;
    std::vector<asIScriptObject*> attached;
    world.eachEntity([&](u32 id,const World::Entity& entity)
    {
        if(strcmp(entity.name,"Component1")!=0)return;
        if(auto* behaviour=world.behaviour(id))attached.push_back(behaviour);
        const auto* spatial=world.space().getSpatial(id);
        NX_EXPECT(spatial&&fabsf(spatial->place.y-2)<0.01f);
    });
    NX_EXPECT(attached.size()==2);
    if(attached.size()==2)NX_EXPECT(attached[0]!=attached[1]);
}

NX_TEST(nativeActorPhysicsAndInstanceOverridesRun)
{
    Rig r;NX_EXPECT(r.ok);if(!r.ok)return;
    BlueprintDocument doc;doc.create();std::string error,id,source;
    const bool ok=doc.createVisualAsset("actor",id,error)&&
        doc.setVisualActorProperty(id,0,"collision","box:1,1,1",error)&&
        doc.setVisualActorProperty(id,0,"mass","2",error)&&
        doc.setVisualActorProperty(id,0,"gravity","0",error)&&
        doc.placeVisualActor("Screen",id,"0,0,0",error)&&doc.placeVisualActor("Screen",id,"5,0,0",error)&&
        doc.setInputValue(0,1,"instanceName","First",error)&&doc.setInputValue(0,1,"sizeOverride","2,2,2",error)&&
        doc.setVisualActorProperty(id,0,"size","3,3,3",error)&&compileBlueprint(doc,source,error);
    NX_EXPECT(ok);if(!ok)return;
    r.platform.files["showcase/main/screen.json"]="{\"title\":\"Actor overrides\"}";
    r.platform.files["showcase/main/main.as"]=source;
    if(!r.screens.init(r.services,"showcase","main")){NX_EXPECT(false);return;}
    r.step(3);NX_EXPECT(!r.screens.failed());
    if(!r.services.world){NX_EXPECT(false);return;}
    auto& world=*r.services.world;
    const auto first=world.findNamed("First"),second=world.findNamed("Root");
    NX_EXPECT(first&&second&&first!=second);
    const auto* one=world.space().getSpatial(first);const auto* two=world.space().getSpatial(second);
    NX_EXPECT(one&&two&&fabsf(one->size.x-2)<0.01f&&fabsf(two->size.x-3)<0.01f);
    const auto* body=world.space().getBody(first);const auto* collider=world.space().getCollider(first);
    NX_EXPECT(body&&fabsf(body->mass-2)<0.01f&&body->gravity==0);
    NX_EXPECT(collider&&collider->shape==Shape3::Box);
}

NX_TEST(nativeNestedWidgetDesignerCompilesIntoLiveRuntime)
{
    Rig r;NX_EXPECT(r.ok);if(!r.ok)return;
    BlueprintDocument doc;doc.create();std::string id,error,source;
    const bool ok=doc.createVisualAsset("widget",id,error)&&
        doc.appendVisualWidget(id,"column","",error)&&doc.appendVisualWidget(id,"label","Health",error,"item-1")&&
        doc.appendVisualWidget(id,"progress","",error,"item-1")&&doc.setVisualWidgetProperty(id,"item-3","width","320",error)&&
        doc.connectVisualWidget("Screen",id,error)&&compileBlueprint(doc,source,error);
    NX_EXPECT(ok);if(!ok)return;
    r.platform.files["showcase/main/screen.json"]="{\"title\":\"Nested UI\"}";
    r.platform.files["showcase/main/main.as"]=source;
    if(!r.screens.init(r.services,"showcase","main")){NX_EXPECT(false);return;}
    r.step(3);NX_EXPECT(!r.screens.failed());
    if(!r.services.ui){NX_EXPECT(false);return;}
    auto* container=r.services.ui->find("item-1");auto* label=r.services.ui->find("item-2");auto* progress=r.services.ui->find("item-3");
    NX_EXPECT(container&&label&&progress&&label->parent==container&&progress->parent==container&&progress->width==320);
    const char page[]="[{\"button\":\"Do not run\",\"click\":\"MissingHandler\",\"id\":\"design-only\"}]";
    Ui& view=*r.services.ui;
    NX_EXPECT(!view.loadPage(page,sizeof(page)-1,view.root(),nullptr));
    NX_EXPECT(!view.find("design-only"));
    NX_EXPECT(view.loadPage(page,sizeof(page)-1,view.root(),nullptr,"design test",true));
    const auto* design=view.find("design-only");
    NX_EXPECT(design&&design->events[static_cast<size_t>(UiEvent::Click)]==nullptr);
}

NX_TEST(nativeWidgetButtonEventGraphExecutesThroughReduxUi)
{
    Rig r;NX_EXPECT(r.ok);if(!r.ok)return;
    BlueprintDocument doc;doc.create();std::string id,error,source;size_t callback=0;
    const bool ok=doc.createVisualAsset("widget",id,error)&&doc.appendVisualWidget(id,"button","Play",error)&&
        doc.connectVisualWidget("Screen",id,error)&&doc.openVisualWidgetEvent("Screen",id,"item-1","click",callback,error)&&
        doc.addConnectedNode(callback,0,"then","api:Save:Save::set:string,string",260,60,error)&&
        doc.setInputValue(callback,1,"key","widget_clicked",error)&&doc.setInputValue(callback,1,"value","yes",error)&&compileBlueprint(doc,source,error);
    NX_EXPECT(ok);if(!ok)return;
    r.platform.files["showcase/main/screen.json"]="{\"title\":\"Widget event graph\"}";
    r.platform.files["showcase/main/main.as"]=source;
    if(!r.screens.init(r.services,"showcase","main")){NX_EXPECT(false);return;}
    r.step(3);NX_EXPECT(!r.screens.failed());
    if(!r.services.ui){NX_EXPECT(false);return;}
    auto* button=r.services.ui->find("item-1");
    NX_EXPECT(button);if(!button)return;
    r.services.ui->focus(button);
    r.press(Action::Accept);
    r.step(3);NX_EXPECT(!r.screens.failed());
    NX_EXPECT(strcmp(r.save.get("widget_clicked","missing"),"yes")==0);
}

NX_TEST(nativeStackAnchorsAdaptToAspectRatio)
{
    Rig r;NX_EXPECT(r.ok);if(!r.ok)return;
    BlueprintDocument doc;doc.create();std::string id,error,source;
    const bool ok=doc.createVisualAsset("widget",id,error)&&doc.appendVisualWidget(id,"stack","",error)&&
        doc.setVisualWidgetProperty(id,"item-1","grow","1",error)&&doc.appendVisualWidget(id,"label","HUD",error,"item-1")&&
        doc.setVisualWidgetProperty(id,"item-2","width","100",error)&&doc.setVisualWidgetProperty(id,"item-2","height","40",error)&&
        doc.setVisualWidgetProperty(id,"item-2","anchor","bottom-right",error)&&doc.setVisualWidgetProperty(id,"item-2","offset","-24,-24",error)&&
        doc.connectVisualWidget("Screen",id,error)&&compileBlueprint(doc,source,error);
    NX_EXPECT(ok);if(!ok)return;
    r.platform.files["showcase/main/screen.json"]="{\"title\":\"Anchored HUD\"}";
    r.platform.files["showcase/main/main.as"]=source;
    if(!r.screens.init(r.services,"showcase","main")){NX_EXPECT(false);return;}
    r.step(3);NX_EXPECT(!r.screens.failed());if(!r.services.ui)return;
    auto& view=*r.services.ui;auto* stack=view.find("item-1");auto* label=view.find("item-2");
    NX_EXPECT(stack&&label);if(!stack||!label)return;
    view.drawDesign(r.canvas,0,0,1280,720);
    const float wideX=label->x;
    NX_EXPECT(fabsf(label->x-(stack->w-stack->pad(2)-label->w-24))<0.01f);
    NX_EXPECT(fabsf(label->y-(stack->h-stack->pad(3)-label->h-24))<0.01f);
    view.drawDesign(r.canvas,0,0,960,720);
    NX_EXPECT(fabsf(wideX-label->x-320)<0.01f);
}

NX_TEST(nativeLinkedThemeStylesReachRuntimeWidgets)
{
    Rig r;NX_EXPECT(r.ok);if(!r.ok)return;
    BlueprintDocument doc;doc.create();std::string page,theme,error,source;
    const bool ok=doc.createVisualAsset("widget",page,error)&&doc.appendVisualWidget(page,"button","Play",error)&&
        doc.createVisualAsset("theme",theme,error)&&doc.setVisualThemeProperty(theme,"button","normal","back","#123456",error)&&
        doc.setVisualThemeProperty(theme,"button","focused","radius","7",error)&&
        doc.setVisualThemeProperty(theme,"button","hovered","back","#FF0000",error)&&
        doc.setVisualThemeProperty(theme,"button","normal","minWidth","160",error)&&
        doc.setVisualThemeProperty(theme,"button","normal","minHeight","60",error)&&
        doc.setVisualThemeProperty(theme,"button","normal","align","right",error)&&doc.setVisualTheme(page,theme,error)&&
        doc.connectVisualWidget("Screen",page,error)&&compileBlueprint(doc,source,error);
    NX_EXPECT(ok);if(!ok)return;
    r.platform.files["showcase/main/screen.json"]="{\"title\":\"Themed UI\"}";
    r.platform.files["showcase/main/main.as"]=source;
    if(!r.screens.init(r.services,"showcase","main")){NX_EXPECT(false);return;}
    r.step(3);NX_EXPECT(!r.screens.failed());if(!r.services.ui)return;
    const auto* widget=r.services.ui->find("item-1");
    NX_EXPECT(widget&&widget->style);
    if(widget&&widget->style)
    {
        NX_EXPECT(fabsf(widget->style->looks[0].back.r-18.f/255.f)<0.001f);
        NX_EXPECT(widget->style->looks[static_cast<size_t>(UiState::Focused)].radius==7);
        NX_EXPECT(widget->style->minWidth==160&&widget->style->minHeight==60);
        NX_EXPECT(widget->style->align==Align::Right);
        r.device->beginFrame({});r.canvas.begin();
        r.services.ui->drawDesign(r.canvas,0,0,1280,720,"item-1",static_cast<int>(UiState::Hovered));
        r.canvas.end();r.device->endFrame();
        NX_EXPECT(widget->look.back.r==1.f&&widget->look.back.g==0.f);
    }
}

NX_TEST(nativeStretchAnchorsFillResponsiveStack)
{
    Rig r;NX_EXPECT(r.ok);if(!r.ok)return;
    BlueprintDocument doc;doc.create();std::string id,error,source;
    const bool ok=doc.createVisualAsset("widget",id,error)&&doc.appendVisualWidget(id,"stack","",error)&&
        doc.setVisualWidgetProperty(id,"item-1","grow","1",error)&&doc.appendVisualWidget(id,"label","HUD",error,"item-1")&&
        doc.setVisualWidgetProperty(id,"item-2","anchor","stretch-both",error)&&doc.setVisualWidgetProperty(id,"item-2","offset","20,30",error)&&
        doc.connectVisualWidget("Screen",id,error)&&compileBlueprint(doc,source,error);
    NX_EXPECT(ok);if(!ok)return;
    r.platform.files["showcase/main/screen.json"]="{\"title\":\"Stretch HUD\"}";r.platform.files["showcase/main/main.as"]=source;
    if(!r.screens.init(r.services,"showcase","main")){NX_EXPECT(false);return;}r.step(3);if(!r.services.ui){NX_EXPECT(false);return;}
    auto& view=*r.services.ui;auto* stack=view.find("item-1");auto* label=view.find("item-2");NX_EXPECT(stack&&label);if(!stack||!label)return;
    view.drawDesign(r.canvas,0,0,1280,720);const float wide=label->w;
    NX_EXPECT(fabsf(label->w-(stack->w-stack->pad(0)-stack->pad(2)-40))<0.01f);
    NX_EXPECT(fabsf(label->h-(stack->h-stack->pad(1)-stack->pad(3)-60))<0.01f);
    view.drawDesign(r.canvas,0,0,960,720);NX_EXPECT(fabsf(wide-label->w-320)<0.01f);
}

NX_TEST(nativeLinkedWidgetComponentsLoadWithIndependentIDs)
{
    Rig r;NX_EXPECT(r.ok);if(!r.ok)return;
    BlueprintDocument doc;doc.create();std::string page,child,error,source;
    const bool ok=doc.createVisualAsset("widget",page,error)&&doc.createVisualAsset("widget",child,error)&&
        doc.appendVisualWidget(child,"button","Reusable",error)&&doc.appendVisualWidget(page,"component",child,error)&&
        doc.appendVisualWidget(page,"component",child,error)&&doc.connectVisualWidget("Screen",page,error)&&compileBlueprint(doc,source,error);
    NX_EXPECT(ok);if(!ok)return;
    r.platform.files["showcase/main/screen.json"]="{\"title\":\"Components\"}";r.platform.files["showcase/main/main.as"]=source;
    if(!r.screens.init(r.services,"showcase","main")){NX_EXPECT(false);return;}r.step(3);NX_EXPECT(!r.screens.failed());if(!r.services.ui)return;
    auto* first=r.services.ui->find("item-1/item-1");auto* second=r.services.ui->find("item-2/item-1");
    NX_EXPECT(first&&second&&first!=second);
}

NX_TEST(nativeActorCameraTracksParentAndRespectsManualCamera)
{
    Rig r;NX_EXPECT(r.ok);if(!r.ok)return;
    BlueprintDocument doc;doc.create();std::string id,error,source;
    NX_EXPECT(doc.createVisualAsset("actor",id,error)&&doc.appendVisualActorComponent(id,0,"camera","0,2,-5",error));
    NX_EXPECT(doc.setVisualActorProperty(id,1,"camera","75",error)&&doc.visualActorSource(id,source,error));
    JsonTree tree;NX_EXPECT(tree.parse(source.data(),source.size()));
    World world(r.assets,r.host,"blueprint",64,32);auto& space=world.space();space.init(&r.solids,&r.audio);
    const u32 root=space.makeActor(tree.root(),"showcase/main",{3,4,5});NX_EXPECT(root);if(!root)return;
    const u32 camera=world.findNamed("Camera1");NX_EXPECT(camera&&world.parentOf(camera)==root);
    NX_EXPECT(space.getAppearance(camera)==nullptr);
    space.update(1.f/60);auto eye=space.cameraPlace();
    NX_EXPECT(fabsf(eye.x-3)<0.001f&&fabsf(eye.y-6)<0.001f&&fabsf(eye.z)<0.001f);
    space.getSpatial(root)->place={8,4,5};space.update(1.f/60);eye=space.cameraPlace();
    NX_EXPECT(fabsf(eye.x-8)<0.001f);
    space.setCamera({0,5,8},{0,0,0},60);space.update(1.f/60);eye=space.cameraPlace();
    NX_EXPECT(eye.x==0&&eye.y==5&&eye.z==8);
}

NX_TEST(nativeWidgetTimelineInterpolatesOnRenderer)
{
    Rig r;NX_EXPECT(r.ok);if(!r.ok)return;
    BlueprintDocument doc;doc.create();std::string id,error,source;
    const bool ok=doc.createVisualAsset("widget",id,error)&&doc.appendVisualWidget(id,"label","Animated",error)&&
        doc.setWidgetKey(id,"item-1","x","0",0,error)&&doc.setWidgetKey(id,"item-1","x","100",1,error)&&
        doc.setWidgetKey(id,"item-1","opacity","0",0,error)&&doc.setWidgetKey(id,"item-1","opacity","1",1,error)&&
        doc.connectVisualWidget("Screen",id,error)&&compileBlueprint(doc,source,error);
    NX_EXPECT(ok);if(!ok)return;
    r.platform.files["showcase/main/screen.json"]="{\"title\":\"Timeline\"}";r.platform.files["showcase/main/main.as"]=source;
    if(!r.screens.init(r.services,"showcase","main")){NX_EXPECT(false);return;}r.step(3);if(!r.services.ui){NX_EXPECT(false);return;}
    const auto* widget=r.services.ui->find("item-1");NX_EXPECT(widget);if(!widget)return;
    r.device->beginFrame({});r.canvas.begin();r.services.ui->drawDesign(r.canvas,0,0,1280,720,nullptr,-1,0.5f);r.canvas.end();r.device->endFrame();
    NX_EXPECT(fabsf(widget->animationValues[0]-50)<0.001f&&fabsf(widget->animationValues[3]-0.5f)<0.001f);
}

NX_TEST(nativePlacementViewportProjectsRayToWorldPlane)
{
    Rig r;NX_EXPECT(r.ok);if(!r.ok)return;
    BlueprintDocument doc;doc.create();std::string id,error,source;
    NX_EXPECT(doc.createVisualAsset("actor",id,error)&&doc.visualActorSource(id,source,error));
    JsonTree tree;NX_EXPECT(tree.parse(source.data(),source.size()));
    World world(r.assets,r.host,"blueprint",64,0);auto& space=world.space();space.init(&r.solids);
    NX_EXPECT(space.makeActor(tree.root(),"showcase/main",{3,2,5}));
    space.setCamera({15,18,22},{0,0,0},55);
    r.device->beginFrame({});r.canvas.begin();space.draw(r.canvas,100,80,800,500);r.canvas.end();r.device->endFrame();
    Vec2 point;NX_EXPECT(space.toScreen({3,2,5},point));
    const auto ray=space.rayThrough(point),eye=space.cameraPlace();const float t=(2-eye.y)/ray.y;
    NX_EXPECT(t>0&&fabsf(eye.x+ray.x*t-3)<0.01f&&fabsf(eye.z+ray.z*t-5)<0.01f);
    NX_EXPECT(doc.placeVisualActor("Screen",id,"3,2,5",error));
}

NX_TEST(nativeActorCameraSelectionIsInstanceScoped)
{
    Rig r;NX_EXPECT(r.ok);if(!r.ok)return;
    BlueprintDocument doc;doc.create();std::string id,error,source;
    NX_EXPECT(doc.createVisualAsset("actor",id,error)&&doc.appendVisualActorComponent(id,0,"camera","0,2,-5",error));
    NX_EXPECT(doc.appendVisualActorComponent(id,0,"camera","0,8,-10",error)&&doc.visualActorSource(id,source,error));
    JsonTree tree;NX_EXPECT(tree.parse(source.data(),source.size()));
    World world(r.assets,r.host,"blueprint",64,32);auto& space=world.space();space.init(&r.solids,&r.audio);
    const u32 a=space.makeActor(tree.root(),"showcase/main",{3,4,5}),b=space.makeActor(tree.root(),"showcase/main",{13,4,5});
    NX_EXPECT(a&&b);if(!a||!b)return;
    NX_EXPECT(space.activateActorCamera(a,"Camera1"));
    NX_EXPECT(fabsf(space.cameraPlace().x-3)<0.001f);
    NX_EXPECT(space.activateActorCamera(b,"Camera1"));
    NX_EXPECT(fabsf(space.cameraPlace().x-13)<0.001f);
    NX_EXPECT(!space.activateActorCamera(a,"")&&!space.activateActorCamera(a,"Missing"));
    NX_EXPECT(fabsf(space.cameraPlace().x-13)<0.001f);
    NX_EXPECT(space.activateActorCamera(a,"Camera2")&&fabsf(space.cameraPlace().y-12)<0.001f);
    world.destroyEntity(a);
    NX_EXPECT(!space.activateActorCamera(a,"Camera2"));
}

NX_TEST(nativeActorComponentLookupIsScopedAndRejectsAmbiguity)
{
    Rig r;NX_EXPECT(r.ok);if(!r.ok)return;
    BlueprintDocument doc;doc.create();std::string id,error,source;
    NX_EXPECT(doc.createVisualAsset("actor",id,error)&&doc.appendVisualActorComponent(id,0,"box","0,1,0",error));
    NX_EXPECT(doc.appendVisualActorComponent(id,1,"sphere","0,2,0",error)&&doc.visualActorSource(id,source,error));
    JsonTree tree;NX_EXPECT(tree.parse(source.data(),source.size()));
    World world(r.assets,r.host,"blueprint",64,32);auto& space=world.space();space.init(&r.solids,&r.audio);
    const u32 first=space.makeActor(tree.root(),"showcase/main",{0,0,0}),second=space.makeActor(tree.root(),"showcase/main",{10,0,0});
    NX_EXPECT(first&&second);if(!first||!second)return;
    const auto a=world.findComponent(first,"Component2"),b=world.findComponent(second,"Component2");
    NX_EXPECT(a&&b&&a!=b&&world.parentOf(world.parentOf(a))==first&&world.parentOf(world.parentOf(b))==second);
    NX_EXPECT(!world.findComponent(first,"Root")&&!world.findComponent(first,"Missing"));
    const auto duplicate=space.makeActor(tree.root(),"showcase/main",{0,0,0});NX_EXPECT(duplicate);if(!duplicate)return;
    world.setParent(duplicate,first);
    NX_EXPECT(!world.findComponent(first,"Component2"));
    world.destroyEntity(duplicate);
    NX_EXPECT(world.findComponent(first,"Component2")==a);
}

NX_TEST(nativeFlowControlsExecuteWithPersistentCounters)
{
    BlueprintDocument doc;doc.create();std::string error,source;
    bool ok=true;const size_t g=1;
    auto add=[&](const char* kind){ok=ok&&doc.addNode(g,kind,0,0,error);};
    auto wire=[&](size_t from,const char* output,size_t to,const char* input){ok=ok&&doc.connect(g,from,output,to,input,error);};
    auto input=[&](size_t n,const char* pin,const char* value){ok=ok&&doc.setInputValue(g,n,pin,value,error);};
    auto ref=[&](size_t n,const char* value){ok=ok&&doc.setReference(g,n,value,error);};
    add("flow:do-once");add("var:get:int");add("add:int");add("var:set:int");
    add("flow:do-n");add("var:get:int");add("add:int");add("var:set:int");
    add("equal:int");add("equal:int");add("and:bool");add("branch");
    add("api:Save:Save::set:string,string");add("api:Save:Save::set:string,string");
    ref(2,"onceCount");ref(4,"onceCount");ref(6,"nCount");ref(8,"nCount");
    wire(0,"then",1,"in");wire(1,"body",4,"in");wire(2,"value",3,"a");input(3,"b","1");wire(3,"result",4,"value");
    wire(1,"then",5,"in");input(5,"count","3");wire(5,"body",8,"in");wire(6,"value",7,"a");input(7,"b","1");wire(7,"result",8,"value");
    wire(5,"then",12,"in");wire(2,"value",9,"a");input(9,"b","1");wire(6,"value",10,"a");input(10,"b","3");
    wire(9,"result",11,"a");wire(10,"result",11,"b");wire(11,"result",12,"condition");
    wire(12,"true",13,"in");wire(12,"false",14,"in");input(13,"key","flow");input(13,"value","ok");input(14,"key","flow");input(14,"value","bad");
    if(!ok||!compileBlueprint(doc,source,error)){NX_ERROR("Flow authoring: %s",error.c_str());NX_EXPECT(false);return;}
    Rig r;NX_EXPECT(r.ok);if(!r.ok)return;
    r.platform.files["project/main/screen.json"]="{\"title\":\"Flow controls\"}";
    r.platform.files["project/main/main.as"]=source;
    if(!r.screens.init(r.services,"project","main")){NX_EXPECT(false);return;}
    r.step(10);NX_EXPECT(!r.screens.failed()&&strcmp(r.save.get("flow","missing"),"ok")==0);
}

NX_TEST(nativeInheritedActorsAndComponentValidityThroughCompiledGraphs)
{
    Rig r;NX_EXPECT(r.ok);if(!r.ok)return;
    BlueprintDocument doc;doc.create();std::string base,child,error,source,bytes;
    bool ok=doc.createVisualAsset("actor",base,error)&&
        doc.appendVisualActorComponent(base,0,"box","0,1,0",error)&&
        doc.setVisualActorProperty(base,1,"name","Socket",error)&&
        doc.createVisualActorChild(base,child,error)&&
        doc.appendVisualActorComponent(child,1,"sphere","0,2,0",error)&&
        doc.setVisualActorProperty(child,2,"name","Sensor",error)&&
        doc.setVisualActorProperty(child,1,"place","0,3,0",error)&&
        doc.setVisualActorProperty(base,1,"size","2,2,2",error)&&
        doc.appendVisualActorComponent(base,0,"box","0,0,1",error)&&
        doc.placeVisualActor("Screen",child,"0,0,0",error)&&
        doc.placeVisualActor("Screen",child,"10,0,0",error)&&
        doc.setInputValue(0,1,"instanceName","First",error)&&
        doc.setInputValue(0,2,"instanceName","Second",error);
    auto add=[&](const char* kind){const auto n=doc.graphs[0].nodes.size();ok=ok&&doc.addNode(0,kind,0,0,error);return n;};
    auto wire=[&](size_t from,const char* output,size_t to,const char* input){ok=ok&&doc.connect(0,from,output,to,input,error);};
    auto input=[&](size_t n,const char* pin,const char* value){ok=ok&&doc.setInputValue(0,n,pin,value,error);};
    auto lookup=[&](size_t actor,const char* name){const auto n=add("actor:component");wire(actor,"result",n,"actor");input(n,"component",name);return n;};
    auto valid=[&](size_t n){const auto check=add("actor:valid");wire(n,"result",check,"entity");return check;};
    auto gate=[&](size_t previous,size_t condition){const auto n=add("flow:gate");wire(previous,"then",n,"in");wire(condition,"result",n,"open");return n;};
    auto save=[&](size_t from,const char* pin,const char* key,const char* value){const auto n=add("api:Save:Save::set:string,string");wire(from,pin,n,"in");input(n,"key",key);input(n,"value",value);return n;};
    const auto socket=lookup(1,"Socket"),other=lookup(2,"Socket"),missing=lookup(1,"Missing");
    const auto captured=add("capture:Entity@");wire(2,"then",captured,"in");wire(socket,"result",captured,"value");
    const auto initial=gate(captured,valid(captured));save(initial,"body","component_live","yes");
    const auto absent=gate(initial,valid(missing));save(absent,"body","unexpected_missing","yes");
    const auto destroy=add("api:Entity:destroy:Entity@");wire(absent,"then",destroy,"in");wire(captured,"result",destroy,"target");
    const auto stale=gate(destroy,valid(captured));save(stale,"body","unexpected_stale","yes");
    const auto surviving=gate(stale,valid(other));save(surviving,"body","other_live","yes");
    save(surviving,"then","continued","yes");
    if(!ok||!doc.serialize(bytes,error)){NX_ERROR("Actor authoring: %s",error.c_str());NX_EXPECT(false);return;}
    BlueprintDocument copy;
    if(!copy.load(bytes.data(),bytes.size(),error)||!compileBlueprint(copy,source,error)){NX_ERROR("Actor compilation: %s",error.c_str());NX_EXPECT(false);return;}
    r.platform.files["project/main/screen.json"]="{\"title\":\"Inherited actors and validity\"}";
    r.platform.files["project/main/main.as"]=source;
    if(!r.screens.init(r.services,"project","main")){NX_EXPECT(false);return;}
    r.step(5);NX_EXPECT(!r.screens.failed());
    NX_EXPECT(strcmp(r.save.get("component_live","missing"),"yes")==0);
    NX_EXPECT(strcmp(r.save.get("unexpected_missing","missing"),"missing")==0);
    NX_EXPECT(strcmp(r.save.get("unexpected_stale","missing"),"missing")==0);
    NX_EXPECT(strcmp(r.save.get("other_live","missing"),"yes")==0&&strcmp(r.save.get("continued","missing"),"yes")==0);
    if(!r.services.world){NX_EXPECT(false);return;}
    auto& world=*r.services.world;
    const auto first=world.findNamed("First"),second=world.findNamed("Second");
    NX_EXPECT(first&&second&&!world.findComponent(first,"Socket")&&!world.findComponent(first,"Sensor"));
    const auto survivingSocket=world.findComponent(second,"Socket"),sensor=world.findComponent(second,"Sensor");
    NX_EXPECT(survivingSocket&&sensor&&world.parentOf(sensor)==survivingSocket);
    const auto* spatial=world.space().getSpatial(survivingSocket);
    NX_EXPECT(spatial&&fabsf(spatial->place.y-3)<0.01f&&fabsf(spatial->size.x-2)<0.01f);
}

NX_TEST(nativeFlowResetAndGateIsolationAcrossBehaviourInstances)
{
    Rig r;NX_EXPECT(r.ok);if(!r.ok)return;
    BlueprintDocument doc;doc.create();std::string id,error,source;
    bool ok=doc.createVisualAsset("actor",id,error)&&doc.addBehaviour("FlowTest",error)&&
        doc.setVisualActorProperty(id,0,"behaviour","FlowTest",error)&&
        doc.placeVisualActor("Screen",id,"0,0,0",error)&&doc.placeVisualActor("Screen",id,"10,0,0",error)&&
        doc.setInputValue(0,1,"instanceName","FlowFirst",error)&&doc.setInputValue(0,2,"instanceName","FlowSecond",error);
    size_t g=doc.graphs.size();
    for(size_t i=0;i<doc.graphs.size();++i)if(doc.graphs[i].owner=="behaviour:FlowTest"&&doc.graphs[i].name=="update"){g=i;break;}
    if(!ok||g==doc.graphs.size()){NX_ERROR("Behaviour authoring: %s",error.c_str());NX_EXPECT(false);return;}
    auto add=[&](const char* kind){const auto n=doc.graphs[g].nodes.size();ok=ok&&doc.addNode(g,kind,0,0,error);return n;};
    auto wire=[&](size_t from,const char* output,size_t to,const char* input){ok=ok&&doc.connect(g,from,output,to,input,error);};
    auto input=[&](size_t n,const char* pin,const char* value){ok=ok&&doc.setInputValue(g,n,pin,value,error);};
    auto get=[&](const char* name){const auto n=add("var:get:int");ok=ok&&doc.setReference(g,n,name,error);return n;};
    auto increment=[&](size_t from,const char* output,const char* name)
    {
        const auto current=get(name),sum=add("add:int"),set=add("var:set:int");
        ok=ok&&doc.setReference(g,set,name,error);wire(current,"value",sum,"a");input(sum,"b","1");
        wire(sum,"result",set,"value");wire(from,output,set,"in");return set;
    };
    auto equals=[&](const char* field,const char* value){const auto current=get(field),eq=add("equal:int");wire(current,"value",eq,"a");input(eq,"b",value);return eq;};
    auto both=[&](size_t a,size_t b){const auto n=add("and:bool");wire(a,"result",n,"a");wire(b,"result",n,"b");return n;};
    const auto tick=increment(0,"then","tick");
    const auto reset=equals("tick","5");
    const auto once=add("flow:do-once");wire(tick,"then",once,"in");wire(reset,"result",once,"reset");increment(once,"body","onceCount");
    const auto n=add("flow:do-n");wire(once,"then",n,"in");input(n,"count","3");wire(reset,"result",n,"reset");increment(n,"body","nCount");
    const auto gate=add("flow:gate");wire(n,"then",gate,"in");wire(equals("tick","4"),"result",gate,"open");increment(gate,"body","gateCount");
    const auto flip=add("flow:flip-flop");wire(gate,"then",flip,"in");wire(reset,"result",flip,"reset");
    increment(flip,"a","flipA");increment(flip,"b","flipB");
    const auto multi=add("flow:multi-gate");wire(flip,"then",multi,"in");wire(reset,"result",multi,"reset");
    increment(multi,"first","multiFirst");increment(multi,"fourth","multiFourth");
    const auto seq=add("sequence");wire(multi,"then",seq,"in");
    const auto controlled=add("flow:controlled-gate");wire(seq,"first",controlled,"open");wire(seq,"second",controlled,"in");increment(controlled,"body","controlledCount");
    const auto seq2=add("sequence");wire(seq,"then",seq2,"in");wire(seq2,"first",controlled,"close");wire(seq2,"second",controlled,"toggle");
    const auto sw=add("flow:switch-int");wire(seq2,"then",sw,"in");input(sw,"value","9");increment(sw,"default","switchCount");
    const auto sample=add("branch");wire(sw,"then",sample,"in");wire(equals("tick","15"),"result",sample,"condition");
    const auto branch=add("branch");wire(sample,"true",branch,"in");
    const auto checks=both(both(both(equals("onceCount","2"),equals("nCount","6")),equals("gateCount","1")),
        both(both(equals("multiFirst","2"),equals("multiFourth","2")),both(both(equals("flipA","8"),equals("flipB","7")),
        both(equals("controlledCount","15"),equals("switchCount","15")))));
    wire(checks,"result",branch,"condition");
    const auto self=add("event:entity"),name=add("api:Entity:get_name:Entity@");wire(self,"value",name,"target");
    const auto good=add("api:Save:Save::set:string,string"),bad=add("api:Save:Save::set:string,string");
    wire(name,"result",good,"key");wire(name,"result",bad,"key");
    wire(branch,"true",good,"in");wire(branch,"false",bad,"in");input(good,"value","ok");input(bad,"value","bad");
    if(!ok||!compileBlueprint(doc,source,error)){NX_ERROR("Behaviour flow authoring: %s",error.c_str());NX_EXPECT(false);return;}
    r.platform.files["project/main/screen.json"]="{\"title\":\"Flow instance isolation\"}";
    r.platform.files["project/main/main.as"]=source;
    if(!r.screens.init(r.services,"project","main")){NX_EXPECT(false);return;}
    r.step(20);NX_EXPECT(!r.screens.failed());
    NX_EXPECT(strcmp(r.save.get("FlowFirst","missing"),"ok")==0&&strcmp(r.save.get("FlowSecond","missing"),"ok")==0);
}

NX_TEST(nativeActorClassOverridesAndConstructionExecuteBeforeStart)
{
    Rig r;NX_EXPECT(r.ok);if(!r.ok)return;
    BlueprintDocument doc;doc.create();std::string base,child,parent,derived,error,source,bytes;
    bool ok=doc.createVisualAsset("actor",base,error)&&doc.createVisualActorChild(base,child,error)&&
        doc.ensureActorBehaviour(child,derived,error)&&doc.ensureActorBehaviour(base,parent,error)&&
        doc.editVariableDeclaration(parent,"field health:int=100",error)&&doc.setActorFieldExposed(parent,"health",true,error)&&
        doc.editVariableDeclaration(derived,"field health:int=75",error)&&
        doc.addFunction("Construction",error,parent)&&doc.addFunction("Record",error,parent)&&doc.addFunction("Record",error,derived)&&doc.addFunction("Construction",error,derived);
    if(!ok){NX_ERROR("Actor inheritance: %s",error.c_str());NX_EXPECT(false);return;}
    auto graph=[&](const std::string& owner,const char* label)
    {for(size_t i=0;i<doc.graphs.size();++i)if(doc.graphs[i].owner==owner&&doc.graphs[i].name==label)return i;return doc.graphs.size();};
    const auto setup=graph(parent,"Construction");
    ok=ok&&doc.addNode(setup,"call:function",0,0,error,"Record")&&doc.connect(setup,0,"then",1,"in",error);
    const auto childSetup=graph(derived,"Construction");
    ok=ok&&doc.addNode(childSetup,"call:parent",0,0,error)&&doc.connect(childSetup,0,"then",1,"in",error);
    for(const auto& owner:std::vector<std::string>{parent,derived})
    {
        const auto g=graph(owner,"Record");
        ok=ok&&doc.addNode(g,"var:get:int",0,0,error)&&doc.setReference(g,1,"health",error)&&
            doc.addNode(g,"equal:int",0,0,error)&&doc.connect(g,1,"value",2,"a",error)&&doc.setInputValue(g,2,"b",owner==parent?"100":"42",error)&&
            doc.addNode(g,"branch",0,0,error)&&doc.connect(g,0,"then",3,"in",error)&&doc.connect(g,2,"result",3,"condition",error)&&
            doc.addNode(g,"api:Save:Save::set:string,string",0,0,error)&&doc.connect(g,3,"true",4,"in",error)&&
            doc.setInputValue(g,4,"key",owner==parent?"base_construction":"child_construction",error)&&doc.setInputValue(g,4,"value","ok",error);
    }
    ok=ok&&doc.placeVisualActor("Screen",base,"0,0,0",error)&&doc.placeVisualActor("Screen",child,"10,0,0",error)&&
        doc.addNode(0,"literal:int",0,0,error)&&doc.setLiteral(0,3,"42",error)&&doc.connect(0,3,"value",2,"spawn_health",error)&&doc.serialize(bytes,error);
    BlueprintDocument copy;
    if(!ok||!copy.load(bytes.data(),bytes.size(),error)||!compileBlueprint(copy,source,error)){NX_ERROR("Actor class compile: %s",error.c_str());NX_EXPECT(false);return;}
    r.platform.files["project/main/screen.json"]="{\"title\":\"Actor class construction\"}";r.platform.files["project/main/main.as"]=source;
    if(!r.screens.init(r.services,"project","main")){NX_EXPECT(false);return;}
    r.step(5);NX_EXPECT(!r.screens.failed()&&strcmp(r.save.get("base_construction","missing"),"ok")==0&&strcmp(r.save.get("child_construction","missing"),"ok")==0);
}

NX_TEST(nativeDeferredTimeExecutesOnRealScreenUpdates)
{
    Rig r;NX_EXPECT(r.ok);if(!r.ok)return;
    BlueprintDocument doc;doc.create();std::string error,source;
    bool ok=true;
    auto add=[&](const char* kind){const auto n=doc.graphs[0].nodes.size();ok=ok&&doc.addNode(0,kind,0,0,error);return n;};
    auto wire=[&](size_t from,const char* output,size_t to,const char* input){ok=ok&&doc.connect(0,from,output,to,input,error);};
    auto input=[&](size_t n,const char* pin,const char* value){ok=ok&&doc.setInputValue(0,n,pin,value,error);};
    auto save=[&](size_t from,const char* port,const char* key){const auto n=add("api:Save:Save::set:string,string");wire(from,port,n,"in");input(n,"key",key);input(n,"value","yes");};
    const auto seq=add("sequence");wire(0,"then",seq,"in");
    const auto delay=add("time:delay");wire(seq,"first",delay,"in");input(delay,"seconds","0.1");save(delay,"completed","delay_done");
    const auto cancelled=add("time:delay");wire(seq,"second",cancelled,"in");input(cancelled,"seconds","0.05");save(cancelled,"completed","cancelled");
    const auto tail=add("sequence");wire(seq,"then",tail,"in");wire(tail,"first",cancelled,"cancel");
    const auto timer=add("time:timer");wire(tail,"second",timer,"in");input(timer,"seconds","0.05");input(timer,"loop","false");save(timer,"completed","timer_done");save(timer,"then","timer_started");
    if(!ok||!compileBlueprint(doc,source,error)){NX_ERROR("Deferred time: %s",error.c_str());NX_EXPECT(false);return;}
    r.platform.files["project/main/screen.json"]="{\"title\":\"Deferred time\"}";r.platform.files["project/main/main.as"]=source;
    if(!r.screens.init(r.services,"project","main")){NX_EXPECT(false);return;}
    r.step(1);NX_EXPECT(strcmp(r.save.get("timer_started","missing"),"yes")==0);
    NX_EXPECT(strcmp(r.save.get("delay_done","missing"),"missing")==0);
    r.step(12);NX_EXPECT(!r.screens.failed());
    NX_EXPECT(strcmp(r.save.get("delay_done","missing"),"yes")==0&&strcmp(r.save.get("timer_done","missing"),"yes")==0);
    NX_EXPECT(strcmp(r.save.get("cancelled","missing"),"missing")==0);
}

NX_TEST(nativeDeferredActorInheritanceAndCallableEvents)
{
    Rig r;NX_EXPECT(r.ok);if(!r.ok)return;
    BlueprintDocument doc;doc.create();std::string base,child,parent,derived,error,source;
    bool ok=doc.createVisualAsset("actor",base,error)&&doc.createVisualActorChild(base,child,error)&&
        doc.ensureActorBehaviour(child,derived,error)&&doc.ensureActorBehaviour(base,parent,error)&&
        doc.addFunction("Later",error,derived);
    if(!ok){NX_ERROR("Deferred actors: %s",error.c_str());NX_EXPECT(false);return;}
    auto graph=[&](const std::string& owner,const char* label)
    {for(size_t i=0;i<doc.graphs.size();++i)if(doc.graphs[i].owner==owner&&doc.graphs[i].name==label)return i;return doc.graphs.size();};
    auto delayed=[&](size_t g,const char* key)
    {
        ok=ok&&doc.addNode(g,"time:delay",0,0,error)&&doc.connect(g,0,"then",1,"in",error)&&
            doc.setInputValue(g,1,"seconds","0.05",error)&&
            doc.addNode(g,"api:Save:Save::set:string,string",0,0,error)&&doc.connect(g,1,"completed",2,"in",error)&&
            doc.setInputValue(g,2,"key",key,error)&&doc.setInputValue(g,2,"value","yes",error);
    };
    delayed(graph(parent,"start"),"parent_delayed");
    delayed(graph(derived,"Later"),"child_delayed");
    const auto g=graph(derived,"update");
    ok=ok&&doc.addNode(g,"flow:do-once",0,0,error)&&doc.connect(g,0,"then",1,"in",error)&&
        doc.addNode(g,"call:function",0,0,error,"Later")&&doc.connect(g,1,"body",2,"in",error)&&
        doc.placeVisualActor("Screen",child,"0,0,0",error);
    if(!ok||!compileBlueprint(doc,source,error)){NX_ERROR("Deferred actor compile: %s",error.c_str());NX_EXPECT(false);return;}
    r.platform.files["project/main/screen.json"]="{\"title\":\"Deferred actors\"}";r.platform.files["project/main/main.as"]=source;
    if(!r.screens.init(r.services,"project","main")){NX_EXPECT(false);return;}
    r.step(15);NX_EXPECT(!r.screens.failed());
    NX_EXPECT(strcmp(r.save.get("parent_delayed","missing"),"yes")==0&&strcmp(r.save.get("child_delayed","missing"),"yes")==0);
}

NX_TEST(nativeDispatcherBindingAndActorCastExecution)
{
    Rig r;NX_EXPECT(r.ok);if(!r.ok)return;
    BlueprintDocument doc;doc.create();std::string id,error,source;
    bool ok=doc.addFunction("OnScore",error)&&doc.createVisualAsset("actor",id,error)&&
        doc.addBehaviour("Target",error)&&doc.setVisualActorProperty(id,0,"behaviour","Target",error)&&
        doc.placeVisualActor("Screen",id,"0,0,0",error);
    const auto* fn=doc.function("Screen","OnScore");if(!ok||!fn){NX_EXPECT(false);return;}
    const auto g=static_cast<size_t>(fn-doc.graphs.data());
    ok=doc.editFunctionSignature(g,"void(amount:string)",error)&&
        doc.addNode(g,"api:Save:Save::set:string,string",0,0,error)&&doc.connect(g,0,"then",1,"in",error)&&
        doc.connect(g,0,"arg_amount",1,"value",error)&&doc.setInputValue(g,1,"key","dispatcher",error);
    auto add=[&](const char* kind,const char* ref=""){const auto n=doc.graphs[0].nodes.size();ok=ok&&doc.addNode(0,kind,0,0,error,ref);return n;};
    auto wire=[&](size_t from,const char* output,size_t to,const char* input){ok=ok&&doc.connect(0,from,output,to,input,error);};
    auto input=[&](size_t n,const char* pin,const char* value){ok=ok&&doc.setInputValue(0,n,pin,value,error);};
    const auto event=add("call:event","OnScore");wire(1,"then",event,"in");input(event,"arg_amount","direct");
    const auto bind=add("dispatch:bind","OnScore");wire(event,"then",bind,"in");
    const auto send=add("dispatch:broadcast","OnScore");wire(bind,"then",send,"in");input(send,"arg_amount","bound");
    const auto unbind=add("dispatch:unbind","OnScore");wire(send,"then",unbind,"in");
    const auto ignored=add("dispatch:broadcast","OnScore");wire(unbind,"then",ignored,"in");input(ignored,"arg_amount","incorrect");
    const auto cast=add("actor:cast-class","Target");wire(ignored,"then",cast,"in");wire(1,"result",cast,"actor");
    const auto save=add("api:Save:Save::set:string,string");wire(cast,"success",save,"in");input(save,"key","cast");input(save,"value","yes");
    const auto missing=add("null:entity"),bad=add("actor:cast-class","Target");wire(cast,"then",bad,"in");wire(missing,"value",bad,"actor");
    const auto fail=add("api:Save:Save::set:string,string");wire(bad,"failure",fail,"in");input(fail,"key","null_cast");input(fail,"value","yes");
    if(!ok||!compileBlueprint(doc,source,error)){NX_ERROR("Native dispatch and cast: %s",error.c_str());NX_EXPECT(false);return;}
    r.platform.files["project/main/screen.json"]="{\"title\":\"Dispatch and cast\"}";r.platform.files["project/main/main.as"]=source;
    if(!r.screens.init(r.services,"project","main")){NX_EXPECT(false);return;}
    r.step(3);NX_EXPECT(!r.screens.failed());
    NX_EXPECT(strcmp(r.save.get("dispatcher","missing"),"bound")==0);
    NX_EXPECT(strcmp(r.save.get("cast","missing"),"yes")==0&&strcmp(r.save.get("null_cast","missing"),"yes")==0);
}

NX_TEST(nativeTypedMapAndSetExecution)
{
    Rig r;NX_EXPECT(r.ok);if(!r.ok)return;
    BlueprintDocument doc;doc.create();std::string error,source;
    bool ok=doc.editVariableDeclaration("Screen","field scores:BP_MapInt",error)&&doc.editVariableDeclaration("Screen","field names:BP_SetString",error);
    auto add=[&](const char* kind){const auto n=doc.graphs[0].nodes.size();ok=ok&&doc.addNode(0,kind,0,0,error);return n;};
    auto wire=[&](size_t from,const char* output,size_t to,const char* input){ok=ok&&doc.connect(0,from,output,to,input,error);};
    auto input=[&](size_t n,const char* pin,const char* value){ok=ok&&doc.setInputValue(0,n,pin,value,error);};
    const auto map=add("var:get:BP_MapInt"),set=add("var:get:BP_SetString");
    ok=ok&&doc.setReference(0,map,"scores",error)&&doc.setReference(0,set,"names",error);
    const auto put=add("collection:BP_MapInt:set");wire(0,"then",put,"in");wire(map,"value",put,"target");input(put,"key","player");input(put,"value","7");
    const auto replace=add("collection:BP_MapInt:set");wire(put,"then",replace,"in");wire(map,"value",replace,"target");input(replace,"key","player");input(replace,"value","42");
    const auto insert=add("collection:BP_SetString:add");wire(replace,"then",insert,"in");wire(set,"value",insert,"target");input(insert,"key","Alice");
    const auto duplicate=add("collection:BP_SetString:add");wire(insert,"then",duplicate,"in");wire(set,"value",duplicate,"target");input(duplicate,"key","Alice");
    const auto get=add("collection:BP_MapInt:get");wire(map,"value",get,"target");input(get,"key","player");input(get,"value","0");
    const auto eq=add("equal:int");wire(get,"result",eq,"a");input(eq,"b","42");
    const auto length=add("collection:BP_SetString:length");wire(set,"value",length,"target");
    const auto count=add("equal:uint");wire(length,"result",count,"a");input(count,"b","1");
    const auto both=add("and:bool");wire(eq,"result",both,"a");wire(count,"result",both,"b");
    const auto branch=add("branch");wire(duplicate,"then",branch,"in");wire(both,"result",branch,"condition");
    const auto save=add("api:Save:Save::set:string,string");wire(branch,"true",save,"in");input(save,"key","collections");input(save,"value","yes");
    if(!ok||!compileBlueprint(doc,source,error)){NX_ERROR("Typed collections: %s",error.c_str());NX_EXPECT(false);return;}
    r.platform.files["project/main/screen.json"]="{\"title\":\"Collections\"}";r.platform.files["project/main/main.as"]=source;
    if(!r.screens.init(r.services,"project","main")){NX_EXPECT(false);return;}
    r.step(3);NX_EXPECT(!r.screens.failed()&&strcmp(r.save.get("collections","missing"),"yes")==0);
}

NX_TEST(nativeSharedLibraryCalledFromBehaviour)
{
    Rig r;NX_EXPECT(r.ok);if(!r.ok)return;
    BlueprintDocument doc;doc.create();std::string error,source,id;
    bool ok=doc.addFunction("Twice",error)&&doc.createVisualAsset("actor",id,error)&&doc.addBehaviour("Consumer",error)&&doc.setVisualActorProperty(id,0,"behaviour","Consumer",error)&&doc.placeVisualActor("Screen",id,"0,0,0",error);
    const auto* fn=doc.function("Screen","Twice");if(!ok||!fn){NX_EXPECT(false);return;}
    const auto g=static_cast<size_t>(fn-doc.graphs.data());
    ok=doc.editFunctionSignature(g,"int(amount:int)",error)&&doc.addNode(g,"add:int",0,0,error)&&doc.connect(g,0,"arg_amount",1,"a",error)&&doc.connect(g,0,"arg_amount",1,"b",error)&&
        doc.addNode(g,"return:function",0,0,error)&&doc.connect(g,0,"then",2,"in",error)&&doc.connect(g,1,"result",2,"value",error)&&doc.setSharedLibrary(g,true,error);
    size_t actor=0;for(size_t i=0;i<doc.graphs.size();++i)if(doc.graphs[i].owner=="behaviour:Consumer"&&doc.graphs[i].name=="start")actor=i;
    ok=ok&&doc.addNode(actor,"call:library",0,0,error,"Twice")&&doc.connect(actor,0,"then",1,"in",error)&&doc.setInputValue(actor,1,"arg_amount","21",error)&&
        doc.addNode(actor,"equal:int",0,0,error)&&doc.connect(actor,1,"result",2,"a",error)&&doc.setInputValue(actor,2,"b","42",error)&&
        doc.addNode(actor,"branch",0,0,error)&&doc.connect(actor,1,"then",3,"in",error)&&doc.connect(actor,2,"result",3,"condition",error)&&
        doc.addNode(actor,"api:Save:Save::set:string,string",0,0,error)&&doc.connect(actor,3,"true",4,"in",error)&&doc.setInputValue(actor,4,"key","library",error)&&doc.setInputValue(actor,4,"value","yes",error);
    if(!ok||!compileBlueprint(doc,source,error)){NX_ERROR("Shared library: %s",error.c_str());NX_EXPECT(false);return;}
    r.platform.files["project/main/screen.json"]="{\"title\":\"Shared library\"}";r.platform.files["project/main/main.as"]=source;
    if(!r.screens.init(r.services,"project","main")){NX_EXPECT(false);return;}
    r.step(5);NX_EXPECT(!r.screens.failed()&&strcmp(r.save.get("library","missing"),"yes")==0);
}

NX_TEST(nativeCustomEnumAndStructRuntime)
{
    Rig r;NX_EXPECT(r.ok);if(!r.ok)return;
    BlueprintDocument doc;doc.create();std::string error,source;
    bool ok=doc.addFunction("State",error);const auto en=doc.graphs.size()-1;
    ok=ok&&doc.editFunctionSignature(en,"enum(Idle, Running)",error)&&doc.addFunction("Stats",error);
    const auto st=doc.graphs.size()-1;ok=ok&&doc.editFunctionSignature(st,"struct(health:int, name:string)",error);
    auto add=[&](const char* kind,const char* ref=""){const auto n=doc.graphs[0].nodes.size();ok=ok&&doc.addNode(0,kind,0,0,error,ref);return n;};
    auto wire=[&](size_t from,const char* output,size_t to,const char* input){ok=ok&&doc.connect(0,from,output,to,input,error);};
    auto input=[&](size_t n,const char* pin,const char* value){ok=ok&&doc.setInputValue(0,n,pin,value,error);};
    const auto a=add("data:enum-value","State"),b=add("data:enum-value","State");input(a,"member","Running");input(b,"member","Running");
    const auto eq=add("data:enum-equal","State");wire(a,"result",eq,"a");wire(b,"result",eq,"b");
    const auto make=add("data:make-struct","Stats");input(make,"health","42");input(make,"name","hero");
    const auto split=add("data:break-struct","Stats");wire(make,"result",split,"value");
    const auto check=add("equal:int");wire(split,"health",check,"a");input(check,"b","42");
    const auto both=add("and:bool");wire(eq,"result",both,"a");wire(check,"result",both,"b");
    const auto branch=add("branch");wire(0,"then",branch,"in");wire(both,"result",branch,"condition");
    const auto save=add("api:Save:Save::set:string,string");wire(branch,"true",save,"in");input(save,"key","custom_types");wire(split,"name",save,"value");
    if(!ok||!compileBlueprint(doc,source,error)){NX_ERROR("Custom types: %s",error.c_str());NX_EXPECT(false);return;}
    r.platform.files["project/main/screen.json"]="{\"title\":\"Custom types\"}";r.platform.files["project/main/main.as"]=source;
    if(!r.screens.init(r.services,"project","main")){NX_EXPECT(false);return;}
    r.step(3);NX_EXPECT(!r.screens.failed()&&strcmp(r.save.get("custom_types","missing"),"hero")==0);
}

NX_TEST(nativeCoroutineAndMultilistenerRuntime)
{
    Rig r;NX_EXPECT(r.ok);if(!r.ok)return;
    BlueprintDocument doc;doc.create();std::string error,source;
    bool ok=doc.addFunction("Later",error);const auto co=doc.graphs.size()-1;
    ok=ok&&doc.editFunctionSignature(co,"coroutine()",error)&&doc.addNode(co,"async:wait",0,0,error)&&doc.connect(co,0,"then",1,"in",error)&&doc.setInputValue(co,1,"seconds","0.1",error)&&
        doc.addNode(co,"api:Save:Save::set:string,string",0,0,error)&&doc.connect(co,1,"then",2,"in",error)&&doc.setInputValue(co,2,"key","coroutine",error)&&doc.setInputValue(co,2,"value","yes",error);
    for(const char* label:{"Signal","Receiver"})
    {
        ok=ok&&doc.addFunction(label,error);const auto g=doc.graphs.size()-1;
        ok=ok&&doc.editFunctionSignature(g,"event(message:string)",error)&&doc.addNode(g,"api:Save:Save::set:string,string",0,0,error)&&doc.connect(g,0,"then",1,"in",error)&&doc.connect(g,0,"arg_message",1,"value",error)&&doc.setInputValue(g,1,"key",label,error);
    }
    auto add=[&](const char* kind,const char* ref){const auto n=doc.graphs[0].nodes.size();ok=ok&&doc.addNode(0,kind,0,0,error,ref);return n;};
    auto wire=[&](size_t from,size_t to){ok=ok&&doc.connect(0,from,"then",to,"in",error);};
    const auto launch=add("async:start","Later");wire(0,launch);
    const auto bind=add("dispatch:bind","Signal");wire(launch,bind);
    const auto listener=add("dispatch:bind-listener","Signal");wire(bind,listener);ok=ok&&doc.setInputValue(0,listener,"handler","Receiver",error);
    const auto broadcast=add("dispatch:broadcast","Signal");wire(listener,broadcast);ok=ok&&doc.setInputValue(0,broadcast,"arg_message","first",error);
    const auto unbind=add("dispatch:unbind-listener","Signal");wire(broadcast,unbind);ok=ok&&doc.setInputValue(0,unbind,"handler","Receiver",error);
    const auto again=add("dispatch:broadcast","Signal");wire(unbind,again);ok=ok&&doc.setInputValue(0,again,"arg_message","second",error);
    if(!ok||!compileBlueprint(doc,source,error)){NX_ERROR("Coroutine/dispatcher: %s",error.c_str());NX_EXPECT(false);return;}
    r.platform.files["project/main/screen.json"]="{\"title\":\"Coroutine and listeners\"}";r.platform.files["project/main/main.as"]=source;
    if(!r.screens.init(r.services,"project","main")){NX_EXPECT(false);return;}
    r.step(1);NX_EXPECT(strcmp(r.save.get("coroutine","missing"),"missing")==0);
    r.step(20);NX_EXPECT(!r.screens.failed());
    NX_EXPECT(strcmp(r.save.get("coroutine","missing"),"yes")==0&&strcmp(r.save.get("Signal","missing"),"second")==0&&strcmp(r.save.get("Receiver","missing"),"first")==0);
}

NX_TEST(nativeTypedActorHandleRuntime)
{
    Rig r;NX_EXPECT(r.ok);if(!r.ok)return;
    BlueprintDocument doc;doc.create();std::string id,error,source;
    bool ok=doc.createVisualAsset("actor",id,error)&&doc.addBehaviour("Target",error)&&doc.setVisualActorProperty(id,0,"behaviour","Target",error)&&doc.placeVisualActor("Screen",id,"0,0,0",error);
    ok=ok&&doc.editVariableDeclaration("Screen","field target:BP_Behaviour_Target@",error);
    ok=ok&&doc.addNode(0,"actor:cast-class",0,0,error,"Target")&&doc.connect(0,1,"then",2,"in",error)&&doc.connect(0,1,"result",2,"actor",error);
    ok=ok&&doc.addVariableNode(0,"target",false,true,0,0,error)&&doc.connect(0,2,"success",3,"in",error)&&doc.connect(0,2,"typed",3,"value",error);
    ok=ok&&doc.addVariableNode(0,"target",false,false,0,0,error)&&doc.addNode(0,"actor:class-entity",0,0,error,"Target")&&doc.connect(0,4,"value",5,"target",error);
    ok=ok&&doc.addNode(0,"actor:valid",0,0,error)&&doc.connect(0,5,"result",6,"entity",error);
    ok=ok&&doc.addNode(0,"branch",0,0,error)&&doc.connect(0,2,"then",7,"in",error)&&doc.connect(0,6,"result",7,"condition",error);
    ok=ok&&doc.addNode(0,"api:Save:Save::set:string,string",0,0,error)&&doc.connect(0,7,"true",8,"in",error)&&doc.setInputValue(0,8,"key","typed_actor",error)&&doc.setInputValue(0,8,"value","yes",error);
    if(!ok||!compileBlueprint(doc,source,error)){NX_ERROR("Typed actor: %s",error.c_str());NX_EXPECT(false);return;}
    r.platform.files["project/main/screen.json"]="{\"title\":\"Typed actor\"}";r.platform.files["project/main/main.as"]=source;
    if(!r.screens.init(r.services,"project","main")){NX_EXPECT(false);return;}
    r.step(3);NX_EXPECT(!r.screens.failed()&&strcmp(r.save.get("typed_actor","missing"),"yes")==0);
}
