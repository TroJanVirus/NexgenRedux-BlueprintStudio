#include "test.h"
#include "rig.h"
#include "blueprint_fixtures.h"
#include "scene/world.h"
#include "tools/blueprint/native_compile.h"
#ifdef NX_DESKTOP_BLUEPRINT
#include "app/blueprint_workspace.h"
#include "tools/blueprint/native_assets.h"
#endif

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
        if(studio.previewing())r.screens.draw();else studio.draw(r.canvas);
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
    NX_EXPECT(authored&&compileBlueprintProject(doc,output,error));if(output.size()!=2)return;
    std::vector<Screens::PreviewScreen> preview;
    for(const auto& o:output)
    {
        Screens::PreviewScreen p;snprintf(p.info.id,sizeof(p.info.id),"%s",o.id.c_str());p.source=o.source;preview.push_back(p);
    }
    NX_EXPECT(r.screens.startProjectPreview(preview,"Screen",error));if(!r.screens.previewing())return;
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
