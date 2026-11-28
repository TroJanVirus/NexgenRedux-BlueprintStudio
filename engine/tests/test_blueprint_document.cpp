#include "test.h"
#include "script/studio_preview.h"
#include "script/studio_actor_value.h"
#include "script/studio_live_source.h"
#include "script/studio_launch.h"
#include "script/source_fingerprint.h"
#include "base/json_tree.h"
#include "tools/blueprint/native_document.h"
#include "tools/blueprint/native_compile.h"
#include "tools/blueprint/native_import.h"
#include <cstring>
#include <algorithm>

using namespace nx;
NX_TEST(nativePromoteInputPinToVariable)
{
    BlueprintDocument doc;doc.create();std::string error,source;
    NX_EXPECT(doc.addNode(0,"api:Save:Save::set:string,string",300,100,error));
    NX_EXPECT(doc.connect(0,0,"then",1,"in",error));
    NX_EXPECT(doc.setInputValue(0,1,"key","example",error));
    NX_EXPECT(doc.setInputValue(0,1,"value","stored value",error));
    NX_EXPECT(doc.promotePin(0,1,"key",false,"saveKey",error));
    NX_EXPECT(doc.graphs[0].nodes.size()==3&&doc.graphs[0].links.size()==2);
    NX_EXPECT(compileBlueprint(doc,source,error)&&source.find("saveKey")!=std::string::npos);
    NX_EXPECT(!doc.promotePin(0,1,"key",false,"another",error));
    NX_EXPECT(doc.undo()&&doc.graphs[0].nodes.size()==2);
    NX_EXPECT(doc.redo()&&doc.graphs[0].nodes.size()==3);
}
NX_TEST(nativeDeleteSelectedNodesIsAtomic)
{
    BlueprintDocument doc;doc.create();std::string error;
    NX_EXPECT(doc.addNode(0,"api:Save:Save::set:string,string",200,100,error));
    NX_EXPECT(doc.addNode(0,"api:Save:Save::set:string,string",500,100,error));
    NX_EXPECT(doc.connect(0,0,"then",1,"in",error));
    NX_EXPECT(doc.connect(0,1,"then",2,"in",error));
    NX_EXPECT(doc.addComment(0,{1,2},"Temporary nodes",error));
    NX_EXPECT(!doc.deleteNodes(0,{0,1},error)&&doc.graphs[0].nodes.size()==3);
    NX_EXPECT(doc.deleteNodes(0,{1,2},error));
    NX_EXPECT(doc.graphs[0].nodes.size()==1&&doc.graphs[0].links.empty()&&doc.graphs[0].comments.empty());
    NX_EXPECT(doc.undo()&&doc.graphs[0].nodes.size()==3&&doc.graphs[0].links.size()==2&&doc.graphs[0].comments.size()==1);
    NX_EXPECT(doc.redo()&&doc.graphs[0].nodes.size()==1);
}
NX_TEST(nativeCatalogueContainsReferenceAPI)
{
    NX_EXPECT(blueprintCatalogue().size()==1559);
    const auto* save=blueprintSpec("api:Save:Save::set:string,string");
    NX_EXPECT(save&&save->inputs.size()==3&&save->outputs.size()==1);
    if(save&&save->inputs.size()==3)NX_EXPECT(save->inputs[0].type=="exec"&&save->inputs[2].type=="string");
}
NX_TEST(nativeBlueprintCreatesConnectsAndRoundTrips)
{
    BlueprintDocument doc;doc.create();std::string error,result;
    NX_EXPECT(doc.graphs.size()==6&&!doc.dirty());
    NX_EXPECT(doc.addNode(0,"api:Save:Save::set:string,string",300,50,error));
    NX_EXPECT(doc.addNode(0,"literal:string",20,200,error));
    NX_EXPECT(doc.setLiteral(0,2,"Hello",error));
    NX_EXPECT(doc.connect(0,0,"then",1,"in",error));
    NX_EXPECT(doc.connect(0,2,"value",1,"key",error));
    NX_EXPECT(!doc.connect(0,2,"value",1,"in",error));
    NX_EXPECT(!doc.connect(0,2,"value",1,"key",error));
    NX_EXPECT(!doc.deleteNode(0,0,error));
    NX_EXPECT(doc.serialize(result,error));
    BlueprintDocument copy;NX_EXPECT(copy.load(result.data(),result.size(),error));
    if(copy.graphs.size()==6&&copy.graphs[0].nodes.size()==3)
    {
        NX_EXPECT(copy.graphs[0].nodes[2].ref=="Hello");
        NX_EXPECT(copy.graphs[0].links.size()==2&&!copy.dirty());
    }
}
NX_TEST(nativeBlueprintHistoryRestoresNodesAndWires)
{
    BlueprintDocument doc;doc.create();std::string error;
    NX_EXPECT(doc.addNode(0,"branch",300,30,error));
    NX_EXPECT(doc.connect(0,0,"then",1,"in",error));
    doc.markSaved();
    NX_EXPECT(doc.deleteNode(0,1,error)&&doc.graphs[0].links.empty()&&doc.dirty());
    NX_EXPECT(doc.undo()&&!doc.dirty()&&doc.graphs[0].links.size()==1);
    NX_EXPECT(doc.redo()&&doc.graphs[0].nodes.size()==1);
    NX_EXPECT(doc.undo()&&doc.disconnect(0,1,"in",false)&&!doc.canRedo());
    NX_EXPECT(doc.undo()&&doc.graphs[0].links.size()==1);
}
NX_TEST(nativeBlueprintRejectsCyclesAndInvalidLiterals)
{
    BlueprintDocument doc;doc.create();std::string error;
    NX_EXPECT(doc.addNode(0,"add:float",10,10,error)&&doc.addNode(0,"add:float",20,20,error));
    NX_EXPECT(doc.connect(0,1,"result",2,"a",error));
    NX_EXPECT(!doc.connect(0,2,"result",1,"a",error));
    NX_EXPECT(doc.addNode(0,"literal:int",0,0,error));
    NX_EXPECT(!doc.setLiteral(0,3,"1.5",error)&&!doc.setLiteral(0,3,"2147483648",error));
    NX_EXPECT(doc.setLiteral(0,3,"-42",error));
    const char legacy[]="{\"version\":1,\"kind\":\"Screen\"}";
    NX_EXPECT(!doc.load(legacy,strlen(legacy),error)&&doc.graphs[0].nodes.size()==4);
}

NX_TEST(nativeCompilerEmitsTypedAngelScriptAndRejectsIncompleteGraphs)
{
    BlueprintDocument doc;doc.create();std::string error,source="unchanged";
    NX_EXPECT(doc.addNode(0,"api:Save:Save::set:string,string",0,0,error));
    NX_EXPECT(doc.connect(0,0,"then",1,"in",error));
    NX_EXPECT(!compileBlueprint(doc,source,error)&&source=="unchanged"&&error.find("Connect input key")!=std::string::npos);
    NX_EXPECT(doc.addNode(0,"literal:string",0,0,error)&&doc.addNode(0,"literal:string",0,0,error));
    NX_EXPECT(doc.setLiteral(0,2,"message",error)&&doc.setLiteral(0,3,"Hello \"Redux\"",error));
    NX_EXPECT(doc.connect(0,2,"value",1,"key",error)&&doc.connect(0,3,"value",1,"value",error));
    NX_EXPECT(compileBlueprint(doc,source,error));
    NX_EXPECT(source.find("Save::set(\"message\", \"Hello \\\"Redux\\\"\");")!=std::string::npos);
    NX_EXPECT(source.find("class BP_NativeScreen : Screen")!=std::string::npos);
}

NX_TEST(nativeCompilerExecutesActionsOnceAndEnforcesBranchScope)
{
    BlueprintDocument doc;doc.create();std::string error,source;
    NX_EXPECT(!doc.addNode(0,"event:delta",0,0,error));
    NX_EXPECT(doc.addNode(1,"event:delta",0,0,error));
    NX_EXPECT(doc.addNode(0,"api:Scene:Scene::spawn:Vec2",0,0,error));
    NX_EXPECT(doc.addNode(0,"make:Vec2",0,0,error));
    NX_EXPECT(doc.addNode(0,"literal:float",0,0,error));
    NX_EXPECT(doc.addNode(0,"api:Entity:set_spin:Entity@,float",0,0,error));
    NX_EXPECT(doc.setLiteral(0,3,"90",error));
    NX_EXPECT(doc.connect(0,0,"then",1,"in",error));
    NX_EXPECT(doc.connect(0,1,"then",4,"in",error));
    NX_EXPECT(doc.connect(0,2,"result",1,"position",error));
    NX_EXPECT(doc.connect(0,3,"value",2,"x",error)&&doc.connect(0,3,"value",2,"y",error));
    NX_EXPECT(doc.connect(0,1,"result",4,"target",error)&&doc.connect(0,3,"value",4,"value",error));
    NX_EXPECT(compileBlueprint(doc,source,error));
    NX_EXPECT(source.find("Entity@ bp_value_1 = Scene::spawn(Vec2(float(90), float(90)));")!=std::string::npos);
    NX_EXPECT(source.find("(bp_value_1).spin = float(90);")!=std::string::npos);
    NX_EXPECT(doc.disconnect(0,4,"in",false)&&doc.disconnect(0,1,"in",false));
    NX_EXPECT(doc.connect(0,0,"then",4,"in",error));
    NX_EXPECT(!compileBlueprint(doc,source,error)&&error.find("before execution")!=std::string::npos);

    BlueprintDocument branch;branch.create();
    NX_EXPECT(branch.addNode(0,"branch",0,0,error)&&branch.addNode(0,"literal:bool",0,0,error));
    NX_EXPECT(branch.connect(0,0,"then",1,"in",error)&&branch.connect(0,2,"value",1,"condition",error));
    NX_EXPECT(compileBlueprint(branch,source,error)&&source.find("if (false)")!=std::string::npos);
}

NX_TEST(nativeVariablesAreTypedAndSharedAcrossEvents)
{
    BlueprintDocument doc;doc.create();std::string error,source,saved;
    NX_EXPECT(doc.addNode(0,"var:set:float",0,0,error)&&doc.addNode(0,"literal:float",0,0,error));
    NX_EXPECT(doc.addNode(1,"var:get:float",0,0,error)&&doc.addNode(1,"var:set:float",0,0,error));
    NX_EXPECT(doc.setReference(0,1,"speed",error)&&doc.setReference(1,1,"speed",error)&&doc.setReference(1,2,"speed",error));
    NX_EXPECT(doc.setLiteral(0,2,"12.5",error)&&doc.connect(0,0,"then",1,"in",error)&&doc.connect(0,2,"value",1,"value",error));
    NX_EXPECT(doc.connect(1,0,"then",2,"in",error)&&doc.connect(1,1,"value",2,"value",error));
    NX_EXPECT(doc.addNode(2,"var:get:int",0,0,error)&&!doc.setReference(2,1,"speed",error));
    NX_EXPECT(doc.serialize(saved,error));BlueprintDocument copy;NX_EXPECT(copy.load(saved.data(),saved.size(),error));
    NX_EXPECT(compileBlueprint(copy,source,error)&&source.find("float bp_var_speed;")!=std::string::npos&&source.find("bp_var_speed = float(12.5);")!=std::string::npos);
}
NX_TEST(nativeLoopIndexIsScopedAndCountBounded)
{
    BlueprintDocument doc;doc.create();std::string error,source;
    NX_EXPECT(doc.addNode(0,"repeat",0,0,error)&&doc.addNode(0,"literal:int",0,0,error)&&doc.addNode(0,"var:set:int",0,0,error));
    NX_EXPECT(doc.connect(0,0,"then",1,"in",error)&&doc.connect(0,2,"value",1,"count",error));
    NX_EXPECT(doc.connect(0,1,"body",3,"in",error)&&doc.connect(0,1,"index",3,"value",error));
    NX_EXPECT(compileBlueprint(doc,source,error)&&source.find("10000")!=std::string::npos&&source.find("bp_var_value_int = bp_index_1;")!=std::string::npos);
    NX_EXPECT(doc.disconnect(0,3,"in",false)&&doc.connect(0,1,"then",3,"in",error));
    NX_EXPECT(!compileBlueprint(doc,source,error)&&error.find("only available")!=std::string::npos);
}
NX_TEST(nativeReferenceArgumentsRespectOriginalOrder)
{
    BlueprintDocument doc;doc.create();std::string error,source;
    NX_EXPECT(doc.addNode(0,"api:Space:Space::nextRegion::int out,int out,bool out",0,0,error));
    NX_EXPECT(doc.addNode(0,"var:set:int",0,0,error));
    NX_EXPECT(doc.connect(0,0,"then",1,"in",error)&&doc.connect(0,1,"then",2,"in",error)&&doc.connect(0,1,"x",2,"value",error));
    NX_EXPECT(compileBlueprint(doc,source,error)&&source.find("Space::nextRegion(bp_out_1_x, bp_out_1_z, bp_out_1_arrived)")!=std::string::npos);
}

NX_TEST(nativeFunctionGraphsAreReusableAndHistorySafe)
{
    BlueprintDocument doc;doc.create();std::string error,source,saved;
    NX_EXPECT(doc.addFunction("Setup",error)&&doc.addFunction("Reset",error));
    NX_EXPECT(!doc.addFunction("Setup",error)&&!doc.addFunction("update",error));
    NX_EXPECT(doc.addNode(0,"call:function",0,0,error)&&doc.connect(0,0,"then",1,"in",error));
    NX_EXPECT(doc.addNode(6,"call:function",0,0,error)&&doc.connect(6,0,"then",1,"in",error));
    NX_EXPECT(!doc.addNode(7,"call:function",0,0,error)); // Reset -> Setup -> Reset
    NX_EXPECT(!doc.removeFunction(6,error));
    NX_EXPECT(compileBlueprint(doc,source,error)&&source.find("void bp_fn_Setup()")!=std::string::npos&&source.find("bp_fn_Reset();")!=std::string::npos);
    NX_EXPECT(doc.serialize(saved,error));BlueprintDocument copy;NX_EXPECT(copy.load(saved.data(),saved.size(),error));
    NX_EXPECT(doc.addFunction("Unused",error)&&doc.removeFunction(8,error));
    NX_EXPECT(doc.undo()&&doc.graphs.size()==9&&doc.undo()&&doc.graphs.size()==8);
    NX_EXPECT(doc.redo()&&doc.graphs.size()==9&&doc.redo()&&doc.graphs.size()==8);
}

NX_TEST(nativeCompilerUsesReferenceDefaultsAndOutputOrdering)
{
    BlueprintDocument doc;doc.create();std::string error,source;
    NX_EXPECT(doc.addNode(0,"api:Scene:Scene::raycast:Vec2,Vec2,uint:Vec2 out",0,0,error));
    NX_EXPECT(doc.addNode(0,"make:Vec2",0,0,error)&&doc.addNode(0,"literal:float",0,0,error));
    NX_EXPECT(doc.connect(0,0,"then",1,"in",error)&&doc.connect(0,2,"result",1,"from",error)&&doc.connect(0,2,"result",1,"to",error));
    NX_EXPECT(doc.connect(0,3,"value",2,"x",error)&&doc.connect(0,3,"value",2,"y",error));
    NX_EXPECT(compileBlueprint(doc,source,error));
    NX_EXPECT(source.find("bp_out_1_hit, uint(65535)")!=std::string::npos);
}

NX_TEST(nativeBlueprintRecoveryMarksLoadedDraftUnsaved)
{
    BlueprintDocument doc;doc.create();std::string error,source,saved;
    NX_EXPECT(doc.graphs.size()==6);
    NX_EXPECT(doc.addNode(5,"var:set:string",0,0,error)&&doc.addNode(5,"literal:string",0,0,error));
    NX_EXPECT(doc.connect(5,0,"then",1,"in",error)&&doc.connect(5,2,"value",1,"value",error));
    NX_EXPECT(doc.serialize(saved,error));BlueprintDocument recovered;
    NX_EXPECT(recovered.load(saved.data(),saved.size(),error)&&!recovered.dirty());
    recovered.markRecovered();NX_EXPECT(recovered.dirty());
    NX_EXPECT(compileBlueprint(recovered,source,error)&&source.find("void onExit()")!=std::string::npos);
    recovered.markSaved();NX_EXPECT(!recovered.dirty());
}

NX_TEST(nativeBlueprintManagedFileNamesArePortableAndConfined)
{
    std::string file;
    NX_EXPECT(blueprintManagedName("My Game_2",file)&&file=="blueprint-project-My Game_2.bp.json");
    NX_EXPECT(blueprintFileName(file));
    for(const char* bad:{"","../escape","C:\\file","a/b","a.b"," name","name ","a\nb"})
        NX_EXPECT(!blueprintManagedName(bad,file));
    for(const char* bad:{"../a.bp.json","a/b.bp.json","a\\b.bp.json","blueprint-recovery.bp.json","a.json",".hidden.bp.json"})
        NX_EXPECT(!blueprintFileName(bad));
}

NX_TEST(nativeBlueprintScreenExportIsTransactionalAndUsesNativeManifest)
{
    BlueprintDocument doc;doc.create();doc.name="Test \"Game\"";
    std::string source,manifest,error;
    NX_EXPECT(compileBlueprintScreen(doc,source,manifest,error));
    NX_EXPECT(source.find("class BP_NativeScreen : Screen")!=std::string::npos);
    NX_EXPECT(manifest.find("Test \\\"Game\\\"")!=std::string::npos);
    NX_EXPECT(doc.addNode(0,"branch",0,0,error)&&doc.connect(0,0,"then",1,"in",error));
    const auto previousSource=source,previousManifest=manifest;
    NX_EXPECT(!compileBlueprintScreen(doc,source,manifest,error)&&source==previousSource&&manifest==previousManifest);
}

NX_TEST(nativeInputValuesAreTypedUndoableAndWiresTakePrecedence)
{
    BlueprintDocument doc;doc.create();std::string error,source,saved;
    NX_EXPECT(doc.addNode(0,"api:Save:Save::set:string,string",0,0,error)&&doc.connect(0,0,"then",1,"in",error));
    NX_EXPECT(!doc.setInputValue(0,1,"in","true",error)&&!doc.setInputValue(0,1,"absent","x",error));
    NX_EXPECT(doc.setInputValue(0,1,"key","key",error)&&doc.setInputValue(0,1,"value","first",error));
    NX_EXPECT(compileBlueprint(doc,source,error)&&source.find("Save::set(\"key\", \"first\")")!=std::string::npos);
    NX_EXPECT(doc.addNode(0,"literal:string",0,0,error)&&doc.setLiteral(0,2,"wired",error)&&doc.connect(0,2,"value",1,"value",error));
    NX_EXPECT(compileBlueprint(doc,source,error)&&source.find("Save::set(\"key\", \"wired\")")!=std::string::npos);
    NX_EXPECT(doc.serialize(saved,error));BlueprintDocument copy;NX_EXPECT(copy.load(saved.data(),saved.size(),error));
    NX_EXPECT(copy.disconnect(0,1,"value",false)&&compileBlueprint(copy,source,error)&&source.find("\"first\"")!=std::string::npos);
    NX_EXPECT(copy.clearInputValue(0,1,"value")&&!compileBlueprint(copy,source,error));
    NX_EXPECT(copy.undo()&&compileBlueprint(copy,source,error));
    NX_EXPECT(copy.addNode(0,"repeat",0,0,error)&&!copy.setInputValue(0,3,"count","1.5",error)&&copy.setInputValue(0,3,"count","5",error));
}

NX_TEST(nativeNodeEditorAppliesAllValuesAsOneTransaction)
{
    BlueprintDocument doc;doc.create();std::string error,source;
    NX_EXPECT(doc.addNode(0,"api:Save:Save::set:string,string",0,0,error)&&doc.connect(0,0,"then",1,"in",error));
    NX_EXPECT(doc.setInputValue(0,1,"key","original",error)&&doc.setInputValue(0,1,"value","old",error));
    const auto revision=doc.revision();
    NX_EXPECT(!doc.editNodeValues(0,1,{{"key","discard"},{"unknown","invalid"}},nullptr,error)&&doc.revision()==revision);
    NX_EXPECT(compileBlueprint(doc,source,error)&&source.find("Save::set(\"original\", \"old\")")!=std::string::npos);
    NX_EXPECT(doc.editNodeValues(0,1,{{"key","changed"},{"value","new"}},nullptr,error));
    NX_EXPECT(compileBlueprint(doc,source,error)&&source.find("Save::set(\"changed\", \"new\")")!=std::string::npos);
    NX_EXPECT(doc.undo()&&doc.revision()==revision&&compileBlueprint(doc,source,error)&&source.find("Save::set(\"original\", \"old\")")!=std::string::npos);
    NX_EXPECT(doc.addNode(0,"literal:float",0,0,error));const std::string invalid="bad",valid="2.5";
    NX_EXPECT(!doc.editNodeValues(0,2,{},&invalid,error));
    NX_EXPECT(doc.editNodeValues(0,2,{},&valid,error)&&doc.graphs[0].nodes[2].ref=="2.5");
}

NX_TEST(nativeNodeAnnotationsAndDisconnectRoundTrip)
{
    BlueprintDocument doc;doc.create();std::string error,saved;
    NX_EXPECT(doc.addNode(0,"api:Save:Save::set:string,string",0,0,error)&&doc.addNode(1,"api:Save:Save::set:string,string",0,0,error));
    NX_EXPECT(doc.connect(0,0,"then",1,"in",error));
    const std::string alias="Save checkpoint",comment="Writes checkpoint data";const unsigned colour=0x1A1672Cu;
    NX_EXPECT(doc.editNodeValues(0,1,{},nullptr,error,&alias,&comment,&colour));
    NX_EXPECT(doc.graphs[0].nodes[1].displayName==alias&&doc.graphs[1].nodes[1].displayName==alias);
    NX_EXPECT(doc.graphs[0].nodes[1].comment==comment&&doc.graphs[1].nodes[1].comment.empty());
    NX_EXPECT(doc.addNode(2,"api:Save:Save::set:string,string",0,0,error)&&doc.graphs[2].nodes[1].displayName==alias);
    NX_EXPECT(doc.serialize(saved,error));BlueprintDocument copy;NX_EXPECT(copy.load(saved.data(),saved.size(),error));
    NX_EXPECT(copy.graphs[0].nodes[1].colour==colour&&copy.graphs[0].nodes[1].comment==comment&&copy.graphs[1].nodes[1].displayName==alias);
    NX_EXPECT(copy.disconnectNode(0,1)&&copy.graphs[0].links.empty()&&copy.undo()&&copy.graphs[0].links.size()==1);
    const std::string reset;NX_EXPECT(copy.editNodeValues(0,1,{},nullptr,error,&reset));
    NX_EXPECT(copy.nodeDisplayName(1,copy.graphs[1].nodes[1]).empty()&&copy.undo()&&copy.nodeDisplayName(1,copy.graphs[1].nodes[1])==alias);
}

NX_TEST(nativeReusableCameraSequenceSceneReferences)
{
    BlueprintDocument doc;doc.create();std::string error,id,source,bytes;
    NX_EXPECT(doc.createVisualAsset("sequence",id,error)&&doc.addScreen("arena",error));
    size_t graph=0,node=0;NX_EXPECT(doc.placeCameraSequence("Screen",id,graph,node,error)&&graph==1&&node==1);
    NX_EXPECT(doc.placeCameraSequence("arena",id,graph,node,error)&&doc.graphs[graph].owner=="arena");
    std::vector<BlueprintCameraKey> keys;NX_EXPECT(doc.cameraSequenceKeys(id,keys,error));keys.back().fov=33;
    NX_EXPECT(doc.setCameraSequenceKeys(id,keys,error)&&doc.renameVisualAsset(id,"Boss reveal",error));
    std::vector<BlueprintScreenOutput> output;NX_EXPECT(compileBlueprintProject(doc,output,error)&&output.size()==2);
    for(const auto& item:output)NX_EXPECT(item.source.find("float(33)")!=std::string::npos);
    NX_EXPECT(!doc.removeVisualAsset(id,error));
    NX_EXPECT(doc.serialize(bytes,error));BlueprintDocument copy;NX_EXPECT(copy.load(bytes.data(),bytes.size(),error));
    NX_EXPECT(copy.cameraSequenceKeys(id,keys,error)&&keys.back().fov==33);
    std::string promoted;NX_EXPECT(copy.promoteCameraSequence(1,1,"Alternate",keys,promoted,error)&&promoted!=id);
    NX_EXPECT(copy.undo()&&copy.visualAsset(promoted)==nullptr&&copy.visualAsset(id)!=nullptr);
}

NX_TEST(nativeCameraSequenceKeysAndCompilation)
{
    std::string error,source;std::vector<BlueprintCameraKey> keys;
    const std::string encoded="0,0,2,8,0,1,0,60,0;2,4,2,8,0,1,0,40,0;4,0,2,4,0,1,0,60,1";
    NX_EXPECT(blueprintCameraKeys(encoded,keys,error));
    NX_EXPECT(blueprintCameraSample(keys,1).from[0]==2&&blueprintCameraSample(keys,1).fov==50);
    NX_EXPECT(blueprintCameraSample(keys,3).from[0]==4&&blueprintCameraSample(keys,4).from[0]==0);
    NX_EXPECT(blueprintCameraKeyText(keys)==encoded);
    NX_EXPECT(!blueprintCameraKeys("0,0,0,0,0,0,0,60,0",keys,error));
    BlueprintDocument doc;doc.create();
    const std::string eased="0,0,2,8,0,1,0,60,0;2,4,2,8,0,1,0,40,2@Reveal";
    NX_EXPECT(blueprintCameraKeys(eased,keys,error)&&blueprintCameraSample(keys,0.5f).from[0]==0.625f&&blueprintCameraKeyText(keys)==eased);
    NX_EXPECT(!doc.addNode(0,"camera:sequence",0,0,error));
    NX_EXPECT(doc.addNode(1,"camera:sequence",300,60,error)&&doc.connect(1,0,"then",1,"in",error));
    NX_EXPECT(doc.setInputValue(1,1,"keys",eased,error)&&doc.setInputValue(1,1,"play","true",error)&&doc.setInputValue(1,1,"anchor","Boss",error));
    NX_EXPECT(!compileBlueprint(doc,source,error)&&error.find("Reveal")!=std::string::npos);
    NX_EXPECT(doc.addFunction("Reveal",error));
    NX_EXPECT(compileBlueprint(doc,source,error)&&source.find("bp_seq_state_1_1_running")!=std::string::npos&&source.find("Space::camera(")!=std::string::npos);
    std::string bytes;NX_EXPECT(doc.serialize(bytes,error));BlueprintDocument copy;NX_EXPECT(copy.load(bytes.data(),bytes.size(),error)&&compileBlueprint(copy,source,error));
}

NX_TEST(nativeMoveConnectionIsTransactional)
{
    BlueprintDocument doc;doc.create();std::string error;
    NX_EXPECT(doc.addNode(0,"api:Save:Save::set:string,string",0,0,error)&&doc.addNode(0,"api:Save:Save::set:string,string",0,0,error));
    NX_EXPECT(doc.connect(0,0,"then",1,"in",error));
    NX_EXPECT(!doc.moveConnection(0,0,2,"invalid",false,error)&&doc.graphs[0].links[0].to==1);
    NX_EXPECT(doc.moveConnection(0,0,2,"in",false,error)&&doc.graphs[0].links[0].to==2);
    NX_EXPECT(doc.undo()&&doc.graphs[0].links[0].to==1);
}

NX_TEST(nativeResizableCommentAndRGBRoundTrip)
{
    BlueprintDocument doc;doc.create();std::string error,bytes;
    unsigned colour=0,black=0;NX_EXPECT(blueprintParseRGB("#a1b2c3",colour)&&blueprintRGBHex(colour)=="A1B2C3");
    NX_EXPECT(blueprintParseRGB("000000",black)&&black!=0&&blueprintRGBHex(black)=="000000");
    NX_EXPECT(!blueprintParseRGB("#XYZ123",colour));
    std::vector<std::string> components;NX_EXPECT(blueprintInputComponents("Color","#FF8000",components)&&components.size()==4&&components[0]=="1.000000"&&components[3]=="1");
    NX_EXPECT(doc.addComment(0,{0},"Group",error)&&doc.resizeComment(0,0,-32,-16,600,300));
    NX_EXPECT(doc.editComment(0,0,"Group",colour,error)&&doc.serialize(bytes,error));
    BlueprintDocument copy;NX_EXPECT(copy.load(bytes.data(),bytes.size(),error));
    const auto& item=copy.graphs[0].comments[0];NX_EXPECT(item.width==600&&item.height==300&&item.x==-32&&item.colour==colour);
    NX_EXPECT(copy.resizeComment(0,0,0,0,800,450)&&copy.undo()&&copy.graphs[0].comments[0].width==600);
}

NX_TEST(nativeCompilerDiagnosticsAndSourceMapLocateExactGraphNodes)
{
    BlueprintDocument doc;doc.create();std::string error,source="unchanged";
    BlueprintLocation location;BlueprintSourceMap map;
    NX_EXPECT(doc.addNode(0,"branch",0,0,error)&&doc.connect(0,0,"then",1,"in",error));
    NX_EXPECT(!compileBlueprint(doc,source,error,&location,&map)&&source=="unchanged"&&map.empty());
    NX_EXPECT(location.graph==0&&location.node==1);
    NX_EXPECT(doc.setInputValue(0,1,"condition","true",error));
    NX_EXPECT(doc.addNode(0,"api:Save:Save::set:string,string",0,0,error)&&doc.connect(0,1,"true",2,"in",error));
    NX_EXPECT(doc.setInputValue(0,2,"key","hello",error)&&doc.setInputValue(0,2,"value","world",error));
    NX_EXPECT(compileBlueprint(doc,source,error,&location,&map));
    const auto pos=source.find("Save::set(");
    if(pos==std::string::npos){NX_EXPECT(false);return;}
    const size_t line=1+static_cast<size_t>(std::count(source.begin(),source.begin()+static_cast<std::ptrdiff_t>(pos),'\n'));
    const auto result=blueprintSourceLocation(map,line);
    NX_EXPECT(result.graph==0&&result.node==2);
    NX_EXPECT(blueprintErrorLine("Preview error: __blueprint_preview/main.as:"+std::to_string(line)+": Null pointer","__blueprint_preview/main.as")==line);
    NX_EXPECT(blueprintErrorLine("unrelated.as:7: error","blueprint.as")==0);
    NX_EXPECT(blueprintSourceLocation(map,999999).node==static_cast<size_t>(-1));
    const auto old=source;const auto count=map.size();
    NX_EXPECT(doc.clearInputValue(0,2,"key")&&!compileBlueprint(doc,source,error,&location,&map));
    NX_EXPECT(source==old&&map.size()==count&&location.node==2);
}

NX_TEST(nativeBuiltinOperatorsHaveConsistentTypedPinsAndCompile)
{
    std::string error,source;
    size_t operators=0;
    for(const auto& spec:blueprintCatalogue())
    {
        if(spec.operation.empty())continue;
        ++operators;
        NX_EXPECT(spec.pure&&spec.outputs.size()==1&&spec.outputs[0].type==spec.result);
        BlueprintDocument doc;doc.create();
        const auto setter="var:set:"+spec.result;
        bool authored=doc.addNode(0,spec.id,0,0,error)&&doc.addNode(0,setter,0,0,error)&&
            doc.connect(0,0,"then",2,"in",error)&&doc.connect(0,1,"result",2,"value",error);
        for(const auto& p:spec.inputs)
            authored=authored&&doc.setInputValue(0,1,p.name,p.type=="string"?"hello":p.type=="bool"?"true":"2",error);
        NX_EXPECT(authored&&compileBlueprint(doc,source,error));
    }
    NX_EXPECT(operators==106);
}

NX_TEST(nativeProjectScreensOwnTheirStateAndFunctions)
{
    BlueprintDocument doc;doc.create();std::string error,saved;
    NX_EXPECT(doc.addScreen("level",error)&&!doc.addScreen("level",error)&&!doc.addScreen("../bad",error));
    NX_EXPECT(doc.graphs.size()==12&&doc.isProject());
    NX_EXPECT(doc.addFunction("Setup",error)&&doc.addFunction("Setup",error,"level"));
    NX_EXPECT(doc.addNode(0,"var:set:int",0,0,error)&&doc.setReference(0,1,"score",error));
    NX_EXPECT(doc.addNode(6,"var:set:string",0,0,error)&&doc.setReference(6,1,"score",error));
    NX_EXPECT(doc.addNode(0,"call:function",0,0,error)&&doc.addNode(6,"call:function",0,0,error));
    NX_EXPECT(doc.connect(0,0,"then",1,"in",error)&&doc.setInputValue(0,1,"value","5",error));
    NX_EXPECT(doc.connect(6,0,"then",1,"in",error)&&doc.setInputValue(6,1,"value","level",error));
    NX_EXPECT(doc.serialize(saved,error));BlueprintDocument copy;NX_EXPECT(copy.load(saved.data(),saved.size(),error));
    std::vector<BlueprintScreenOutput> output;
    NX_EXPECT(compileBlueprintProject(copy,output,error)&&output.size()==2);
    if(output.size()==2)
    {
        NX_EXPECT(output[0].source.find("int bp_var_score;")!=std::string::npos);
        NX_EXPECT(output[1].source.find("string bp_var_score;")!=std::string::npos);
        NX_EXPECT(output[1].id=="level"&&output[1].map.front().location.graph==6);
    }
    NX_EXPECT(copy.removeScreen("level",error)&&!copy.isProject()&&copy.undo()&&copy.isProject());
}

NX_TEST(nativeProjectNavigationRejectsMissingConstantTargets)
{
    BlueprintDocument doc;doc.create();std::string error;BlueprintLocation location;
    std::vector<BlueprintScreenOutput> out;
    NX_EXPECT(doc.addScreen("level",error));
    NX_EXPECT(doc.addNode(0,"api:Screens:Screens::push:string",0,0,error)&&doc.connect(0,0,"then",1,"in",error));
    NX_EXPECT(doc.setInputValue(0,1,"id","unknown",error)&&!compileBlueprintProject(doc,out,error,&location));
    NX_EXPECT(location.graph==0&&location.node==1&&out.empty());
    NX_EXPECT(doc.setInputValue(0,1,"id","level",error)&&compileBlueprintProject(doc,out,error));
    NX_EXPECT(!doc.removeScreen("level",error));
    NX_EXPECT(doc.clearInputValue(0,1,"id")&&doc.addNode(0,"literal:string",0,0,error)&&doc.setLiteral(0,2,"level",error)&&doc.connect(0,2,"value",1,"id",error));
    NX_EXPECT(!doc.removeScreen("level",error)&&compileBlueprintProject(doc,out,error));
    NX_EXPECT(doc.setLiteral(0,2,"unknown",error)&&!compileBlueprintProject(doc,out,error,&location)&&out.size()==2);
}

NX_TEST(nativeBehaviourGraphsAreScopedAndAttachmentsPersist)
{
    BlueprintDocument doc;doc.create();std::string error,source,saved;
    NX_EXPECT(doc.addBehaviour("Spinner",error)&&!doc.addBehaviour("Spinner",error)&&!doc.addBehaviour("bad name",error));
    NX_EXPECT(doc.screens().size()==1&&doc.behaviours().size()==1);
    NX_EXPECT(!doc.addNode(0,"event:entity",0,0,error)&&!doc.addNode(6,"event:other",0,0,error));
    NX_EXPECT(doc.addNode(8,"event:other",0,0,error)&&doc.addNode(6,"event:entity",0,0,error));
    NX_EXPECT(!doc.addNode(6,"attach:behaviour",0,0,error)&&doc.addNode(0,"attach:behaviour",0,0,error));
    NX_EXPECT(!doc.setReference(0,1,"Missing",error)&&!doc.removeBehaviour("Spinner",error));
    NX_EXPECT(doc.serialize(saved,error));BlueprintDocument copy;NX_EXPECT(copy.load(saved.data(),saved.size(),error));
    NX_EXPECT(compileBlueprint(copy,source,error)&&source.find("class BP_Behaviour_Spinner : Behaviour")!=std::string::npos);
    NX_EXPECT(source.find("void onHit(Entity@ other)")!=std::string::npos);
    NX_EXPECT(copy.deleteNode(0,1,error)&&copy.removeBehaviour("Spinner",error)&&copy.undo()&&copy.behaviours().size()==1);
}

NX_TEST(nativeFunctionSignaturesResolveForwardReferencesAndProtectWires)
{
    BlueprintDocument doc;doc.create();std::string error,source,saved;
    NX_EXPECT(doc.addFunction("Add",error)&&doc.editFunctionSignature(6,"int(a:int, b:int)",error));
    NX_EXPECT(doc.addNode(6,"add:int",0,0,error)&&doc.addNode(6,"return:function",0,0,error));
    NX_EXPECT(doc.connect(6,0,"arg_a",1,"a",error)&&doc.connect(6,0,"arg_b",1,"b",error));
    NX_EXPECT(doc.connect(6,0,"then",2,"in",error)&&doc.connect(6,1,"result",2,"value",error));
    NX_EXPECT(doc.addNode(0,"call:function",0,0,error)&&doc.connect(0,0,"then",1,"in",error));
    NX_EXPECT(doc.setInputValue(0,1,"arg_a","7",error)&&doc.setInputValue(0,1,"arg_b","8",error));
    NX_EXPECT(doc.addNode(0,"var:set:int",0,0,error)&&doc.connect(0,1,"then",2,"in",error)&&doc.connect(0,1,"result",2,"value",error));
    NX_EXPECT(!doc.editFunctionSignature(6,"float(a:float)",error));
    NX_EXPECT(doc.functionSignature(6)=="int(a:int, b:int)");
    NX_EXPECT(doc.serialize(saved,error));BlueprintDocument copy;NX_EXPECT(copy.load(saved.data(),saved.size(),error));
    NX_EXPECT(compileBlueprint(copy,source,error)&&source.find("int bp_fn_Add(int bp_arg_a, int bp_arg_b)")!=std::string::npos);
    NX_EXPECT(source.find("bp_fn_Add(int(7), int(8))")!=std::string::npos);
    NX_EXPECT(!copy.addNode(0,"return:function",0,0,error));
    NX_EXPECT(copy.disconnect(6,2,"in",false)&&!compileBlueprint(copy,source,error));
    NX_EXPECT(error.find("returns a value")!=std::string::npos);
}

NX_TEST(nativeFunctionBranchContinuationRequiresReturnsOnAllPaths)
{
    BlueprintDocument doc;doc.create();std::string error,source;BlueprintLocation location;
    NX_EXPECT(doc.addFunction("Choose",error)&&doc.editFunctionSignature(6,"int(flag:bool)",error));
    NX_EXPECT(doc.addNode(6,"branch",0,0,error)&&doc.addNode(6,"return:function",0,0,error)&&doc.addNode(6,"return:function",0,0,error));
    NX_EXPECT(doc.connect(6,0,"then",1,"in",error)&&doc.connect(6,0,"arg_flag",1,"condition",error));
    NX_EXPECT(doc.connect(6,1,"true",2,"in",error)&&doc.setInputValue(6,2,"value","0007",error));
    NX_EXPECT(!compileBlueprint(doc,source,error,&location)&&location.graph==6&&location.node==0);
    NX_EXPECT(doc.connect(6,1,"then",3,"in",error)&&doc.setInputValue(6,3,"value","-0008",error));
    NX_EXPECT(compileBlueprint(doc,source,error)&&source.find("return int(7);")!=std::string::npos&&source.find("return int(-8);")!=std::string::npos);
    NX_EXPECT(doc.addNode(6,"return:function",0,0,error)&&doc.setInputValue(6,4,"value","9",error));
    NX_EXPECT(doc.connect(6,1,"false",4,"in",error)&&!compileBlueprint(doc,source,error,&location));
    NX_EXPECT(location.graph==6&&location.node==1&&error.find("unreachable")!=std::string::npos);
    NX_EXPECT(doc.undo()&&compileBlueprint(doc,source,error));
}

NX_TEST(nativeAssetRootsAndNamesAreConfined)
{
    BlueprintDocument doc;doc.create();std::string error,saved;
    NX_EXPECT(blueprintAssetRoot("bpa-12345-1")&&!blueprintAssetRoot("bpa-../bad")&&!blueprintAssetRoot("own/bpa-1"));
    NX_EXPECT(blueprintAssetName("level.json")&&blueprintAssetName("textures",true));
    NX_EXPECT(!doc.addScreen("con",error)&&!doc.addScreen("lpt1",error));
    for(const char* name:{"../level.json","a/b.png","a\\b.png","con.json","file.new","bad.","A.png"})
        NX_EXPECT(!blueprintAssetName(name));
    doc.assetRoot="bpa-12345-1";NX_EXPECT(doc.serialize(saved,error));
    BlueprintDocument copy;NX_EXPECT(copy.load(saved.data(),saved.size(),error)&&copy.assetRoot==doc.assetRoot);
    doc.assetRoot="../external";NX_EXPECT(!doc.serialize(saved,error));
}

NX_TEST(nativeLevelNodesGenerateTheOriginalSceneHelper)
{
    BlueprintDocument doc;doc.create();std::string source,error;
    NX_EXPECT(doc.addNode(0,"level:load",0,0,error)&&doc.setInputValue(0,1,"file","asset:scene.json",error)&&doc.connect(0,0,"then",1,"in",error));
    NX_EXPECT(doc.addNode(1,"level:update",0,0,error)&&doc.addNode(1,"event:delta",0,0,error)&&doc.connect(1,0,"then",1,"in",error)&&doc.connect(1,2,"value",1,"dt",error));
    NX_EXPECT(doc.addNode(2,"level:drawHud",0,0,error)&&doc.addNode(2,"event:canvas",0,0,error)&&doc.connect(2,0,"then",1,"in",error)&&doc.connect(2,2,"value",1,"canvas",error));
    NX_EXPECT(doc.addBehaviour("Actor",error)&&!doc.addNode(6,"level:load",0,0,error));
    NX_EXPECT(compileBlueprint(doc,source,error));
    NX_EXPECT(source.rfind("#include \"global:scene.as\"",0)==0&&source.find("Level bp_level;")!=std::string::npos);
    NX_EXPECT(source.find("bp_level.load(\"asset:scene.json\")")!=std::string::npos);
    NX_EXPECT(source.find("bp_level.update(dt, null, false, Vec3())")!=std::string::npos);
    NX_EXPECT(source.find("bp_level.drawHud(canvas)")!=std::string::npos);
}

NX_TEST(nativeLevelEventsAndNamedAttachmentsValidateAndPersist)
{
    BlueprintDocument doc;doc.create();std::string error,source,saved;
    NX_EXPECT(doc.addLevelEvents("Screen",error)&&!doc.addLevelEvents("Screen",error));
    NX_EXPECT(doc.graphs.size()==8&&doc.graphs[6].name=="onLevelTouch");
    NX_EXPECT(doc.addNode(6,"event:levelName",0,0,error)&&doc.addNode(6,"event:levelWho",0,0,error));
    NX_EXPECT(!doc.addNode(6,"event:levelWhat",0,0,error)&&!doc.addNode(0,"event:levelName",0,0,error));
    NX_EXPECT(doc.addBehaviour("Door",error)&&doc.addNode(0,"level:attach",0,0,error));
    NX_EXPECT(doc.setInputValue(0,1,"name","door",error)&&doc.connect(0,0,"then",1,"in",error));
    NX_EXPECT(!doc.removeBehaviour("Door",error));
    NX_EXPECT(doc.serialize(saved,error));BlueprintDocument copy;NX_EXPECT(copy.load(saved.data(),saved.size(),error));
    NX_EXPECT(compileBlueprint(copy,source,error)&&source.find("LevelTouch(this.onLevelTouch)")!=std::string::npos);
    NX_EXPECT(source.find("LevelAgent(this.onLevelAgent)")!=std::string::npos&&source.find("bp_level.find(\"door\")")!=std::string::npos);
    NX_EXPECT(copy.addFunction("Reload",error)&&copy.addNode(13,"level:clear",0,0,error)&&copy.connect(13,0,"then",1,"in",error));
    NX_EXPECT(copy.addNode(6,"call:function",0,0,error)&&copy.connect(6,0,"then",3,"in",error));
    BlueprintLocation location;
    NX_EXPECT(!compileBlueprint(copy,source,error,&location)&&location.graph==13&&location.node==1);
}

NX_TEST(nativeBehaviourAttachmentChecksExecutionScopeAndEntityLifetime)
{
    BlueprintDocument doc;doc.create();std::string error,source;
    NX_EXPECT(doc.addBehaviour("Actor",error)&&doc.addNode(0,"var:get:Entity@",0,0,error));
    NX_EXPECT(doc.addNode(0,"attach:behaviour",0,0,error)&&doc.connect(0,1,"value",2,"target",error));
    NX_EXPECT(doc.addNode(0,"branch",0,0,error)&&doc.connect(0,2,"result",3,"condition",error));
    NX_EXPECT(doc.connect(0,0,"then",3,"in",error)&&!compileBlueprint(doc,source,error));
    NX_EXPECT(doc.disconnect(0,3,"in",false)&&doc.connect(0,0,"then",2,"in",error)&&doc.connect(0,2,"then",3,"in",error));
    NX_EXPECT(compileBlueprint(doc,source,error)&&source.find("bp_attach_2 !is null && bp_attach_2.alive")!=std::string::npos);
}
NX_TEST(nativeAgentCallbacksRejectUnsafeVisibilityMutation)
{
    BlueprintDocument doc;doc.create();std::string error,source;BlueprintLocation location;
    NX_EXPECT(doc.addLevelEvents("Screen",error)&&doc.addNode(7,"level:show",0,0,error)&&doc.connect(7,0,"then",1,"in",error));
    NX_EXPECT(!compileBlueprint(doc,source,error,&location)&&location.graph==7&&location.node==1);
}

NX_TEST(nativeTraceWatchesAreUndoableAndDoNotChangeGeneratedSource)
{
    BlueprintDocument doc;doc.create();std::string error,source,after,saved;
    NX_EXPECT(doc.addNode(0,"literal:int",0,0,error)&&!doc.toggleWatch(0,1));
    NX_EXPECT(compileBlueprint(doc,source,error)&&doc.toggleWatch(0,0)&&doc.graphs[0].nodes[0].watch);
    NX_EXPECT(compileBlueprint(doc,after,error)&&source==after);
    NX_EXPECT(doc.serialize(saved,error));BlueprintDocument copy;NX_EXPECT(copy.load(saved.data(),saved.size(),error)&&copy.graphs[0].nodes[0].watch);
    NX_EXPECT(doc.undo()&&!doc.graphs[0].nodes[0].watch&&doc.redo()&&doc.graphs[0].nodes[0].watch);
}

NX_TEST(nativeSourceImportCreatesEditableTypedGraphsAndRejectsLoss)
{
    BlueprintDocument doc;doc.create();std::string error,generated,saved;
    const std::string source=R"(// imported original
        class Example : Screen {
            void onEnter() {
                score = 2;
                Save::set("status", "ready");
            }
            int score;
            void update(float elapsed) {
                if (score < 4 && elapsed > 0.0f) {
                    score = score + 1;
                } else {
                    Save::set("status", "done");
                }
                Save::set("after", "yes");
            }
        })";
    const bool imported=importBlueprintScreen(source,doc,error);
    if(!imported)NX_ERROR("Import diagnostic: %s",error.c_str());
    NX_EXPECT(imported);
    NX_EXPECT(doc.name=="Example"&&doc.dirty()&&doc.graphs[0].nodes.size()>1&&doc.graphs[1].nodes.size()>1);
    NX_EXPECT(compileBlueprint(doc,generated,error)&&generated.find("bp_var_score")!=std::string::npos&&generated.find("if (")!=std::string::npos);
    NX_EXPECT(doc.serialize(saved,error));
    for(const char* unsupported:{
        "class Bad : Screen { void onEnter() { int local = 1; local = true; } }",
        "class Bad : Screen { int score = 1 + 2; }",
        "class Bad : Screen { void onEnter() { Save::set(\"a\", 5); } }",
        "class Bad : Screen { void onEnter() { while(true) {} } }",
        "#include \"global:scene.as\"\nclass Bad : Screen {}",
        "class Bad : Screen { void onEnter() { Save::set(\"a\", \"\\q\"); } }",
        "class Bad : Screen {} class Other {}"})
    {
        NX_EXPECT(!importBlueprintScreen(unsupported,doc,error)&&!error.empty());
        std::string after;NX_EXPECT(doc.serialize(after,error)&&after==saved);
    }
}

NX_TEST(nativeSourceImportSupportsTypedForwardMethodsAndEarlyReturns)
{
    BlueprintDocument doc;std::string error,source;
    const char* code=R"(class Example : Screen {
        int score;
        void onEnter() { score = Pick(true, 7); Record(score); }
        void Record(int amount) { if(amount == 7) { Save::set("picked","yes"); } }
        int Pick(bool flag, int amount) { if(flag) { return amount; } return 8; }
    })";
    const bool imported=importBlueprintScreen(code,doc,error);
    if(!imported)NX_ERROR("Import diagnostic: %s",error.c_str());
    NX_EXPECT(imported);
    NX_EXPECT(doc.function("Screen","Pick")&&doc.function("Screen","Record"));
    NX_EXPECT(compileBlueprint(doc,source,error)&&source.find("bp_fn_Pick(true, int(7))")!=std::string::npos);
    NX_EXPECT(source.find("int bp_fn_Pick(bool bp_arg_flag, int bp_arg_amount)")!=std::string::npos);
    const auto good=source;
    NX_EXPECT(!importBlueprintScreen("class Bad : Screen { int BadCall() { return BadCall(); } }",doc,error));
    NX_EXPECT(!importBlueprintScreen("class Bad : Screen { int score; void onEnter() { score = F() + 1; } int F(){return 3;} }",doc,error));
    NX_EXPECT(compileBlueprint(doc,source,error)&&source==good);
}

NX_TEST(nativeCaptureValuesRespectExecutionOrderAndBranchScope)
{
    BlueprintDocument doc;doc.create();std::string error,code,saved;
    NX_EXPECT(doc.addNode(0,"capture:int",0,0,error)&&doc.setReference(0,1,"original",error));
    NX_EXPECT(doc.setInputValue(0,1,"value","7",error)&&doc.connect(0,0,"then",1,"in",error));
    NX_EXPECT(doc.addNode(0,"var:set:int",0,0,error)&&doc.connect(0,1,"result",2,"value",error)&&doc.connect(0,1,"then",2,"in",error));
    NX_EXPECT(compileBlueprint(doc,code,error)&&code.find("int bp_local_original_1 = int(7);")!=std::string::npos);
    NX_EXPECT(doc.serialize(saved,error));BlueprintDocument copy;NX_EXPECT(copy.load(saved.data(),saved.size(),error)&&copy.graphs[0].nodes[1].ref=="original");
    NX_EXPECT(copy.disconnect(0,2,"in",false)&&copy.disconnect(0,1,"in",false)&&copy.connect(0,0,"then",2,"in",error));
    NX_EXPECT(!compileBlueprint(copy,code,error)&&error.find("Captured value used before execution")!=std::string::npos);
}
NX_TEST(nativeSourceImportCapturesLocalsOnceAndRejectsEscapingScope)
{
    BlueprintDocument doc;std::string error,code;
    const std::string source=R"(class Example : Screen {
        int score;
        void onEnter() {
            score = 3;
            int initial = score;
            score = 8;
            string label = Save::get("original", "old");
            Save::set("original", "new");
            if(initial == 3) { Save::set("captured", label); }
        }
    })";
    const bool imported=importBlueprintScreen(source,doc,error);
    if(!imported)NX_ERROR("Import diagnostic: %s",error.c_str());
    NX_EXPECT(imported&&compileBlueprint(doc,code,error));
    NX_EXPECT(code.find("int bp_local_initial_")!=std::string::npos&&code.find("string bp_local_label_")!=std::string::npos);
    NX_EXPECT(!importBlueprintScreen("class Bad : Screen { void onEnter() { if(true) { string x = \"a\"; } Save::set(\"x\",x); } }",doc,error));
    NX_EXPECT(!importBlueprintScreen("class Bad : Screen { void onEnter() { int x = 1; x = true; } }",doc,error));
}

NX_TEST(nativeMutableLocalAssignmentsPreserveBranchScope)
{
    BlueprintDocument doc;std::string error,source,saved;
    const auto* original=R"(class Example : Screen {
        int score;
        void onEnter() {
            int local = 1;
            if(true) { local += 2; }
            else { local = 9; }
            local++;
            score = local;
        }
    })";
    const bool imported=importBlueprintScreen(original,doc,error);
    if(!imported)NX_ERROR("Import diagnostic: %s",error.c_str());
    NX_EXPECT(imported&&compileBlueprint(doc,source,error));
    NX_EXPECT(source.find("int bp_local_local_")!=std::string::npos&&source.find("bp_local_local_1 = ")!=std::string::npos);
    NX_EXPECT(doc.serialize(saved,error));BlueprintDocument copy;
    NX_EXPECT(copy.load(saved.data(),saved.size(),error)&&compileBlueprint(copy,source,error));
    NX_EXPECT(!importBlueprintScreen("class Bad : Screen { void onEnter() { if(true) { int x = 1; } x = 2; } }",doc,error));
}
NX_TEST(nativeMutableLocalReferencesAreTypedAndRequireExecutedDeclaration)
{
    BlueprintDocument doc;doc.create();std::string error,code;
    NX_EXPECT(doc.addNode(0,"capture:int",0,0,error)&&doc.setInputValue(0,1,"value","1",error));
    NX_EXPECT(doc.addNode(0,"local:set:int",0,0,error)&&doc.setInputValue(0,2,"value","2",error));
    NX_EXPECT(!doc.connect(0,1,"result",2,"local",error)&&!doc.setInputValue(0,2,"local","1",error));
    NX_EXPECT(doc.connect(0,1,"local",2,"local",error)&&doc.connect(0,0,"then",2,"in",error));
    NX_EXPECT(!compileBlueprint(doc,code,error)&&error.find("before declaration")!=std::string::npos);
    NX_EXPECT(doc.disconnect(0,2,"in",false)&&doc.connect(0,0,"then",1,"in",error)&&doc.connect(0,1,"then",2,"in",error));
    NX_EXPECT(compileBlueprint(doc,code,error)&&code.find("bp_local_captured_1 = int(2);")!=std::string::npos);
}

NX_TEST(nativeSourceImportBoundedLoopsPreserveNestedLocalScope)
{
    BlueprintDocument doc;std::string error,code,saved;
    const auto* original=R"(class Example : Screen {
        int score;
        void onEnter() {
            int sum = 0;
            for(int i=0; i<3; ++i) {
                for(int j=0; j<2; j++) { sum += i + j; }
            }
            score = sum;
        }
    })";
    const bool imported=importBlueprintScreen(original,doc,error);
    if(!imported)NX_ERROR("Import diagnostic: %s",error.c_str());
    NX_EXPECT(imported&&compileBlueprint(doc,code,error));
    NX_EXPECT(code.find("for (int bp_index_")!=std::string::npos);
    NX_EXPECT(doc.serialize(saved,error));
    for(const char* invalid:{
        "class Bad : Screen { void onEnter(){ for(int i=0;i<10001;++i) {} } }",
        "class Bad : Screen { void onEnter(){ for(int i=1;i<3;++i) {} } }",
        "class Bad : Screen { void onEnter(){ for(int i=0;i<3;++i) { i++; } } }",
        "class Bad : Screen { int score; void onEnter(){ for(int i=0;i<3;++i) {} score=i; } }",
        "class Bad : Screen { int limit; void onEnter(){ for(int i=0;i<limit;++i) {} } }",
        "class Bad : Screen { void onEnter(){ for(int i=0;i<3;++i) { break; } } }"})
    {
        NX_EXPECT(!importBlueprintScreen(invalid,doc,error)&&!error.empty());
        std::string after;NX_EXPECT(doc.serialize(after,error)&&after==saved);
    }
}

NX_TEST(nativeImportElseIfRejectsIncompleteAndOversizedChainsTransactionally)
{
    BlueprintDocument doc;doc.create();std::string error,saved,after;
    NX_EXPECT(doc.serialize(saved,error));
    const std::string incomplete="class Bad : Screen { int F(int n) { if(n == 0) { return 1; } else if(n == 1) { return 2; } } }";
    NX_EXPECT(!importBlueprintScreen(incomplete,doc,error)&&!error.empty());
    NX_EXPECT(doc.serialize(after,error)&&after==saved);
    std::string deep="class Bad : Screen { void onEnter() { if(true) {}";
    for(int i=0;i<40;++i)deep+=" else if(false) {}";
    deep+=" } }";
    NX_EXPECT(!importBlueprintScreen(deep,doc,error)&&error.find("nesting limit")!=std::string::npos);
    NX_EXPECT(doc.serialize(after,error)&&after==saved);
}

NX_TEST(nativeImportSingleStatementBodiesRejectUnsafeChanges)
{
    BlueprintDocument doc;std::string error,generated,saved,after;
    const std::string valid="class Example : Screen { void onEnter(){ int x=1; if(true) ++x; else --x; for(int i=0;i<2;++i) x+=i; } }";
    NX_EXPECT(importBlueprintScreen(valid,doc,error)&&compileBlueprint(doc,generated,error));
    NX_EXPECT(doc.serialize(saved,error));
    for(const char* invalid:{
        "class Bad : Screen { void onEnter(){ for(int i=0;i<3;++i) ++i; } }",
        "class Bad : Screen { void onEnter(){ int x=1; ++x++; } }",
        "class Bad : Screen { void onEnter(){ int x=1; if(true) x=2; else else x=3; } }",
        "class Bad : Screen { void onEnter(){ for(int i=0;i<3;++i) ; int x=i; } }",
        "class Bad : Screen { void onEnter(){ if(true) { int x=1; } ++x; } }"})
    {
        NX_EXPECT(!importBlueprintScreen(invalid,doc,error)&&!error.empty());
        NX_EXPECT(doc.serialize(after,error)&&after==saved);
    }
}

NX_TEST(nativeImportExplicitInstanceReferencesDoNotResolveToParameters)
{
    BlueprintDocument doc;std::string error,code,saved,after;
    const char* valid="class Example : Screen { int x; void Set(int x){this.x=x;} int Get(int x){return this.x+x;} void onEnter(){this.Set(2);int n=this.Get(3);} }";
    NX_EXPECT(importBlueprintScreen(valid,doc,error)&&compileBlueprint(doc,code,error));
    NX_EXPECT(code.find("bp_var_x = bp_arg_x")!=std::string::npos);
    NX_EXPECT(code.find("bp_fn_Set(int(2))")!=std::string::npos);
    NX_EXPECT(doc.serialize(saved,error));
    for(const char* invalid:{
        "class Bad : Screen { int F(int x){return this.x;} }",
        "class Bad : Screen { void F(int x){this.x=x;} }",
        "class Bad : Screen { void onEnter(){this.Missing();} }",
        "class Bad : Screen { int x; void onEnter(){this.x=1; int n=this.F()+1;} int F(){return 1;} }"})
    {
        NX_EXPECT(!importBlueprintScreen(invalid,doc,error)&&!error.empty());
        NX_EXPECT(doc.serialize(after,error)&&after==saved);
    }
}

NX_TEST(nativeImportedLayoutAndScopedBlocksAreDeterministic)
{
    BlueprintDocument doc,other;std::string error,code,saved,again;
    const auto* original=R"(class Example : Screen {
        int score;
        void onEnter() {
            int total=1;
            { int local=2; total+=local; }
            { int local=3; total+=local; }
            for(int i=0;i<=2;++i) total+=i;
            if(total==9) Save::set("scoped","yes");
        }
    })";
    NX_EXPECT(importBlueprintScreen(original,doc,error)&&compileBlueprint(doc,code,error));
    NX_EXPECT(doc.serialize(saved,error));
    NX_EXPECT(importBlueprintScreen(original,other,error)&&other.serialize(again,error)&&saved==again);
    for(const auto& g:doc.graphs)for(size_t i=0;i<g.nodes.size();++i)
    {
        BlueprintNodeSpec spec;NX_EXPECT(doc.nodeSpec(g,g.nodes[i],spec));
        const auto h=46.f+22.f*static_cast<float>(std::max(spec.inputs.size(),spec.outputs.size()));
        for(size_t j=i+1;j<g.nodes.size();++j)
        {
            BlueprintNodeSpec second;NX_EXPECT(doc.nodeSpec(g,g.nodes[j],second));
            const auto h2=46.f+22.f*static_cast<float>(std::max(second.inputs.size(),second.outputs.size()));
            const auto& a=g.nodes[i];const auto& b=g.nodes[j];
            NX_EXPECT(a.x+196<=b.x||b.x+196<=a.x||a.y+std::max(72.f,h)<=b.y||b.y+std::max(72.f,h2)<=a.y);
        }
    }
    NX_EXPECT(!importBlueprintScreen("class Bad : Screen { void onEnter(){ {int x=1;} int y=x; } }",doc,error));
    NX_EXPECT(!importBlueprintScreen("class Bad : Screen { void onEnter(){ for(int i=0;i<=10000;++i) {} } }",doc,error));
    NX_EXPECT(doc.serialize(again,error)&&again==saved);
}

NX_TEST(nativeLargeProjectRoundTripHistoryAndMalformedInputStress)
{
    BlueprintDocument doc;doc.create();std::string error,saved,again,source,after;
    NX_INFO("Large project stress: authoring 2048 data nodes");
    // Over 2,000 editable nodes distributed over the native event graphs.
    for(size_t graph=0;graph<4;++graph)
    {
        for(size_t i=0;i<512;++i)
            if(!doc.addNode(graph,"literal:int",static_cast<float>(i%16)*252.f,
                static_cast<float>(i/16)*120.f,error)){NX_EXPECT(false);return;}
        NX_EXPECT(doc.setLiteral(graph,512,"42",error));
    }
    NX_INFO("Large project stress: authoring execution chain");
    // Include real connected execution in the workload, not only disconnected data.
    for(size_t i=0;i<32;++i)
    {
        const size_t n=doc.graphs[0].nodes.size();
        if(!doc.addNode(0,"api:Save:Save::set:string,string",360.f,static_cast<float>(i)*120.f,error)||
            !doc.setInputValue(0,n,"key","stress"+std::to_string(i),error)||
            !doc.setInputValue(0,n,"value","ok",error)||
            !doc.connect(0,i?n-1:0,"then",n,"in",error)){NX_EXPECT(false);return;}
    }
    NX_INFO("Large project stress: serializing and compiling");
    NX_EXPECT(doc.serialize(saved,error)&&compileBlueprint(doc,source,error));
    NX_INFO("Large project stress: loading and recompiling");
    BlueprintDocument loaded;
    NX_EXPECT(loaded.load(saved.data(),saved.size(),error)&&loaded.serialize(again,error)&&again==saved);
    NX_EXPECT(compileBlueprint(loaded,after,error)&&after==source);
    if(loaded.graphs.size()<4)return;
    const auto original=loaded.graphs[3].nodes[512].x;
    for(size_t i=0;i<16;++i)
    {
        NX_EXPECT(loaded.move(3,512,original+static_cast<float>(i+1),4000.f));
        NX_EXPECT(loaded.undo()&&!loaded.dirty());
        NX_EXPECT(loaded.redo()&&loaded.dirty());
        NX_EXPECT(loaded.undo()&&!loaded.dirty());
    }
    NX_EXPECT(loaded.serialize(again,error)&&again==saved);
    // The serializer appends a newline; removing whitespace is not corruption.
    const auto closing=saved.find_last_not_of(" \t\r\n");
    NX_EXPECT(closing!=std::string::npos&&saved[closing]=='}');
    if(closing==std::string::npos)return;
    NX_EXPECT(loaded.load(saved.data(),closing+1,error)&&loaded.serialize(again,error)&&again==saved);
    for(size_t cut:{size_t(1),saved.size()/2,closing})
    {
        NX_EXPECT(!loaded.load(saved.data(),cut,error));
        NX_EXPECT(loaded.serialize(again,error)&&again==saved);
    }
}

NX_TEST(nativeDocumentAcceptsReduxMainScreenId)
{
    BlueprintDocument doc;doc.name="Imported";std::string error,bytes;
    NX_EXPECT(doc.addScreen("main",error)&&doc.addScreen("level",error));
    NX_EXPECT(!doc.addScreen("main",error)&&!doc.addScreen("screen",error));
    NX_EXPECT(doc.serialize(bytes,error));
    BlueprintDocument restored;
    NX_EXPECT(restored.load(bytes.data(),bytes.size(),error));
    NX_EXPECT(restored.screens()==std::vector<std::string>({"main","level"}));
    std::vector<BlueprintScreenOutput> output;
    NX_EXPECT(compileBlueprintProject(restored,output,error)&&output.size()==2);
    if(output.size()==2)NX_EXPECT(output[0].id=="main"&&output[1].id=="level");
}

NX_TEST(nativeScreenManifestsSurviveSaveExportAndScreenHistory)
{
    BlueprintDocument doc;doc.create();std::string error,bytes;
    const std::string original="{\n  \"title\": \"Original title\", \"custom\": {\"precision\": 1.234567891}, \"order\": 12\n}\n";
    NX_EXPECT(doc.addScreen("level",error));
    NX_EXPECT(doc.importScreenManifest("level",original,error));
    NX_EXPECT(!doc.importScreenManifest("level",original,error));
    NX_EXPECT(!doc.importScreenManifest("Screen","{}",error));
    NX_EXPECT(!doc.importScreenManifest("missing",original,error));
    NX_EXPECT(!doc.importScreenManifest("Screen",std::string(65537,'x'),error));
    NX_EXPECT(doc.serialize(bytes,error));
    BlueprintDocument restored;NX_EXPECT(restored.load(bytes.data(),bytes.size(),error));
    std::vector<BlueprintScreenOutput> output;
    NX_EXPECT(compileBlueprintProject(restored,output,error)&&output.size()==2);
    if(output.size()==2)NX_EXPECT(output[1].manifest==original&&output[0].manifest!=original);
    NX_EXPECT(restored.removeScreen("level",error)&&!restored.screenManifest("level"));
    NX_EXPECT(restored.undo()&&restored.screenManifest("level")&&*restored.screenManifest("level")==original);
    NX_EXPECT(restored.redo()&&!restored.screenManifest("level"));
    NX_EXPECT(restored.addScreen("level",error)&&!restored.screenManifest("level"));
    NX_EXPECT(restored.serialize(bytes,error));
}

NX_TEST(nativeImportResolvesBoundedIncludeTreesTransactionally)
{
    const std::string script="class Included : Screen { void onEnter(){ Save::set(\"key\",\"value\"); } }";
    std::string expanded,error;
    const auto read=[&](const std::string& name,std::string& text)
    {
        if(name=="global:shared/entry.as")text="#include \"asset:screen.as\"\n";
        else if(name=="asset:screen.as")text=script;
        else if(name=="asset:cycle.as")text="#include \"asset:cycle.as\"\n";
        else return false;
        return true;
    };
    NX_EXPECT(resolveBlueprintIncludes("#include \"global:shared/entry.as\"\n#include \"asset:screen.as\"\n","main.as",read,expanded,error));
    BlueprintDocument doc;NX_EXPECT(importBlueprintScreen(expanded,doc,error));
    const auto before=expanded;
    for(const char* invalid:{"#include \"asset:cycle.as\"\n","#include \"asset:missing.as\"\n","#include \"global:../bad.as\"\n","#include \"own:bad.as\"\n","#include \"asset:screen.as\" trailing\n","#define X 1\n"})
    {
        NX_EXPECT(!resolveBlueprintIncludes(invalid,"main.as",read,expanded,error));
        NX_EXPECT(expanded==before&&(error.find("main.as:1:")!=std::string::npos||error.find("asset:cycle.as:1:")!=std::string::npos));
    }
    NX_EXPECT(!resolveBlueprintIncludes(std::string(512*1024+1,'x'),"main.as",read,expanded,error)&&expanded==before);
}

NX_TEST(nativeImportStandaloneFunctionsRetainScopeAndForwardCalls)
{
    BlueprintDocument doc;std::string error,generated,saved;
    const std::string code=R"(int Twice(int amount){int n=amount+amount;return n;}
        class Example : Screen { int value; void onEnter(){value=Twice(4);Record(value);} }
        void Record(int amount){if(amount==8){Save::set("standalone","ok");}})";
    NX_EXPECT(importBlueprintScreen(code,doc,error)&&compileBlueprint(doc,generated,error));
    NX_EXPECT(doc.function("Screen","Twice")&&doc.function("Screen","Record"));
    NX_EXPECT(generated.find("bp_fn_Twice(int(4))")!=std::string::npos&&generated.find("bp_fn_Record(bp_var_value)")!=std::string::npos);
    NX_EXPECT(doc.serialize(saved,error));
    for(const char* invalid:{
        "int state=1+2; class Bad : Screen {}",
        "int Read(){return x;} class Bad : Screen {int x;}",
        "void Write(){x=1;} class Bad : Screen {int x;}",
        "void Run(){F();} class Bad : Screen {void F(){}}",
        "void Run(){this.F();} class Bad : Screen {void F(){}}",
        "void Run(){} class Bad : Screen {void onEnter(){this.Run();}}",
        "void Run(){} class Bad : Screen {void Run(){}}",
        "void Run(){} class Bad : Screen {int Run;}",
        "void onEnter(){} class Bad : Screen {}",
        "void Run(){Run();} class Bad : Screen {}"})
    {
        NX_EXPECT(!importBlueprintScreen(invalid,doc,error)&&!error.empty());
        std::string after;NX_EXPECT(doc.serialize(after,error)&&after==saved);
    }
}

NX_TEST(nativeImportBehaviourClassesKeepIndependentOwnerScopes)
{
    BlueprintDocument doc;std::string error,source,saved;
    const char* code=R"(class Counter : Behaviour {
        int count;
        void start(){count=1;Record();}
        void Record(){if(count==1){Save::set(entity.name,"started");}}
        void onHit(Entity@ partner){Save::set(partner.name,entity.name);}
    }
    class Example : Screen {int count;void Record(){count=2;}void onEnter(){Record();}})";
    NX_EXPECT(importBlueprintScreen(code,doc,error)&&compileBlueprint(doc,source,error));
    NX_EXPECT(doc.behaviours()==std::vector<std::string>{"Counter"});
    NX_EXPECT(doc.function("behaviour:Counter","Record")&&doc.function("Screen","Record"));
    NX_EXPECT(source.find("class BP_Behaviour_Counter : Behaviour")!=std::string::npos&&source.find("(other).name")!=std::string::npos);
    NX_EXPECT(doc.serialize(saved,error));
    for(const char* invalid:{"class A : Behaviour {} class B : Screen {} class A : Behaviour {}", "class A : Behaviour {int entity;} class B : Screen {}", "class A : Behaviour {void start(){Record();}} class B : Screen {void Record(){}}", "class A : Behaviour {void onHit(int other){}} class B : Screen {}"})
    {
        NX_EXPECT(!importBlueprintScreen(invalid,doc,error));
        std::string after;NX_EXPECT(doc.serialize(after,error)&&after==saved);
    }
}

NX_TEST(nativeImportDirectBehaviourAttachmentPreservesCallSemantics)
{
    BlueprintDocument doc;std::string error,source,saved;
    const char* code=R"(class Actor : Behaviour {void start(){Save::set("actor","started");}}
        class Example : Screen {void onEnter(){Entity@ a=Scene::spawn(Vec2(0.0f,0.0f));a.add(Actor());}})";
    NX_EXPECT(importBlueprintScreen(code,doc,error)&&compileBlueprint(doc,source,error));
    NX_EXPECT(std::any_of(doc.graphs[0].nodes.begin(),doc.graphs[0].nodes.end(),[](const BlueprintDocument::Node& n){return n.kind=="attach:direct"&&n.ref=="Actor";}));
    NX_EXPECT(source.find(".add(BP_Behaviour_Actor());")!=std::string::npos&&source.find("!is null")==std::string::npos);
    NX_EXPECT(!doc.removeBehaviour("Actor",error)&&doc.serialize(saved,error));
    for(const char* bad:{"class Actor : Behaviour {} class Example : Screen {void onEnter(){Entity@ a=Scene::spawn(Vec2(0.0f,0.0f));a.add(Actor(1));}}", "class Example : Screen {void onEnter(){Entity@ a=Scene::spawn(Vec2(0.0f,0.0f));a.add(Missing());}}"})
    {
        NX_EXPECT(!importBlueprintScreen(bad,doc,error));
        std::string after;NX_EXPECT(doc.serialize(after,error)&&after==saved);
    }
}

NX_TEST(nativeImportedProjectMergesIndependentBehavioursAndRemapsConflicts)
{
    BlueprintDocument project;project.name="Merged";std::string error,bytes;
    auto merge=[&](const std::string& owner,const std::string& value)
    {
        const auto code="class Actor : Behaviour {void start(){Save::set(\"actor\",\""+value+"\");}} class Example : Screen {void onEnter(){Entity@ a=Scene::spawn(Vec2(0.0f,0.0f));a.add(Actor());}}";
        BlueprintDocument doc;return importBlueprintScreen(code,doc,error)&&mergeImportedBlueprintScreen(project,std::move(doc),owner,error);
    };
    NX_EXPECT(merge("main","first")&&merge("intro","first")&&merge("level-two","second"));
    NX_EXPECT(project.behaviours()==std::vector<std::string>({"Actor","level_two_Actor_1"}));
    NX_EXPECT(project.screens()==std::vector<std::string>({"main","intro","level-two"}));
    for(const auto& g:project.graphs)for(const auto& n:g.nodes)if(n.kind=="attach:direct")
        NX_EXPECT(n.ref==(g.owner=="level-two"?"level_two_Actor_1":"Actor"));
    NX_EXPECT(project.serialize(bytes,error));
    std::vector<BlueprintScreenOutput> output;NX_EXPECT(compileBlueprintProject(project,output,error)&&output.size()==3);
    NX_EXPECT(!merge("main","other"));
    std::string after;NX_EXPECT(project.serialize(after,error)&&after==bytes);
}

NX_TEST(nativeImportedPrimitiveFieldsPreserveDeclarationsAndInitialValues)
{
    BlueprintDocument doc;std::string error,source,bytes;
    const char* code=R"(class Counter : Behaviour {int count=7;void start(){if(count==7){Save::set("counter","ok");}}}
        class Example : Screen {int unused;int score=-4;string label="initial";float speed=1.5f;bool flag=true;uint count=uint(3);double precise=1.25;void onEnter(){Save::set("label",label);}})";
    NX_EXPECT(importBlueprintScreen(code,doc,error)&&doc.serialize(bytes,error));
    BlueprintDocument restored;NX_EXPECT(restored.load(bytes.data(),bytes.size(),error)&&compileBlueprint(restored,source,error));
    NX_EXPECT(source.find("int bp_var_unused;")!=std::string::npos&&source.find("int bp_var_score = int(-4);")!=std::string::npos);
    NX_EXPECT(source.find("string bp_var_label = \"initial\";")!=std::string::npos&&source.find("int bp_var_count = int(7);")!=std::string::npos);
    for(const char* invalid:{"class Bad : Screen {int x=1.0f;}", "class Bad : Screen {int x=2+3;}", "class Bad : Screen {uint x=-1u;}", "class Bad : Screen {int x=999999999999;}"})
    {
        NX_EXPECT(!importBlueprintScreen(invalid,doc,error));
        std::string after;NX_EXPECT(doc.serialize(after,error)&&after==bytes);
    }
}

NX_TEST(nativeImportedModuleGlobalsRetainSharedScope)
{
    BlueprintDocument doc;std::string error,source,bytes;
    const char* code=R"(int count=2; void Bump(){count++;} class Actor : Behaviour {void start(){count++;}} class Example : Screen {void onEnter(){Bump();if(count==3){Save::set("global","ok");}}})";
    NX_EXPECT(importBlueprintScreen(code,doc,error)&&doc.serialize(bytes,error));
    BlueprintDocument restored;NX_EXPECT(restored.load(bytes.data(),bytes.size(),error)&&compileBlueprint(restored,source,error));
    NX_EXPECT(restored.moduleGlobals().size()==1&&source.find("int bp_global_count = int(2);")!=std::string::npos);
    NX_EXPECT(source.find("bp_global_count =")!=std::string::npos&&source.find("bp_var_count")==std::string::npos);
    NX_EXPECT(restored.addScreen("level",error)&&restored.undo());
    NX_EXPECT(restored.addNode(0,"global:get:int",0,0,error));
    NX_EXPECT(!restored.setReference(0,restored.graphs[0].nodes.size()-1,"missing",error));
    NX_EXPECT(restored.undo());
    std::string after;NX_EXPECT(restored.serialize(after,error)&&after==bytes);
    NX_EXPECT(!importBlueprintScreen("int state=1+2; class Bad : Screen {}",doc,error));
    NX_EXPECT(doc.serialize(after,error)&&after==bytes);
}

NX_TEST(nativeImportedMultiScreenGlobalsAndBehavioursKeepModuleOwnership)
{
    BlueprintDocument project;project.name="Modules";std::string error,bytes;
    for(const auto& owner:std::vector<std::string>{"main","level"})
    {
        const auto code=std::string("int count=")+(owner=="main"?"1":"9")+"; class Actor : Behaviour {void start(){count++;}} class Example : Screen {void onEnter(){count++;Entity@ a=Scene::spawn(Vec2(0.0f,0.0f));a.add(Actor());}}";
        BlueprintDocument screen;NX_EXPECT(importBlueprintScreen(code,screen,error)&&mergeImportedBlueprintScreen(project,std::move(screen),owner,error));
    }
    NX_EXPECT(project.moduleGlobals("main").size()==1&&project.moduleGlobals("level").size()==1);
    NX_EXPECT(project.moduleForOwner("behaviour:Actor")=="main"&&project.moduleForOwner("behaviour:level_Actor_1")=="level");
    NX_EXPECT(!project.canAttach("level","Actor")&&!project.canAttach("main","level_Actor_1"));
    NX_EXPECT(project.serialize(bytes,error));
    BlueprintDocument restored;NX_EXPECT(restored.load(bytes.data(),bytes.size(),error));
    std::vector<BlueprintScreenOutput> out;NX_EXPECT(compileBlueprintProject(restored,out,error)&&out.size()==2);
    if(out.size()==2)
    {
        NX_EXPECT(out[0].source.find("int bp_global_count = int(1);")!=std::string::npos&&out[0].source.find("BP_Behaviour_level_Actor_1")==std::string::npos);
        NX_EXPECT(out[1].source.find("int bp_global_count = int(9);")!=std::string::npos&&out[1].source.find("BP_Behaviour_Actor")==std::string::npos);
    }
    NX_EXPECT(!restored.removeScreen("level",error));
}

NX_TEST(nativeVariableDeclarationEditorRenamesReferencesTransactionally)
{
    BlueprintDocument doc;std::string error,before,after,source;
    NX_EXPECT(importBlueprintScreen("int count=2; class Example : Screen {int score=1;void onEnter(){score++;count++;}}",doc,error));
    NX_EXPECT(doc.serialize(before,error));
    NX_EXPECT(doc.editVariableDeclaration("Screen","field score>points:int=7",error));
    NX_EXPECT(doc.editVariableDeclaration("Screen","global count>total:int=9",error));
    NX_EXPECT(compileBlueprint(doc,source,error)&&source.find("bp_var_points")!=std::string::npos&&source.find("bp_global_total")!=std::string::npos&&source.find("bp_var_score")==std::string::npos);
    NX_EXPECT(doc.serialize(after,error));
    NX_EXPECT(!doc.editVariableDeclaration("Screen","field -points",error)&&!doc.editVariableDeclaration("Screen","global total:float=1",error));
    std::string checked;NX_EXPECT(doc.serialize(checked,error)&&checked==after);
    NX_EXPECT(doc.undo()&&doc.undo()&&doc.serialize(checked,error)&&checked==before);
    NX_EXPECT(doc.redo()&&doc.redo()&&doc.serialize(checked,error)&&checked==after);
    NX_EXPECT(doc.editVariableDeclaration("Screen","field label:string=hello world",error)&&doc.editVariableDeclaration("Screen","field -label",error));
    NX_EXPECT(doc.serialize(checked,error)&&checked==after);
}

NX_TEST(nativeNamedVariablePlacementUsesExactReferenceAndSingleUndo)
{
    BlueprintDocument doc;std::string error,before,after;
    NX_EXPECT(importBlueprintScreen("int count=2; class Example : Screen {int score=1;}",doc,error)&&doc.serialize(before,error));
    NX_EXPECT(doc.addVariableNode(0,"score",false,false,200,100,error));
    NX_EXPECT(doc.graphs[0].nodes.back().kind=="var:get:int"&&doc.graphs[0].nodes.back().ref=="score");
    NX_EXPECT(doc.undo()&&doc.serialize(after,error)&&after==before);
    NX_EXPECT(doc.addVariableNode(0,"count",true,true,200,100,error));
    NX_EXPECT(doc.graphs[0].nodes.back().kind=="global:set:int"&&doc.graphs[0].nodes.back().ref=="count");
    NX_EXPECT(doc.undo()&&doc.serialize(after,error)&&after==before);
    NX_EXPECT(!doc.addVariableNode(0,"missing",false,false,200,100,error)&&doc.serialize(after,error)&&after==before);
}

NX_TEST(nativeContextPlacementConnectsAtomicallyAndRejectsIncompatiblePins)
{
    BlueprintDocument doc;doc.create();std::string error,before,after;
    NX_EXPECT(doc.serialize(before,error));
    NX_EXPECT(doc.addConnectedNode(0,0,"then","branch",200,100,error));
    NX_EXPECT(doc.graphs[0].nodes.size()==2&&doc.graphs[0].links.size()==1&&doc.graphs[0].links[0].input=="in");
    NX_EXPECT(doc.undo()&&doc.serialize(after,error)&&after==before);
    NX_EXPECT(!doc.addConnectedNode(0,0,"then","literal:int",200,100,error)&&doc.serialize(after,error)&&after==before);
    NX_EXPECT(doc.addConnectedNode(0,0,"then","branch",200,100,error)&&doc.serialize(before,error));
    NX_EXPECT(!doc.addConnectedNode(0,0,"then","branch",400,100,error)&&doc.serialize(after,error)&&after==before);
}

NX_TEST(nativeContextSearchSupportsReversePlacementNamedFunctionsAndSafeConversions)
{
    BlueprintDocument doc;doc.create();std::string error,before,after;
    NX_EXPECT(doc.addFunction("Pick",error));
    const auto fn=doc.graphs.size()-1;
    NX_EXPECT(doc.setFunctionSignature(fn,{{"value","double"}},"int",error));
    NX_EXPECT(doc.canCallFunction(0,"Pick")&&!doc.canCallFunction(fn,"Pick"));
    NX_EXPECT(doc.addNode(0,"literal:int",100,100,error));
    NX_EXPECT(doc.serialize(before,error));
    NX_EXPECT(doc.placeContextNode(0,1,"value",true,"call:function","Pick","arg_value",400,100,error));
    NX_EXPECT(doc.graphs[0].nodes.size()==4&&doc.graphs[0].nodes[2].ref=="Pick"&&doc.graphs[0].nodes[3].kind=="cast:int:double"&&doc.graphs[0].links.size()==2);
    NX_EXPECT(doc.undo()&&doc.serialize(after,error)&&after==before);
    NX_EXPECT(doc.addNode(0,"var:set:int",500,100,error));
    NX_EXPECT(doc.serialize(before,error));
    NX_EXPECT(doc.placeContextNode(0,2,"value",false,"call:function","Pick","result",200,100,error));
    NX_EXPECT(doc.graphs[0].nodes.back().ref=="Pick"&&doc.graphs[0].links.back().from==3&&doc.graphs[0].links.back().to==2);
    NX_EXPECT(doc.undo()&&doc.serialize(after,error)&&after==before);
    NX_EXPECT(!doc.placeContextNode(0,2,"value",false,"literal:double","","value",200,100,error)&&doc.serialize(after,error)&&after==before);
    NX_EXPECT(doc.placeContextNode(0,static_cast<size_t>(-1),"",true,"call:function","Pick","",200,100,error));
    NX_EXPECT(doc.graphs[0].nodes.back().ref=="Pick"&&doc.undo());
}

NX_TEST(nativeLayoutMetadataRoundTripsAndDoesNotChangeGeneratedCode)
{
    BlueprintDocument doc;doc.create();std::string error,before,after,source,generated;
    NX_EXPECT(doc.addNode(0,"branch",300,60,error)&&doc.addNode(0,"literal:bool",100,220,error));
    NX_EXPECT(doc.connect(0,0,"then",1,"in",error)&&doc.connect(0,2,"value",1,"condition",error));
    NX_EXPECT(compileBlueprint(doc,source,error)&&doc.serialize(before,error));
    NX_EXPECT(doc.addComment(0,{1,2},"Decision",error));
    NX_EXPECT(doc.insertRoutePoint(0,0,0,200,100)&&doc.insertRoutePoint(0,0,1,250,120));
    NX_EXPECT(doc.moveRoutePoint(0,0,1,260,140)&&doc.moveNodes(0,{1,2},20,40));
    NX_EXPECT(doc.renameComment(0,0,"Updated",error)&&doc.serialize(after,error));
    BlueprintDocument restored;NX_EXPECT(restored.load(after.data(),after.size(),error)&&restored.graphs[0].comments.size()==1&&restored.graphs[0].links[0].route.size()==2);
    NX_EXPECT(compileBlueprint(restored,generated,error)&&source==generated);
    NX_EXPECT(restored.arrange(0,error)&&restored.graphs[0].links[0].route.empty());
    NX_EXPECT(restored.undo()&&restored.serialize(generated,error)&&generated==after);
    NX_EXPECT(restored.alignNodes(0,{1,2},false,error)&&restored.graphs[0].nodes[1].x==restored.graphs[0].nodes[2].x);
    NX_EXPECT(restored.deleteNode(0,2,error)&&restored.graphs[0].comments[0].members.size()==1);
    NX_EXPECT(restored.deleteNode(0,1,error)&&restored.graphs[0].comments.empty());
    NX_EXPECT(!doc.addComment(0,{1},"Overlap",error)&&!doc.insertRoutePoint(0,0,99,0,0));
}

NX_TEST(nativeCollapseLinearSelectionPreservesTypedBoundaryAndUndo)
{
    BlueprintDocument doc;std::string error,before,after,source;
    NX_EXPECT(importBlueprintScreen("class Example : Screen {int score;void onEnter(){score=7;score++;}}",doc,error));
    const auto& g=doc.graphs[0];std::vector<size_t> selected;
    for(size_t i=0;i<g.nodes.size();++i)if(g.nodes[i].kind!="entry")selected.push_back(i);
    NX_EXPECT(doc.serialize(before,error));
    NX_EXPECT(doc.collapseToFunction(0,selected,"ApplyScore",error));
    NX_EXPECT(doc.graphs[0].nodes.size()==2&&doc.graphs[0].nodes.back().ref=="ApplyScore"&&doc.function("Screen","ApplyScore"));
    NX_EXPECT(compileBlueprint(doc,source,error)&&source.find("bp_fn_ApplyScore")!=std::string::npos);
    NX_EXPECT(doc.serialize(after,error));
    NX_EXPECT(doc.undo());std::string checked;NX_EXPECT(doc.serialize(checked,error)&&checked==before);
    NX_EXPECT(doc.redo()&&doc.serialize(checked,error)&&checked==after);
    NX_EXPECT(doc.renameFunction(doc.graphs.size()-1,"UpdateScore",error)&&doc.graphs[0].nodes.back().ref=="UpdateScore");
    NX_EXPECT(doc.duplicateFunction(doc.graphs.size()-1,"AnotherScore",error)&&doc.function("Screen","AnotherScore"));
    NX_EXPECT(doc.undo()&&doc.undo()&&doc.serialize(checked,error)&&checked==after);
}
NX_TEST(nativeCollapsedGraphExpandsTypedTunnels)
{
    BlueprintDocument doc;std::string error,bytes,source;doc.create();
    NX_EXPECT(doc.addNode(0,"literal:int",0,200,error)&&doc.addNode(0,"capture:int",300,100,error)&&doc.addNode(0,"var:set:int",600,100,error));
    NX_EXPECT(doc.connect(0,0,"then",2,"in",error)&&doc.connect(0,1,"value",2,"value",error)&&doc.connect(0,2,"then",3,"in",error)&&doc.connect(0,2,"result",3,"value",error));
    NX_EXPECT(doc.collapseToFunction(0,{2},"PrivateCapture",error,true));
    NX_EXPECT(doc.function("Screen","PrivateCapture")->collapsed&&compileBlueprint(doc,source,error));
    NX_EXPECT(doc.serialize(bytes,error));
    BlueprintDocument copy;NX_EXPECT(copy.load(bytes.data(),bytes.size(),error));
    NX_EXPECT(copy.expandCollapsed(0,copy.graphs[0].nodes.size()-1,error));
    NX_EXPECT(copy.graphs[0].nodes.size()==4&&copy.graphs[0].links.size()==4&&compileBlueprint(copy,source,error));
    NX_EXPECT(copy.undo()&&copy.serialize(source,error)&&source==bytes);
}
NX_TEST(nativeCollapseUsesStableInputAndOneExecutedReturn)
{
    BlueprintDocument doc;std::string error,before,source;doc.create();
    NX_EXPECT(doc.addNode(0,"literal:int",0,200,error)&&doc.addNode(0,"capture:int",300,100,error)&&doc.addNode(0,"var:set:int",600,100,error));
    NX_EXPECT(doc.connect(0,0,"then",2,"in",error)&&doc.connect(0,1,"value",2,"value",error)&&doc.connect(0,2,"then",3,"in",error)&&doc.connect(0,2,"result",3,"value",error));
    NX_EXPECT(doc.collapseToFunction(0,{2},"CaptureValue",error));
    const auto* fn=doc.function("Screen","CaptureValue");NX_EXPECT(fn&&fn->parameters.size()==1&&fn->returnType=="int");
    NX_EXPECT(compileBlueprint(doc,source,error)&&source.find("bp_fn_CaptureValue")!=std::string::npos);
    NX_EXPECT(doc.serialize(before,error));
    NX_EXPECT(!doc.collapseToFunction(0,{0},"Bad",error));std::string after;NX_EXPECT(doc.serialize(after,error)&&after==before);
}

NX_TEST(nativeCollapseRejectsLiveExternalGetterAndPartialCommentTransactionally)
{
    BlueprintDocument doc;std::string error,before,after;doc.create();
    NX_EXPECT(doc.addNode(0,"var:get:int",100,200,error)&&doc.addNode(0,"var:set:int",300,100,error));
    NX_EXPECT(doc.connect(0,0,"then",2,"in",error)&&doc.connect(0,1,"value",2,"value",error)&&doc.serialize(before,error));
    NX_EXPECT(!doc.collapseToFunction(0,{2},"Unsafe",error)&&doc.serialize(after,error)&&after==before);
    NX_EXPECT(doc.addComment(0,{0,1,2},"Partial",error)&&doc.serialize(before,error));
    NX_EXPECT(!doc.collapseToFunction(0,{1,2},"Partial",error)&&doc.serialize(after,error)&&after==before);
    NX_EXPECT(doc.deleteComment(0,0)&&doc.collapseToFunction(0,{1,2},"Safe",error));
}

NX_TEST(nativeInlineMacrosExpandWithTypedArgumentsAndNoCallableMethod)
{
    BlueprintDocument doc;std::string error,source,bytes;
    NX_EXPECT(importBlueprintScreen("class Example : Screen {int score;void SetScore(int value){score=value;}void onEnter(){SetScore(3);SetScore(4);}}",doc,error));
    const auto* fn=doc.function("Screen","SetScore");NX_EXPECT(fn!=nullptr);if(!fn)return;
    const auto index=static_cast<size_t>(fn-doc.graphs.data());
    NX_EXPECT(doc.setInlineMacro(index,true,error)&&doc.serialize(bytes,error));
    BlueprintDocument restored;NX_EXPECT(restored.load(bytes.data(),bytes.size(),error)&&restored.graphs[index].inlineMacro);
    NX_EXPECT(compileBlueprint(restored,source,error)&&source.find("bp_fn_SetScore")==std::string::npos&&source.find("inline macro SetScore")!=std::string::npos);
    NX_EXPECT(!restored.setFunctionSignature(index,{},"int",error));
    NX_EXPECT(restored.setInlineMacro(index,false,error)&&compileBlueprint(restored,source,error)&&source.find("bp_fn_SetScore")!=std::string::npos);
}
NX_TEST(nativeCollapseCompleteBranchRegionWithSingleContinuation)
{
    BlueprintDocument doc;std::string error,source;
    NX_EXPECT(importBlueprintScreen("class Example : Screen {int score;void onEnter(){if(score==0){score=7;}else{score=9;}}}",doc,error));
    std::vector<size_t> nodes;for(size_t i=0;i<doc.graphs[0].nodes.size();++i)if(doc.graphs[0].nodes[i].kind!="entry")nodes.push_back(i);
    NX_EXPECT(doc.collapseToFunction(0,nodes,"Decision",error)&&compileBlueprint(doc,source,error));
    NX_EXPECT(doc.setInlineMacro(doc.graphs.size()-1,true,error)&&compileBlueprint(doc,source,error)&&source.find("inline macro Decision")!=std::string::npos);
}

NX_TEST(nativeArrayFieldsRoundTripAndGenerateNativeOperations)
{
    BlueprintDocument doc;std::string error,source,bytes;
    const char* original=R"(array<int> shared; class Example : Screen { int[] scores; void onEnter(){scores.insertLast(3);scores.resize(2);scores[1]=7;shared.insertLast(scores[1]);shared.removeAt(0);if(scores.length()==uint(2)){Save::set("arrays","ok");}}})";
    NX_EXPECT(importBlueprintScreen(original,doc,error)&&doc.serialize(bytes,error));
    BlueprintDocument restored;NX_EXPECT(restored.load(bytes.data(),bytes.size(),error)&&compileBlueprint(restored,source,error));
    NX_EXPECT(source.find("array<int> bp_var_scores;")!=std::string::npos&&source.find("array<int> bp_global_shared;")!=std::string::npos);
    NX_EXPECT(source.find(".insertLast(")!=std::string::npos&&source.find(".removeAt(")!=std::string::npos&&source.find(".resize(")!=std::string::npos);
    NX_EXPECT(restored.editVariableDeclaration("Screen","field labels:array<string>",error));
    NX_EXPECT(restored.addVariableNode(0,"labels",false,false,0,0,error));
    NX_EXPECT(!restored.addVariableNode(0,"labels",false,true,0,0,error));
    NX_EXPECT(!restored.editVariableDeclaration("Screen","field bad:array<int>=1",error));
}

#include "blueprint_orbit_fixture.h"
NX_TEST(nativeCameraOrbitIsAuthoredThroughDocumentOperations)
{
    BlueprintDocument doc,fixture;std::string error,source,expected;
    if(!blueprint_orbit::author(doc,error)){NX_ERROR("Orbit author: %s",error.c_str());NX_EXPECT(false);return;}
    NX_EXPECT(doc.graphs[0].nodes.size()==3&&doc.graphs[1].nodes.size()==6&&doc.graphs[2].nodes.size()==8);
    NX_EXPECT(fixture.load(blueprint_orbit::project,sizeof(blueprint_orbit::project)-1,error));
    NX_EXPECT(compileBlueprint(doc,source,error)&&compileBlueprint(fixture,expected,error)&&source==expected);
    NX_EXPECT(source.find("SolidBox")!=std::string::npos&&source.find("SolidPlane")!=std::string::npos&&source.find("Space::camera")!=std::string::npos);
}

NX_TEST(nativeTimelineSimplifiesOrbitWithoutManualState)
{
    BlueprintDocument doc,fixture;std::string error,source,expected;
    if(!blueprint_orbit::author(doc,error,true)){NX_ERROR("Timeline author: %s",error.c_str());NX_EXPECT(false);return;}
    NX_EXPECT(doc.graphs[0].fields.empty()&&doc.graphs[0].nodes.size()==3&&doc.graphs[1].nodes.size()==5&&doc.graphs[2].nodes.size()==3);
    NX_EXPECT(fixture.load(blueprint_orbit::timelineProject,sizeof(blueprint_orbit::timelineProject)-1,error));
    NX_EXPECT(compileBlueprint(doc,source,error)&&compileBlueprint(fixture,expected,error)&&source==expected);
    NX_EXPECT(source.find("bp_timeline_state_1_1")!=std::string::npos&&source.find("Space::camera")!=std::string::npos);
    NX_EXPECT(!doc.addNode(0,"time:timeline",200,200,error));
}

NX_TEST(nativeTimedCameraOrbitReducesSceneToSevenActiveNodes)
{
    BlueprintDocument doc;std::string error,source;
    if(!blueprint_orbit::author(doc,error,2)){NX_ERROR("Timed orbit author: %s",error.c_str());NX_EXPECT(false);return;}
    NX_EXPECT(doc.graphs[0].nodes.size()+doc.graphs[1].nodes.size()+doc.graphs[2].nodes.size()==7);
    NX_EXPECT(doc.graphs[0].links.size()+doc.graphs[1].links.size()+doc.graphs[2].links.size()==4);
    NX_EXPECT(compileBlueprint(doc,source,error)&&source.find("Scene::find")!=std::string::npos&&source.find("Space::camera")!=std::string::npos&&source.find("Space::draw(canvas)")!=std::string::npos);
    NX_EXPECT(!doc.addNode(0,"camera:orbit-timed",100,100,error)&&!doc.addNode(1,"render:space",100,100,error));
}

NX_TEST(nativeVectorAndColorPinsStoreTypedComponentsWithoutExtraNodes)
{
    BlueprintDocument doc;doc.create();std::string error,source,bytes,before;
    NX_EXPECT(doc.addNode(0,"spawn:box",300,100,error)&&doc.connect(0,0,"then",1,"in",error));
    NX_EXPECT(doc.setInputValue(0,1,"place","1, 2.5, -3",error)&&doc.setInputValue(0,1,"size","2,2,2",error)&&doc.setInputValue(0,1,"color","0.9,0.4,0.2,1",error));
    NX_EXPECT(doc.serialize(before,error));
    NX_EXPECT(!doc.setInputValue(0,1,"place","1,2,3);Save::set(\"bad\",\"bad\")",error)&&!doc.setInputValue(0,1,"place","1,2",error)&&!doc.setInputValue(0,1,"place","1,,3",error)&&!doc.setInputValue(0,1,"color","1,2,3,NaN",error));
    NX_EXPECT(doc.serialize(bytes,error)&&bytes==before);
    BlueprintDocument loaded;NX_EXPECT(loaded.load(bytes.data(),bytes.size(),error)&&compileBlueprint(loaded,source,error));
    NX_EXPECT(source.find("Vec3(float(1), float(2.5), float(-3))")!=std::string::npos&&source.find("Color(float(0.9), float(0.4), float(0.2), float(1))")!=std::string::npos);
    NX_EXPECT(doc.undo()&&doc.graphs[0].nodes[1].values.size()==2);
    NX_EXPECT(doc.redo()&&doc.serialize(bytes,error)&&bytes==before);
}

NX_TEST(nativeReusableVisualAssetsRoundTripAndUndo)
{
    BlueprintDocument doc;doc.create();std::string error,bytes;
    using Asset=BlueprintDocument::VisualAsset;
    const std::vector<Asset> assets{
        {"player","actor","BP Player","{ \"entities\": [] }",{"hud"}},
        {"hud","widget","Player HUD","[ { \"label\": \"Health\" } ]",{}}
    };
    NX_EXPECT(doc.replaceVisualAssets(assets,error)&&doc.dirty());
    NX_EXPECT(doc.visualAsset("player")&&doc.visualAsset("hud"));
    NX_EXPECT(doc.serialize(bytes,error));
    BlueprintDocument copy;NX_EXPECT(copy.load(bytes.data(),bytes.size(),error));
    NX_EXPECT(copy.visualAssets().size()==2);
    if(const auto* hud=copy.visualAsset("hud"))NX_EXPECT(hud->source==assets[1].source);
    NX_EXPECT(doc.undo()&&doc.visualAssets().empty()&&!doc.dirty());
    NX_EXPECT(doc.redo()&&doc.visualAssets().size()==2);
    auto changed=assets;changed[0].title="Renamed Player";
    NX_EXPECT(doc.replaceVisualAssets(changed,error));
    NX_EXPECT(doc.undo());
    if(const auto* player=doc.visualAsset("player"))NX_EXPECT(player->title=="BP Player");
    const auto revision=doc.revision();
    changed[0].dependencies={"missing"};
    NX_EXPECT(!doc.replaceVisualAssets(changed,error)&&doc.revision()==revision);
    changed=assets;changed[0].dependencies={"player"};
    NX_EXPECT(!doc.replaceVisualAssets(changed,error));
    changed=assets;changed[1].dependencies={"player"};
    NX_EXPECT(!doc.replaceVisualAssets(changed,error));
    changed=assets;changed[1].dependencies={"hud"};
    NX_EXPECT(!doc.replaceVisualAssets(changed,error));
    changed=assets;changed[1].source="{}";
    NX_EXPECT(!doc.replaceVisualAssets(changed,error));
    changed=assets;changed[1].id="player";
    NX_EXPECT(!doc.replaceVisualAssets(changed,error));
    changed=assets;changed[1].id="../hud";
    NX_EXPECT(!doc.replaceVisualAssets(changed,error));
    std::vector<Asset> chain;
    for(unsigned i=0;i<17;++i)
    {
        Asset a{"a"+std::to_string(i),"actor","Actor","{}",{}};
        if(i)a.dependencies.push_back("a"+std::to_string(i-1));
        chain.push_back(a);
    }
    NX_EXPECT(!doc.replaceVisualAssets(chain,error));
    NX_EXPECT(doc.visualAssets().size()==2);
}

NX_TEST(nativeVisualAssetPaletteProducesNativeWidgetJson)
{
    BlueprintDocument doc;doc.create();std::string error,widget,actor,bytes;
    NX_EXPECT(doc.createVisualAsset("widget",widget,error));
    NX_EXPECT(doc.createVisualAsset("actor",actor,error)&&actor!=widget);
    NX_EXPECT(doc.renameVisualAsset(widget,"Player HUD",error));
    NX_EXPECT(doc.appendVisualWidget(widget,"label","Health",error));
    NX_EXPECT(doc.appendVisualWidget(widget,"button","Play",error));
    NX_EXPECT(doc.appendVisualWidget(widget,"progress","",error));
    if(const auto* page=doc.visualAsset(widget))
    {
        NX_EXPECT(page->source.find("Health")!=std::string::npos);
        NX_EXPECT(page->source.find("item-3")!=std::string::npos);
        NX_EXPECT(page->source.find("0.75")!=std::string::npos);
    }
    const auto revision=doc.revision();
    NX_EXPECT(!doc.appendVisualWidget(actor,"label","Wrong",error)&&doc.revision()==revision);
    NX_EXPECT(doc.removeVisualAsset(widget,error)&&!doc.visualAsset(widget));
    NX_EXPECT(doc.undo()&&doc.visualAsset(widget));
    NX_EXPECT(doc.serialize(bytes,error));
    BlueprintDocument copy;NX_EXPECT(copy.load(bytes.data(),bytes.size(),error));
    if(const auto* page=copy.visualAsset(widget))NX_EXPECT(page->title=="Player HUD");
    auto assets=copy.visualAssets();
    for(auto& a:assets)if(a.id==actor)a.dependencies={widget};
    NX_EXPECT(copy.replaceVisualAssets(assets,error));
    NX_EXPECT(!copy.removeVisualAsset(widget,error));
}

NX_TEST(nativeLinkedWidgetCompilesCurrentPageAndProtectsIdentity)
{
    BlueprintDocument doc;doc.create();std::string error,id,source,bytes;
    NX_EXPECT(doc.createVisualAsset("widget",id,error));
    NX_EXPECT(doc.appendVisualWidget(id,"label","Health",error));
    NX_EXPECT(doc.connectVisualWidget("Screen",id,error));
    NX_EXPECT(compileBlueprint(doc,source,error));
    NX_EXPECT(source.find("Ui::loadPage(")!=std::string::npos&&source.find("Health")!=std::string::npos);
    NX_EXPECT(source.find("Ui::draw(canvas)")!=std::string::npos);
    NX_EXPECT(!doc.removeVisualAsset(id,error));
    const auto revision=doc.revision();
    NX_EXPECT(doc.connectVisualWidget("Screen",id,error)&&doc.revision()==revision);
    NX_EXPECT(doc.appendVisualWidget(id,"button","Play",error)&&compileBlueprint(doc,source,error));
    NX_EXPECT(source.find("Play")!=std::string::npos);
    NX_EXPECT(doc.undo()&&compileBlueprint(doc,source,error)&&source.find("Play")==std::string::npos);
    NX_EXPECT(doc.redo()&&compileBlueprint(doc,source,error)&&source.find("Play")!=std::string::npos);
    std::string old,next;
    NX_EXPECT(doc.createVisualAsset("actor",old,error)&&doc.removeVisualAsset(old,error));
    NX_EXPECT(doc.serialize(bytes,error));
    BlueprintDocument loaded;NX_EXPECT(loaded.load(bytes.data(),bytes.size(),error));
    NX_EXPECT(loaded.createVisualAsset("actor",next,error)&&next!=old);
    NX_EXPECT(loaded.addScreen("other",error));
    const size_t other=loaded.graphs.size()-6;
    NX_EXPECT(loaded.addNode(other,"branch",300,40,error)&&loaded.connect(other,0,"then",1,"in",error));
    const auto before=loaded.revision();
    NX_EXPECT(!loaded.connectVisualWidget("other",id,error)&&loaded.revision()==before);
}

NX_TEST(nativeUpstreamFactoriesReferencesAndArrayBorrow)
{
    BlueprintDocument doc;doc.create();std::string error,source,bytes;
    NX_EXPECT(doc.editVariableDeclaration("Screen","field music:Playlist@",error));
    NX_EXPECT(doc.editVariableDeclaration("Screen","global socket:WebSocket@",error));
    NX_EXPECT(doc.editVariableDeclaration("Screen","field names:array<string>",error));
    NX_EXPECT(doc.editVariableDeclaration("Screen","field points:array<float>",error));
    NX_EXPECT(doc.addNode(0,"create:playlist",0,0,error));
    NX_EXPECT(doc.addVariableNode(0,"music",false,true,0,0,error));
    NX_EXPECT(doc.connect(0,0,"then",1,"in",error)&&doc.connect(0,1,"then",2,"in",error)&&doc.connect(0,1,"result",2,"value",error));
    NX_EXPECT(doc.addNode(0,"create:websocket",0,0,error)&&doc.setInputValue(0,3,"url","ws://127.0.0.1:9",error));
    NX_EXPECT(doc.addVariableNode(0,"socket",true,true,0,0,error));
    NX_EXPECT(doc.connect(0,2,"then",3,"in",error)&&doc.connect(0,3,"then",4,"in",error)&&doc.connect(0,3,"result",4,"value",error));
    NX_EXPECT(doc.addVariableNode(1,"names",false,false,0,0,error)&&doc.addVariableNode(1,"points",false,false,0,0,error));
    NX_EXPECT(doc.addNode(1,"api:Entity:animateBlend:Entity@,array<string>@,array<float>@,float",0,0,error));
    NX_EXPECT(doc.connectCompatible(1,1,"value",3,"names",error)&&doc.connectCompatible(1,2,"value",3,"at",error));
    NX_EXPECT(!doc.connectCompatible(1,2,"value",3,"names",error));
    NX_EXPECT(doc.serialize(bytes,error));
    BlueprintDocument copy;NX_EXPECT(copy.load(bytes.data(),bytes.size(),error));
    NX_EXPECT(compileBlueprint(copy,source,error));
    NX_EXPECT(source.find("Playlist()")!=std::string::npos&&source.find("WebSocket(")!=std::string::npos);
    NX_EXPECT(source.find("@bp_var_music = ")!=std::string::npos&&source.find("@bp_global_socket = ")!=std::string::npos);
}
NX_TEST(nativeOldJointNodeIdsMigrate)
{
    BlueprintDocument doc;doc.create();std::string error,bytes;
    NX_EXPECT(doc.addNode(0,"api:Space:Space::join:Entity@,Entity@,Vec3,float",0,0,error));
    NX_EXPECT(doc.addNode(0,"api:Space:Space::hinge:Entity@,Entity@,Vec3,Vec3,float,float",0,0,error));
    NX_EXPECT(doc.serialize(bytes,error));
    for(const auto& pair:std::vector<std::pair<std::string,std::string>>{
        {"api:Space:Space::join:Entity@,Entity@,Vec3,float","api:Space:Space::join:Entity@,Entity@,Vec3"},
        {"api:Space:Space::hinge:Entity@,Entity@,Vec3,Vec3,float,float","api:Space:Space::hinge:Entity@,Entity@,Vec3,Vec3"}})
    {
        const auto at=bytes.find(pair.first);
        NX_EXPECT(at!=std::string::npos);
        if(at!=std::string::npos)bytes.replace(at,pair.first.size(),pair.second);
    }
    BlueprintDocument copy;NX_EXPECT(copy.load(bytes.data(),bytes.size(),error));
    NX_EXPECT(copy.graphs[0].nodes[1].kind=="api:Space:Space::join:Entity@,Entity@,Vec3,float");
    NX_EXPECT(copy.graphs[0].nodes[2].kind=="api:Space:Space::hinge:Entity@,Entity@,Vec3,Vec3,float,float");
}

NX_TEST(nativeImportUpstreamHandleAndBlendWorkflows)
{
    const char* script=R"AS(
WebSocket@ socket;
class Example : Screen {
    Playlist@ music;
    Widget@ label;
    array<string> names;
    array<float> speeds;
    void onEnter() {
        @music = Playlist();
        music.shuffle = true;
        music.play();
        @socket = WebSocket("ws://127.0.0.1:9");
        names.insertLast("idle");
        speeds.insertLast(0.0f);
        Entity@ actor = Space::spawn(Vec3(0.0f,0.0f,0.0f));
        actor.animateBlend(names,speeds);
        @label = Ui::find("health");
    }
    void onExit() {
        socket.close();
        @socket = null;
        music.stop();
        @music = null;
        @label = null;
    }
}
)AS";
    BlueprintDocument doc;std::string error,source,bytes;
    NX_EXPECT(importBlueprintScreen(script,doc,error)&&compileBlueprint(doc,source,error));
    NX_EXPECT(source.find("Playlist()")!=std::string::npos&&source.find("WebSocket(")!=std::string::npos);
    NX_EXPECT(source.find("animateBlend(")!=std::string::npos&&source.find("@bp_global_socket = null;")!=std::string::npos);
    NX_EXPECT(doc.serialize(bytes,error));
    for(const char* bad:{
        "class Bad : Screen { Playlist@ p; void onEnter(){p = Playlist();} }",
        "class Bad : Screen { Playlist@ p; void onEnter(){@p = WebSocket(\"ws://localhost\");} }",
        "class Bad : Screen { int n; void onEnter(){@n=1;} }"})
    {
        NX_EXPECT(!importBlueprintScreen(bad,doc,error));
        std::string after;NX_EXPECT(doc.serialize(after,error)&&after==bytes);
    }
}

NX_TEST(nativeLinkedActorsReuseDefinitionAndMigrateAcrossScreens)
{
    BlueprintDocument doc;doc.create();std::string error,id,code,bytes;
    NX_EXPECT(doc.createVisualAsset("actor",id,error));
    NX_EXPECT(doc.placeVisualActor("Screen",id,"1,2,3",error));
    NX_EXPECT(doc.placeVisualActor("Screen",id,"4,5,6",error));
    NX_EXPECT(doc.addScreen("other",error));
    NX_EXPECT(doc.placeVisualActor("other",id,"7,8,9",error));
    NX_EXPECT(!doc.removeVisualAsset(id,error));
    NX_EXPECT(doc.setVisualActorShape(id,"sphere",error));
    std::vector<BlueprintScreenOutput> output;
    NX_EXPECT(compileBlueprintProject(doc,output,error)&&output.size()==2);
    for(const auto& screen:output)
    {
        NX_EXPECT(screen.source.find("Space::makeActor(Json(")!=std::string::npos);
        NX_EXPECT(screen.source.find("sphere")!=std::string::npos);
        NX_EXPECT(screen.source.find("Space::draw(canvas)")!=std::string::npos);
    }
    NX_EXPECT(doc.undo());
    NX_EXPECT(compileBlueprintOwner(doc,"Screen",code,error)&&code.find("sphere")==std::string::npos);
    NX_EXPECT(doc.redo()&&doc.serialize(bytes,error));
    BlueprintDocument copy;NX_EXPECT(copy.load(bytes.data(),bytes.size(),error));
    NX_EXPECT(compileBlueprintOwner(copy,"other",code,error)&&code.find("sphere")!=std::string::npos);
    NX_EXPECT(!copy.placeVisualActor("other",id,"NaN,0,0",error));
}

NX_TEST(nativeActorComponentHierarchyPreservesLinksAndUndo)
{
    BlueprintDocument doc;doc.create();std::string error,id,source,bytes;
    NX_EXPECT(doc.createVisualAsset("actor",id,error));
    NX_EXPECT(doc.appendVisualActorComponent(id,0,"box","0,1.5,0",error));
    NX_EXPECT(doc.appendVisualActorComponent(id,1,"sphere","1,0,0",error));
    NX_EXPECT(doc.setVisualActorShape(id,"capsule",error,1));
    NX_EXPECT(doc.placeVisualActor("Screen",id,"2,0,0",error)&&compileBlueprint(doc,source,error));
    NX_EXPECT(source.find("Space::makeActor(Json(")!=std::string::npos&&source.find("capsule")!=std::string::npos);
    NX_EXPECT(doc.serialize(bytes,error));
    BlueprintDocument copy;NX_EXPECT(copy.load(bytes.data(),bytes.size(),error));
    NX_EXPECT(copy.visualActorSource(id,source,error)&&source.find("parents")!=std::string::npos);
    NX_EXPECT(!copy.appendVisualActorComponent(id,99,"box","0,0,0",error));
    NX_EXPECT(doc.undo()&&doc.undo());
    NX_EXPECT(doc.visualActorSource(id,source,error)&&source.find("capsule")==std::string::npos);
    NX_EXPECT(doc.redo()&&doc.redo());
    auto assets=copy.visualAssets();
    assets[0].source="{\"entities\":[{},{}],\"parents\":[-1,1]}";
    NX_EXPECT(copy.replaceVisualAssets(assets,error));
    NX_EXPECT(!copy.visualActorSource(id,source,error));
}

NX_TEST(nativeActorInspectorEditsSharedComponentProperties)
{
    BlueprintDocument doc;doc.create();std::string error,id,source,bytes;
    NX_EXPECT(doc.createVisualAsset("actor",id,error)&&doc.addBehaviour("Movement",error));
    NX_EXPECT(doc.appendVisualActorComponent(id,0,"box","0,1,0",error));
    NX_EXPECT(doc.setVisualActorProperty(id,1,"name","Body",error));
    NX_EXPECT(doc.setVisualActorProperty(id,1,"place","1,2,3",error));
    NX_EXPECT(doc.setVisualActorProperty(id,1,"angles","0,90,0",error));
    NX_EXPECT(doc.setVisualActorProperty(id,1,"size","2,1,1",error));
    NX_EXPECT(doc.setVisualActorProperty(id,1,"behaviour","Movement",error));
    NX_EXPECT(!doc.removeBehaviour("Movement",error));
    NX_EXPECT(doc.setVisualActorProperty(id,1,"model","global:models/body.glb",error));
    NX_EXPECT(!doc.setVisualActorProperty(id,1,"model","asset:../bad.glb",error));
    NX_EXPECT(!doc.setVisualActorProperty(id,1,"size","0,1,1",error));
    NX_EXPECT(!doc.setVisualActorProperty(id,1,"behaviour","Missing",error));
    NX_EXPECT(doc.placeVisualActor("Screen",id,"0,0,0",error)&&compileBlueprint(doc,source,error));
    NX_EXPECT(source.find("BP_Behaviour_Movement")!=std::string::npos&&source.find("global:models/body.glb")!=std::string::npos);
    NX_EXPECT(doc.serialize(bytes,error));
    BlueprintDocument copy;NX_EXPECT(copy.load(bytes.data(),bytes.size(),error));
    NX_EXPECT(copy.setVisualActorProperty(id,1,"behaviour","",error)&&copy.removeBehaviour("Movement",error));
    NX_EXPECT(copy.undo()&&copy.undo()&&!copy.removeBehaviour("Movement",error));
}

NX_TEST(nativeActorGeneratedSimpleCollision)
{
    BlueprintDocument doc;doc.create();std::string error,id,child,before,after,source;
    NX_EXPECT(doc.createVisualAsset("actor",id,error));
    NX_EXPECT(doc.visualActorSource(id,before,error));
    NX_EXPECT(doc.setVisualActorProperty(id,0,"simpleCollision","2,3,4;0,1.5,0",error));
    NX_EXPECT(doc.visualActorSource(id,after,error));
    JsonTree json;NX_EXPECT(json.parse(after.data(),after.size()));
    const auto entity=json.root()["entities"][0u];
    NX_EXPECT(entity["wall"].flag()&&entity["cube"][1u].number()==3&&entity["colliderAt"][1u].number()==1.5f);
    NX_EXPECT(!doc.setVisualActorProperty(id,0,"simpleCollision","0,3,4;0,0,0",error));
    NX_EXPECT(doc.undo()&&doc.visualActorSource(id,source,error)&&source==before);
    NX_EXPECT(doc.redo()&&doc.visualActorSource(id,source,error)&&source==after);
    NX_EXPECT(doc.createVisualActorChild(id,child,error));
    NX_EXPECT(doc.setVisualActorProperty(child,0,"simpleCollision","1,2,3;0,1,0",error));
    NX_EXPECT(doc.visualActorSource(child,source,error)&&json.parse(source.data(),source.size()));
    NX_EXPECT(json.root()["entities"][0u]["cube"][1u].number()==2);
    NX_EXPECT(doc.visualActorSource(id,source,error)&&source==after);
}
NX_TEST(nativeActorPhysicsAndRootInstanceOverrides)
{
    BlueprintDocument doc;doc.create();std::string error,id,source,bytes;
    NX_EXPECT(doc.createVisualAsset("actor",id,error));
    NX_EXPECT(doc.setVisualActorProperty(id,0,"collision","capsule:0.5,2",error));
    NX_EXPECT(doc.setVisualActorProperty(id,0,"mass","2",error));
    NX_EXPECT(doc.setVisualActorProperty(id,0,"gravity","0",error));
    NX_EXPECT(doc.setVisualActorProperty(id,0,"friction","0.5",error));
    NX_EXPECT(!doc.setVisualActorProperty(id,0,"collision","capsule:2,1",error));
    NX_EXPECT(!doc.setVisualActorProperty(id,0,"bounce","2",error));
    NX_EXPECT(doc.setVisualActorProperty(id,0,"model","global:models/player.glb",error));
    NX_EXPECT(doc.setVisualActorProperty(id,0,"animate","idle",error));
    NX_EXPECT(doc.placeVisualActor("Screen",id,"0,0,0",error));
    NX_EXPECT(doc.placeVisualActor("Screen",id,"4,0,0",error));
    NX_EXPECT(doc.setInputValue(0,1,"instanceName","First",error));
    NX_EXPECT(doc.setInputValue(0,1,"sizeOverride","2,2,2",error));
    NX_EXPECT(!doc.setInputValue(0,1,"sizeOverride","0,1,1",error));
    NX_EXPECT(doc.setVisualActorProperty(id,0,"size","3,3,3",error));
    std::string first,second;
    NX_EXPECT(doc.visualActorInstance(id,"Screen",doc.graphs[0].nodes[1].values,first,error));
    NX_EXPECT(doc.visualActorInstance(id,"Screen",doc.graphs[0].nodes[2].values,second,error));
    NX_EXPECT(first.find("First")!=std::string::npos&&second.find("First")==std::string::npos);
    NX_EXPECT(first!=second);
    NX_EXPECT(doc.serialize(bytes,error));
    BlueprintDocument copy;NX_EXPECT(copy.load(bytes.data(),bytes.size(),error));
    NX_EXPECT(copy.clearInputValue(0,1,"sizeOverride"));
    NX_EXPECT(copy.clearInputValue(0,1,"instanceName"));
    NX_EXPECT(copy.visualActorInstance(id,"Screen",copy.graphs[0].nodes[1].values,first,error)&&first==second);
    NX_EXPECT(copy.setVisualActorProperty(id,0,"collision","none",error));
    NX_EXPECT(copy.setVisualActorProperty(id,0,"model","",error));
    NX_EXPECT(copy.visualActorSource(id,source,error)&&source.find("capsule")==std::string::npos&&source.find("animate")==std::string::npos);
}

NX_TEST(nativeWidgetDesignerHierarchyAndPropertyEdits)
{
    BlueprintDocument doc;doc.create();std::string id,error,bytes,source;
    NX_EXPECT(doc.createVisualAsset("widget",id,error));
    NX_EXPECT(doc.appendVisualWidget(id,"column","",error));
    NX_EXPECT(doc.appendVisualWidget(id,"label","Health",error,"item-1"));
    NX_EXPECT(doc.appendVisualWidget(id,"progress","",error,"item-1"));
    NX_EXPECT(doc.setVisualWidgetProperty(id,"item-2","text","Player health",error));
    NX_EXPECT(doc.setVisualWidgetProperty(id,"item-3","width","320",error));
    NX_EXPECT(doc.setVisualWidgetProperty(id,"item-1","align","center",error));
    NX_EXPECT(!doc.setVisualWidgetProperty(id,"item-1","align","bad",error));
    NX_EXPECT(!doc.appendVisualWidget(id,"button","No",error,"item-2"));
    NX_EXPECT(doc.moveVisualWidget(id,"item-3",-1,error));
    NX_EXPECT(doc.serialize(bytes,error));
    BlueprintDocument copy;NX_EXPECT(copy.load(bytes.data(),bytes.size(),error));
    NX_EXPECT(copy.connectVisualWidget("Screen",id,error)&&compileBlueprint(copy,source,error));
    NX_EXPECT(source.find("Player health")!=std::string::npos&&source.find("320")!=std::string::npos);
    NX_EXPECT(doc.undo()&&doc.redo());
}

NX_TEST(nativeWidgetEventsAreTypedAndSharedAcrossScreenLinks)
{
    BlueprintDocument doc;doc.create();std::string id,error,bytes,source;size_t graph=0;
    NX_EXPECT(doc.createVisualAsset("widget",id,error)&&doc.appendVisualWidget(id,"button","Play",error));
    NX_EXPECT(!doc.openVisualWidgetEvent("Screen",id,"item-1","click",graph,error));
    NX_EXPECT(doc.connectVisualWidget("Screen",id,error));
    NX_EXPECT(doc.openVisualWidgetEvent("Screen",id,"item-1","click",graph,error));
    const auto callback=doc.graphs[graph].name;
    NX_EXPECT(doc.graphs[graph].parameters.size()==1&&doc.graphs[graph].parameters[0].type=="Widget@");
    NX_EXPECT(!doc.renameFunction(graph,"Renamed",error)&&!doc.removeFunction(graph,error));
    NX_EXPECT(!doc.editFunctionSignature(graph,"void()",error)&&!doc.setInlineMacro(graph,true,error));
    const auto revision=doc.revision();size_t same=0;
    NX_EXPECT(doc.openVisualWidgetEvent("Screen",id,"item-1","click",same,error)&&same==graph&&doc.revision()==revision);
    NX_EXPECT(doc.addScreen("other",error)&&doc.connectVisualWidget("other",id,error));
    NX_EXPECT(doc.function("other",callback)!=nullptr);
    NX_EXPECT(doc.openVisualWidgetEvent("other",id,"item-1","draw",same,error));
    NX_EXPECT(doc.graphs[same].parameters.size()==2&&doc.graphs[same].parameters[0].type=="Canvas@");
    NX_EXPECT(doc.serialize(bytes,error));
    BlueprintDocument copy;NX_EXPECT(copy.load(bytes.data(),bytes.size(),error));
    NX_EXPECT(compileBlueprintOwner(copy,"other",source,error)&&source.find("bp_fn_"+callback)!=std::string::npos);
    NX_EXPECT(copy.clearVisualWidgetEvent(id,"item-1","click",error));
    NX_EXPECT(copy.removeFunction(graph,error));
    NX_EXPECT(copy.undo()&&copy.undo());
    NX_EXPECT(!copy.removeFunction(graph,error));
}

NX_TEST(nativeWidgetHierarchyReparentIsTransactional)
{
    BlueprintDocument doc;doc.create();std::string id,error,bytes;
    NX_EXPECT(doc.createVisualAsset("widget",id,error));
    NX_EXPECT(doc.appendVisualWidget(id,"stack","",error));
    NX_EXPECT(doc.appendVisualWidget(id,"column","",error));
    NX_EXPECT(doc.appendVisualWidget(id,"label","HUD",error,"item-2"));
    const auto revision=doc.revision();
    NX_EXPECT(!doc.reparentVisualWidget(id,"item-2","item-3",error)&&doc.revision()==revision);
    NX_EXPECT(!doc.reparentVisualWidget(id,"item-3","item-3",error)&&doc.revision()==revision);
    NX_EXPECT(doc.reparentVisualWidget(id,"item-3","item-1",error));
    NX_EXPECT(doc.serialize(bytes,error));
    BlueprintDocument copy;NX_EXPECT(copy.load(bytes.data(),bytes.size(),error));
    NX_EXPECT(copy.reparentVisualWidget(id,"item-3","",error));
    NX_EXPECT(copy.undo()&&copy.serialize(bytes,error));
    NX_EXPECT(copy.visualAsset(id)->source==doc.visualAsset(id)->source);
}

NX_TEST(nativeWidgetStackAnchorsPersistAndProtectReparent)
{
    BlueprintDocument doc;doc.create();std::string id,error,bytes,source;
    NX_EXPECT(doc.createVisualAsset("widget",id,error));
    NX_EXPECT(doc.appendVisualWidget(id,"stack","",error));
    NX_EXPECT(doc.appendVisualWidget(id,"label","HUD",error,"item-1"));
    NX_EXPECT(!doc.setVisualWidgetProperty(id,"item-1","anchor","bottom-right",error));
    NX_EXPECT(doc.setVisualWidgetProperty(id,"item-2","anchor","bottom-right",error));
    NX_EXPECT(doc.setVisualWidgetProperty(id,"item-2","offset","-24,-24",error));
    NX_EXPECT(!doc.reparentVisualWidget(id,"item-2","",error));
    NX_EXPECT(doc.connectVisualWidget("Screen",id,error)&&compileBlueprint(doc,source,error));
    NX_EXPECT(source.find("bottom-right")!=std::string::npos);
    NX_EXPECT(doc.serialize(bytes,error));
    BlueprintDocument copy;NX_EXPECT(copy.load(bytes.data(),bytes.size(),error));
    NX_EXPECT(copy.setVisualWidgetProperty(id,"item-2","anchor","",error));
    NX_EXPECT(copy.reparentVisualWidget(id,"item-2","",error));
    NX_EXPECT(copy.undo()&&copy.undo()&&!copy.reparentVisualWidget(id,"item-2","",error));
}

NX_TEST(nativeWidgetPreviewPositionIsOneUndoableEdit)
{
    BlueprintDocument doc;doc.create();std::string id,error,bytes;
    NX_EXPECT(doc.createVisualAsset("widget",id,error));
    NX_EXPECT(doc.appendVisualWidget(id,"stack","",error));
    NX_EXPECT(doc.appendVisualWidget(id,"label","HUD",error,"item-1"));
    const auto original=doc.visualAsset(id)->source;
    NX_EXPECT(doc.positionVisualWidget(id,"item-2","top-left",32,48,error));
    const auto moved=doc.visualAsset(id)->source;
    NX_EXPECT(moved!=original);
    NX_EXPECT(doc.undo()&&doc.visualAsset(id)->source==original);
    NX_EXPECT(doc.redo()&&doc.visualAsset(id)->source==moved);
    const auto revision=doc.revision();
    NX_EXPECT(!doc.positionVisualWidget(id,"item-1","center",0,0,error)&&doc.revision()==revision);
    NX_EXPECT(!doc.positionVisualWidget(id,"item-2","bad",0,0,error)&&doc.revision()==revision);
    NX_EXPECT(doc.positionVisualWidget(id,"item-2","top-left",32,48,error)&&doc.revision()==revision);
    NX_EXPECT(doc.serialize(bytes,error));
    BlueprintDocument copy;NX_EXPECT(copy.load(bytes.data(),bytes.size(),error));
    NX_EXPECT(copy.visualAsset(id)->source==moved);
}

NX_TEST(nativeReusableThemeLinksAndStyleProperties)
{
    BlueprintDocument doc;doc.create();std::string page,theme,error,source,bytes;
    NX_EXPECT(doc.createVisualAsset("widget",page,error)&&doc.appendVisualWidget(page,"button","Play",error));
    NX_EXPECT(doc.createVisualAsset("theme",theme,error));
    NX_EXPECT(doc.setVisualThemeProperty(theme,"button","normal","back","#123456",error));
    NX_EXPECT(doc.setVisualThemeProperty(theme,"button","focused","back","#ffffff",error));
    NX_EXPECT(doc.setVisualThemeProperty(theme,"button","normal","radius","8",error));
    NX_EXPECT(doc.setVisualThemeProperty(theme,"button","normal","minWidth","160",error));
    NX_EXPECT(doc.setVisualThemeProperty(theme,"button","normal","minHeight","60",error));
    NX_EXPECT(doc.setVisualThemeProperty(theme,"button","normal","slice","8",error));
    NX_EXPECT(doc.setVisualThemeProperty(theme,"button","normal","align","right",error));
    NX_EXPECT(!doc.setVisualThemeProperty(theme,"button","normal","align","invalid",error));
    NX_EXPECT(!doc.setVisualThemeProperty(theme,"button","focused","align","left",error));
    NX_EXPECT(!doc.setVisualThemeProperty(theme,"button","hovered","minWidth","80",error));
    NX_EXPECT(doc.setVisualThemeProperty(theme,"button","normal","font","global:freesans.sfn",error));
    NX_EXPECT(doc.setVisualThemeProperty(theme,"button","normal","image","asset:ui/panel.png",error));
    NX_EXPECT(doc.setVisualThemeProperty(theme,"button","normal","soundAccept","global:sound/accept.wav",error));
    NX_EXPECT(!doc.setVisualThemeProperty(theme,"button","normal","image","asset:../bad.png",error));
    NX_EXPECT(!doc.setVisualThemeProperty(theme,"button","pressed","font","global:freesans.sfn",error));
    NX_EXPECT(!doc.setVisualThemeProperty(theme,"button","focused","padding","3",error));
    NX_EXPECT(!doc.setVisualThemeProperty(theme,"button","normal","back","#xyz",error));
    NX_EXPECT(doc.setVisualTheme(page,theme,error)&&doc.connectVisualWidget("Screen",page,error));
    NX_EXPECT(!doc.removeVisualAsset(theme,error));
    NX_EXPECT(compileBlueprint(doc,source,error));
    NX_EXPECT(source.find("Ui::loadThemeSource(")<source.find("Ui::loadPage("));
    NX_EXPECT(source.find("#123456")!=std::string::npos);
    NX_EXPECT(doc.serialize(bytes,error));
    BlueprintDocument copy;NX_EXPECT(copy.load(bytes.data(),bytes.size(),error));
    NX_EXPECT(copy.setVisualTheme(page,"",error)&&compileBlueprint(copy,source,error));
    NX_EXPECT(source.find("Ui::loadThemeSource(")==std::string::npos);
    NX_EXPECT(copy.undo()&&compileBlueprint(copy,source,error)&&source.find("#123456")!=std::string::npos);
}

NX_TEST(nativeLinkedWidgetComponentsExpandAndTrackDefinitions)
{
    BlueprintDocument doc;doc.create();std::string page,child,grand,theme,error,source;
    NX_EXPECT(doc.createVisualAsset("widget",page,error)&&doc.createVisualAsset("widget",child,error)&&doc.createVisualAsset("widget",grand,error));
    NX_EXPECT(doc.appendVisualWidget(grand,"label","Nested",error));
    NX_EXPECT(doc.appendVisualWidget(child,"component",grand,error));
    NX_EXPECT(doc.appendVisualWidget(page,"component",child,error)&&doc.appendVisualWidget(page,"component",child,error));
    NX_EXPECT(!doc.appendVisualWidget(grand,"component",page,error));
    NX_EXPECT(!doc.removeVisualAsset(child,error));
    NX_EXPECT(doc.createVisualAsset("theme",theme,error)&&doc.setVisualTheme(page,theme,error));
    NX_EXPECT(doc.visualWidgetPage(page,source,error));
    NX_EXPECT(source.find("item-1/item-1/item-1")!=std::string::npos&&source.find("item-2/item-1/item-1")!=std::string::npos);
    NX_EXPECT(doc.setVisualWidgetProperty(grand,"item-1","text","Changed",error));
    NX_EXPECT(doc.visualWidgetPage(page,source,error)&&source.find("Changed")!=std::string::npos);
    NX_EXPECT(doc.connectVisualWidget("Screen",page,error)&&compileBlueprint(doc,source,error));
    NX_EXPECT(source.find("item-2/item-1/item-1")!=std::string::npos);
    NX_EXPECT(doc.removeVisualWidgetComponent(page,"item-1",error)&&doc.removeVisualWidgetComponent(page,"item-2",error));
    NX_EXPECT(doc.removeVisualAsset(child,error));NX_EXPECT(doc.undo()&&doc.undo());
    NX_EXPECT(doc.visualWidgetPage(page,source,error)&&source.find("item-2/item-1/item-1")!=std::string::npos);
}

NX_TEST(nativeActorCameraComponentPersistsWithValidatedFov)
{
    BlueprintDocument doc;doc.create();std::string id,error,source;
    NX_EXPECT(doc.createVisualAsset("actor",id,error)&&doc.appendVisualActorComponent(id,0,"camera","0,2,-5",error));
    NX_EXPECT(doc.setVisualActorProperty(id,1,"camera","75",error));
    NX_EXPECT(!doc.setVisualActorProperty(id,1,"camera","180",error));
    NX_EXPECT(doc.placeVisualActor("Screen",id,"3,4,5",error)&&compileBlueprint(doc,source,error));
    NX_EXPECT(source.find("Camera1")!=std::string::npos);
    NX_EXPECT(doc.visualActorSource(id,source,error)&&source.find("75")!=std::string::npos);
}

NX_TEST(nativeWidgetTimelineKeysPersistAndValidate)
{
    BlueprintDocument doc;doc.create();std::string id,error,source,bytes;
    NX_EXPECT(doc.createVisualAsset("widget",id,error)&&doc.appendVisualWidget(id,"label","Animated",error));
    NX_EXPECT(doc.setWidgetKey(id,"item-1","x","100",1,error)&&doc.setWidgetKey(id,"item-1","x","0",0,error));
    NX_EXPECT(doc.setWidgetKey(id,"item-1","opacity","0.5",0.5f,error)&&doc.setWidgetKey(id,"item-1","loop","true",0,error));
    NX_EXPECT(!doc.setWidgetKey(id,"item-1","opacity","2",0,error)&&!doc.setWidgetKey(id,"item-1","duration","0.5",0,error));
    NX_EXPECT(doc.connectVisualWidget("Screen",id,error)&&compileBlueprint(doc,source,error)&&source.find("animation")!=std::string::npos);
    NX_EXPECT(doc.serialize(bytes,error));BlueprintDocument copy;NX_EXPECT(copy.load(bytes.data(),bytes.size(),error));
    NX_EXPECT(copy.setWidgetKey(id,"item-1","x","",1,error)&&copy.undo());
}

NX_TEST(nativeActorOutlinerEditsStableInstanceAndPreservesClass)
{
    BlueprintDocument doc;doc.create();std::string id,error,first,second,bytes;
    NX_EXPECT(doc.createVisualAsset("actor",id,error)&&doc.placeVisualActor("Screen",id,"0,0,0",error)&&doc.placeVisualActor("Screen",id,"4,0,0",error));
    const auto stable=doc.graphs[0].nodes[1].id;
    NX_EXPECT(doc.editVisualActorPlacement("Screen",stable,"instanceName","Guard",error));
    NX_EXPECT(doc.editVisualActorPlacement("Screen",stable,"sizeOverride","2,3,4",error));
    NX_EXPECT(doc.editVisualActorPlacement("Screen",stable,"place","5,2,1",error));
    NX_EXPECT(!doc.editVisualActorPlacement("Screen",stable,"sizeOverride","0,1,1",error));
    NX_EXPECT(!doc.editVisualActorPlacement("Screen",stable,"place","nan,0,0",error));
    NX_EXPECT(!doc.editVisualActorPlacement("Other",stable,"place","0,0,0",error));
    NX_EXPECT(doc.visualActorInstance(id,"Screen",doc.graphs[0].nodes[1].values,first,error)&&doc.visualActorInstance(id,"Screen",doc.graphs[0].nodes[2].values,second,error));
    NX_EXPECT(first.find("Guard")!=std::string::npos&&second.find("Guard")==std::string::npos);
    NX_EXPECT(doc.serialize(bytes,error));BlueprintDocument copy;NX_EXPECT(copy.load(bytes.data(),bytes.size(),error));
    NX_EXPECT(copy.editVisualActorPlacement("Screen",stable,"sizeOverride","",error)&&copy.undo());
    NX_EXPECT(copy.visualActorInstance(id,"Screen",copy.graphs[0].nodes[1].values,second,error)&&first==second);
}

NX_TEST(nativeActorClassInheritancePropagatesAndResetsRootOverrides)
{
    BlueprintDocument doc;doc.create();std::string parent,child,grand,error,source,bytes;
    NX_EXPECT(doc.createVisualAsset("actor",parent,error));
    NX_EXPECT(doc.appendVisualActorComponent(parent,0,"camera","0,2,-5",error));
    NX_EXPECT(doc.createVisualActorChild(parent,child,error)&&doc.createVisualActorChild(child,grand,error));
    NX_EXPECT(doc.setVisualActorProperty(parent,0,"mass","7",error));
    NX_EXPECT(doc.setVisualActorProperty(child,0,"size","2,3,4",error));
    NX_EXPECT(doc.setVisualActorProperty(parent,0,"size","9,9,9",error));
    NX_EXPECT(doc.setVisualActorProperty(parent,1,"camera","85",error));
    NX_EXPECT(doc.visualActorSource(grand,source,error));
    JsonTree tree;NX_EXPECT(tree.parse(source.data(),source.size()));
    NX_EXPECT(tree.root()["entities"][0u]["mass"].number()==7&&tree.root()["entities"][0u]["size"][0u].number()==2);
    NX_EXPECT(tree.root()["entities"][1u]["camera"].number()==85);
    NX_EXPECT(doc.setVisualActorProperty(child,1,"camera","60",error));
    NX_EXPECT(doc.appendVisualActorComponent(child,0,"box","0,0,0",error));
    NX_EXPECT(!doc.removeVisualAsset(parent,error));
    NX_EXPECT(doc.placeVisualActor("Screen",grand,"0,0,0",error)&&compileBlueprint(doc,source,error));
    NX_EXPECT(doc.serialize(bytes,error));BlueprintDocument copy;NX_EXPECT(copy.load(bytes.data(),bytes.size(),error));
    NX_EXPECT(copy.resetVisualActorOverrides(child,error)&&copy.visualActorSource(grand,source,error)&&tree.parse(source.data(),source.size()));
    NX_EXPECT(tree.root()["entities"][0u]["size"][0u].number()==9);
    NX_EXPECT(copy.undo()&&copy.visualActorSource(grand,source,error)&&tree.parse(source.data(),source.size()));
    NX_EXPECT(tree.root()["entities"][0u]["size"][0u].number()==2);
}

NX_TEST(nativeActorCameraSelectionCompilesTypedActorReference)
{
    BlueprintDocument doc;doc.create();std::string id,error,source;
    NX_EXPECT(doc.createVisualAsset("actor",id,error)&&doc.appendVisualActorComponent(id,0,"camera","0,2,-5",error));
    NX_EXPECT(doc.placeVisualActor("Screen",id,"0,0,0",error));
    NX_EXPECT(doc.addConnectedNode(0,1,"then","camera:activate-actor",480,60,error));
    NX_EXPECT(doc.connect(0,1,"result",2,"actor",error)&&doc.setInputValue(0,2,"component","Camera1",error));
    NX_EXPECT(compileBlueprint(doc,source,error)&&source.find("Space::activateActorCamera(")!=std::string::npos);
}

NX_TEST(nativeDerivedComponentsKeepIdentityWhenParentAddsComponents)
{
    BlueprintDocument doc;doc.create();std::string base,child,grand,error,source,bytes;
    NX_EXPECT(doc.createVisualAsset("actor",base,error)&&doc.appendVisualActorComponent(base,0,"camera","0,2,-5",error));
    NX_EXPECT(doc.createVisualActorChild(base,child,error)&&doc.appendVisualActorComponent(child,1,"box","0,1,0",error));
    NX_EXPECT(doc.setVisualActorProperty(child,2,"mass","11",error));
    NX_EXPECT(doc.createVisualActorChild(child,grand,error)&&doc.appendVisualActorComponent(grand,2,"sphere","0,0,1",error));
    NX_EXPECT(doc.setVisualActorProperty(grand,1,"camera","90",error));
    NX_EXPECT(doc.appendVisualActorComponent(base,0,"plane","0,0,0",error));
    NX_EXPECT(doc.visualActorSource(grand,source,error));
    JsonTree tree;NX_EXPECT(tree.parse(source.data(),source.size()));
    NX_EXPECT(tree.root()["entities"].count()==5);
    NX_EXPECT(tree.root()["entities"][1u]["camera"].number()==90);
    NX_EXPECT(tree.root()["entities"][3u]["mass"].number()==11);
    NX_EXPECT(tree.root()["parents"][3u].number()==1&&tree.root()["parents"][4u].number()==3);
    NX_EXPECT(doc.serialize(bytes,error));BlueprintDocument copy;NX_EXPECT(copy.load(bytes.data(),bytes.size(),error));
    std::string actual;NX_EXPECT(copy.visualActorSource(grand,actual,error)&&actual==source);
    NX_EXPECT(copy.resetVisualActorOverrides(grand,error,1)&&copy.visualActorSource(grand,actual,error)&&tree.parse(actual.data(),actual.size()));
    NX_EXPECT(tree.root()["entities"][1u]["camera"].number()==60&&tree.root()["entities"].count()==5);
    NX_EXPECT(tree.root()["entities"][3u]["mass"].number()==11);
    NX_EXPECT(copy.undo()&&copy.visualActorSource(grand,actual,error)&&actual==source);
    NX_EXPECT(copy.placeVisualActor("Screen",grand,"0,0,0",error)&&compileBlueprint(copy,actual,error));
}

NX_TEST(nativeComponentReferenceAndFlowControlsCompile)
{
    BlueprintDocument doc;doc.create();std::string id,error,source,bytes;
    NX_EXPECT(doc.createVisualAsset("actor",id,error)&&doc.appendVisualActorComponent(id,0,"box","0,1,0",error));
    NX_EXPECT(doc.placeVisualActor("Screen",id,"0,0,0",error));
    NX_EXPECT(doc.addNode(0,"actor:component",400,160,error));
    NX_EXPECT(doc.connect(0,1,"result",2,"actor",error)&&doc.setInputValue(0,2,"component","Component1",error));
    NX_EXPECT(doc.addNode(0,"actor:valid",600,160,error)&&doc.connect(0,2,"result",3,"entity",error));
    NX_EXPECT(doc.addConnectedNode(0,1,"then","flow:gate",800,60,error)&&doc.connect(0,3,"result",4,"open",error));
    NX_EXPECT(doc.addConnectedNode(0,4,"body","flow:do-once",1000,60,error));
    NX_EXPECT(doc.addConnectedNode(0,5,"body","flow:do-n",1200,60,error));
    NX_EXPECT(doc.setInputValue(0,6,"count","3",error));
    NX_EXPECT(compileBlueprint(doc,source,error));
    NX_EXPECT(source.find("Space::actorComponent(")!=std::string::npos&&source.find("Space::validEntity(")!=std::string::npos);
    NX_EXPECT(source.find("int bp_flow_state_0_5 = 0;")!=std::string::npos&&source.find("++bp_flow_state_0_6;")!=std::string::npos);
    NX_EXPECT(doc.serialize(bytes,error));BlueprintDocument copy;NX_EXPECT(copy.load(bytes.data(),bytes.size(),error));
    std::string again;NX_EXPECT(compileBlueprint(copy,again,error)&&again==source);
}

NX_TEST(nativeDerivedActorComponentLimitRejectsParentChangeAtomically)
{
    BlueprintDocument doc;doc.create();std::string base,child,error,before,after,source;
    NX_EXPECT(doc.createVisualAsset("actor",base,error)&&doc.createVisualActorChild(base,child,error));
    for(unsigned i=0;i<31;++i)NX_EXPECT(doc.appendVisualActorComponent(child,0,"box","0,0,0",error));
    NX_EXPECT(doc.serialize(before,error));const auto revision=doc.revision();
    NX_EXPECT(!doc.appendVisualActorComponent(base,0,"box","0,0,0",error));
    NX_EXPECT(doc.revision()==revision&&doc.serialize(after,error)&&before==after);
    NX_EXPECT(doc.visualActorSource(child,source,error));
    JsonTree tree;NX_EXPECT(tree.parse(source.data(),source.size())&&tree.root()["entities"].count()==32);
}

NX_TEST(nativeActorBehaviourInheritanceAndExposedSpawnFields)
{
    BlueprintDocument doc;doc.create();std::string base,child,baseOwner,childOwner,error,source,bytes;
    NX_EXPECT(doc.createVisualAsset("actor",base,error)&&doc.createVisualActorChild(base,child,error));
    NX_EXPECT(doc.ensureActorBehaviour(child,childOwner,error)&&doc.ensureActorBehaviour(base,baseOwner,error));
    NX_EXPECT(doc.parentBehaviour(childOwner)==baseOwner);
    NX_EXPECT(doc.editVariableDeclaration(baseOwner,"field health:int=100",error)&&doc.setActorFieldExposed(baseOwner,"health",true,error));
    NX_EXPECT(doc.editVariableDeclaration(childOwner,"field health:int=75",error));
    NX_EXPECT(!doc.editVariableDeclaration(childOwner,"field health:string=bad",error));
    NX_EXPECT(doc.addFunction("Construction",error,baseOwner)&&doc.addFunction("Construction",error,childOwner));
    NX_EXPECT(doc.addFunction("Score",error,baseOwner));
    const auto* fn=doc.function(baseOwner,"Score");NX_EXPECT(fn);
    if(!fn)return;
    NX_EXPECT(doc.editFunctionSignature(static_cast<size_t>(fn-doc.graphs.data()),"int(amount:int)",error));
    NX_EXPECT(doc.addFunction("Score",error,childOwner));
    const auto* overrideFn=doc.function(childOwner,"Score");NX_EXPECT(overrideFn&&overrideFn->returnType=="int"&&overrideFn->parameters.size()==1);
    NX_EXPECT(doc.placeVisualActor("Screen",child,"0,0,0",error)&&doc.setInputValue(0,1,"spawn_health","42",error));
    NX_EXPECT(doc.serialize(bytes,error));BlueprintDocument copy;NX_EXPECT(copy.load(bytes.data(),bytes.size(),error));
    NX_EXPECT(copy.actorSpawnFields(child).size()==1&&copy.parentBehaviour(childOwner)==baseOwner);
    NX_EXPECT(!copy.setActorFieldExposed(baseOwner,"health",false,error));
}

NX_TEST(nativeActorClassParentCallAndInheritedContractProtection)
{
    BlueprintDocument doc;doc.create();std::string base,child,parent,derived,error,source;
    NX_EXPECT(doc.createVisualAsset("actor",base,error)&&doc.createVisualActorChild(base,child,error));
    NX_EXPECT(doc.ensureActorBehaviour(child,derived,error)&&doc.ensureActorBehaviour(base,parent,error));
    NX_EXPECT(doc.editVariableDeclaration(parent,"field score:int=1",error)&&doc.editVariableDeclaration(derived,"field score:int=2",error));
    NX_EXPECT(!doc.editVariableDeclaration(parent,"field score>changed:int=1",error));
    NX_EXPECT(!doc.editVariableDeclaration(derived,"field score:string=wrong",error));
    NX_EXPECT(doc.addFunction("Construction",error,parent)&&doc.addFunction("Construction",error,derived));
    const auto* method=doc.function(derived,"Construction");if(!method){NX_EXPECT(false);return;}
    const auto graph=static_cast<size_t>(method-doc.graphs.data());
    NX_EXPECT(doc.addConnectedNode(graph,0,"then","call:parent",240,60,error));
    NX_EXPECT(doc.placeVisualActor("Screen",child,"0,0,0",error)&&compileBlueprint(doc,source,error));
    NX_EXPECT(source.find("BP_Behaviour_"+parent.substr(10)+"::bp_fn_Construction()")!=std::string::npos);
    NX_EXPECT(doc.resetVisualActorOverrides(child,error)&&doc.actorBehaviour(child)==derived);
    NX_EXPECT(!doc.setVisualActorProperty(child,0,"behaviour","",error));
}

NX_TEST(nativeAdvancedFlowPinsAndRoundTrip)
{
    BlueprintDocument doc;doc.create();std::string error,bytes,source;
    NX_EXPECT(doc.addNode(0,"sequence",0,0,error)&&doc.addNode(0,"flow:controlled-gate",0,0,error));
    NX_EXPECT(doc.connect(0,0,"then",1,"in",error)&&doc.connect(0,1,"first",2,"open",error)&&doc.connect(0,1,"second",2,"in",error)&&doc.connect(0,1,"then",2,"close",error));
    NX_EXPECT(doc.addNode(0,"flow:flip-flop",0,0,error)&&doc.connect(0,2,"body",3,"in",error));
    NX_EXPECT(doc.addNode(0,"flow:multi-gate",0,0,error)&&doc.connect(0,3,"then",4,"in",error));
    NX_EXPECT(doc.addNode(0,"flow:switch-int",0,0,error)&&doc.connect(0,4,"then",5,"in",error));
    NX_EXPECT(doc.setInputValue(0,5,"value","2",error));
    NX_EXPECT(doc.addNode(0,"var:get:array<int>",0,0,error)&&doc.addNode(0,"flow:foreach:int",0,0,error));
    NX_EXPECT(doc.connect(0,5,"then",7,"in",error)&&doc.connect(0,6,"value",7,"target",error));
    NX_EXPECT(doc.serialize(bytes,error));BlueprintDocument copy;NX_EXPECT(copy.load(bytes.data(),bytes.size(),error));
    NX_EXPECT(compileBlueprint(copy,source,error));
    NX_EXPECT(source.find("bp_each_array_7")!=std::string::npos&&source.find("bp_choice_4")!=std::string::npos);
}

NX_TEST(nativeDeferredTimeRoundTripAndScope)
{
    BlueprintDocument doc;doc.create();std::string error,bytes,source;
    NX_EXPECT(doc.addNode(0,"time:delay",0,0,error)&&doc.connect(0,0,"then",1,"in",error));
    NX_EXPECT(doc.addConnectedNode(0,1,"completed","time:retrigger-delay",250,0,error));
    NX_EXPECT(doc.addConnectedNode(0,2,"completed","time:timer",500,0,error));
    NX_EXPECT(doc.setInputValue(0,3,"loop","false",error));
    NX_EXPECT(!doc.addNode(2,"time:delay",0,0,error));
    NX_EXPECT(doc.serialize(bytes,error));BlueprintDocument copy;NX_EXPECT(copy.load(bytes.data(),bytes.size(),error));
    NX_EXPECT(compileBlueprint(copy,source,error));
    NX_EXPECT(source.find("bp_tick_deferred(dt)")!=std::string::npos&&source.find("bp_deferred_0_3_resume(dt)")!=std::string::npos);
}

NX_TEST(nativeDeferredFunctionsRejectOriginalArguments)
{
    BlueprintDocument doc;doc.create();std::string error,source;
    NX_EXPECT(doc.addFunction("Later",error,"Screen"));
    const auto* fn=doc.function("Screen","Later");if(!fn){NX_EXPECT(false);return;}
    const auto g=static_cast<size_t>(fn-doc.graphs.data());
    NX_EXPECT(doc.editFunctionSignature(g,"void(message:string)",error));
    NX_EXPECT(doc.addNode(g,"time:delay",0,0,error)&&doc.connect(g,0,"then",1,"in",error));
    NX_EXPECT(doc.addNode(g,"api:Save:Save::set:string,string",0,0,error)&&doc.connect(g,1,"completed",2,"in",error));
    NX_EXPECT(doc.setInputValue(g,2,"key","later",error)&&doc.connect(g,0,"arg_message",2,"value",error));
    NX_EXPECT(!compileBlueprint(doc,source,error)&&error.find("original function parameters")!=std::string::npos);
}

NX_TEST(nativeCustomEventDispatcherPinsAndClassCastRoundTrip)
{
    BlueprintDocument doc;doc.create();std::string error,bytes,source;
    NX_EXPECT(doc.addFunction("OnScore",error)&&doc.addBehaviour("Target",error));
    const auto* fn=doc.function("Screen","OnScore");if(!fn){NX_EXPECT(false);return;}
    const auto g=static_cast<size_t>(fn-doc.graphs.data());
    NX_EXPECT(doc.editFunctionSignature(g,"void(amount:int)",error));
    NX_EXPECT(doc.addNode(0,"dispatch:bind",0,0,error,"OnScore")&&doc.connect(0,0,"then",1,"in",error));
    NX_EXPECT(doc.addNode(0,"dispatch:broadcast",0,0,error,"OnScore")&&doc.connect(0,1,"then",2,"in",error));
    NX_EXPECT(doc.setInputValue(0,2,"arg_amount","3",error));
    NX_EXPECT(doc.addNode(0,"dispatch:unbind",0,0,error,"OnScore")&&doc.connect(0,2,"then",3,"in",error));
    NX_EXPECT(doc.addNode(0,"call:event",0,0,error,"OnScore")&&doc.connect(0,3,"then",4,"in",error)&&doc.setInputValue(0,4,"arg_amount","4",error));
    NX_EXPECT(doc.addNode(0,"actor:cast-class",0,0,error,"Target")&&doc.connect(0,4,"then",5,"in",error));
    NX_EXPECT(doc.addNode(0,"null:entity",0,0,error)&&doc.connect(0,6,"value",5,"actor",error));
    NX_EXPECT(doc.serialize(bytes,error));BlueprintDocument copy;NX_EXPECT(copy.load(bytes.data(),bytes.size(),error));
    NX_EXPECT(compileBlueprint(copy,source,error));
    NX_EXPECT(source.find("if (bp_bound_OnScore) bp_fn_OnScore")!=std::string::npos&&source.find("cast<BP_Behaviour_Target>")!=std::string::npos);
    NX_EXPECT(!copy.editFunctionSignature(g,"int(amount:int)",error));
}

NX_TEST(nativeTypedCollectionsRoundTrip)
{
    BlueprintDocument doc;doc.create();std::string error,source,bytes;
    NX_EXPECT(doc.editVariableDeclaration("Screen","field scores:BP_MapInt",error));
    NX_EXPECT(doc.addNode(0,"var:get:BP_MapInt",0,0,error)&&doc.setReference(0,1,"scores",error));
    NX_EXPECT(doc.addNode(0,"collection:BP_MapInt:set",0,0,error)&&doc.connect(0,0,"then",2,"in",error)&&doc.connect(0,1,"value",2,"target",error));
    NX_EXPECT(doc.setInputValue(0,2,"key","player",error)&&doc.setInputValue(0,2,"value","42",error));
    NX_EXPECT(doc.serialize(bytes,error));BlueprintDocument copy;NX_EXPECT(copy.load(bytes.data(),bytes.size(),error));
    NX_EXPECT(compileBlueprint(copy,source,error));
    NX_EXPECT(source.find("class BP_MapInt")!=std::string::npos&&source.find("bp_var_scores")!=std::string::npos);
}

NX_TEST(nativeSharedLibraryFunctionAndMacroAcrossOwners)
{
    BlueprintDocument doc;doc.create();std::string error,source,bytes;
    NX_EXPECT(doc.addFunction("Twice",error));
    const auto g=static_cast<size_t>(doc.function("Screen","Twice")-doc.graphs.data());
    NX_EXPECT(doc.editFunctionSignature(g,"int(amount:int)",error));
    NX_EXPECT(doc.addNode(g,"add:int",0,0,error)&&doc.connect(g,0,"arg_amount",1,"a",error)&&doc.connect(g,0,"arg_amount",1,"b",error));
    NX_EXPECT(doc.addNode(g,"return:function",0,0,error)&&doc.connect(g,0,"then",2,"in",error)&&doc.connect(g,1,"result",2,"value",error));
    NX_EXPECT(doc.setSharedLibrary(g,true,error));
    NX_EXPECT(doc.addFunction("SharedMacro",error));
    const auto macro=static_cast<size_t>(doc.function("Screen","SharedMacro")-doc.graphs.data());
    NX_EXPECT(doc.setInlineMacro(macro,true,error)&&doc.setSharedLibrary(macro,true,error));
    NX_EXPECT(doc.addBehaviour("Consumer",error));
    size_t actor=0;for(size_t i=0;i<doc.graphs.size();++i)if(doc.graphs[i].owner=="behaviour:Consumer"&&doc.graphs[i].name=="start")actor=i;
    NX_EXPECT(doc.addNode(actor,"call:library",0,0,error,"Twice")&&doc.connect(actor,0,"then",1,"in",error)&&doc.setInputValue(actor,1,"arg_amount","21",error));
    NX_EXPECT(doc.addNode(actor,"call:library",0,0,error,"SharedMacro")&&doc.connect(actor,1,"then",2,"in",error));
    NX_EXPECT(!doc.setSharedLibrary(g,false,error));
    NX_EXPECT(doc.serialize(bytes,error));BlueprintDocument copy;NX_EXPECT(copy.load(bytes.data(),bytes.size(),error));
    NX_EXPECT(compileBlueprint(copy,source,error)&&source.find("int bp_lib_Twice(")!=std::string::npos&&source.find("inline macro SharedMacro")!=std::string::npos);
}

NX_TEST(nativeNamedHeldInputActionsRoundTripAndRebind)
{
    BlueprintDocument doc;doc.create();std::string error,source,bytes;
    NX_EXPECT(doc.setInputAction("Jump","Space",8,error));
    auto actions=doc.inputActions();NX_EXPECT(actions.size()==1);
    if(!actions.empty()){NX_EXPECT(actions[0].name=="Jump");NX_EXPECT(actions[0].key=="Space");NX_EXPECT(actions[0].button==8);}
    NX_EXPECT(doc.addNode(1,"call:library",340,60,error,"Input_Jump")&&doc.connect(1,0,"then",1,"in",error));
    NX_EXPECT(compileBlueprint(doc,source,error)&&source.find("bp_lib_Input_Jump")!=std::string::npos&&source.find("Input::key")!=std::string::npos&&source.find("Input::padButton")!=std::string::npos);
    NX_EXPECT(doc.setInputAction("Jump","E",10,error));
    NX_EXPECT(doc.serialize(bytes,error));BlueprintDocument copy;NX_EXPECT(copy.load(bytes.data(),bytes.size(),error));
    actions=copy.inputActions();NX_EXPECT(actions.size()==1);
    if(!actions.empty()){NX_EXPECT(actions[0].key=="E");NX_EXPECT(actions[0].button==10);}
    NX_EXPECT(!doc.setInputAction("bad name","Space",8,error));NX_EXPECT(!doc.setInputAction("Jump","None",14,error));NX_EXPECT(!doc.setInputAction("Jump","invalid",8,error));
    NX_EXPECT(doc.undo());actions=doc.inputActions();NX_EXPECT(actions.size()==1);
    if(!actions.empty())NX_EXPECT(actions[0].key=="Space");
}
NX_TEST(nativeCustomTypeDeclarationsAndEvents)
{
    BlueprintDocument doc;doc.create();std::string error,source,bytes;
    NX_EXPECT(doc.addFunction("State",error));const auto en=doc.graphs.size()-1;
    NX_EXPECT(doc.editFunctionSignature(en,"enum(Idle, Running)",error));
    NX_EXPECT(doc.addFunction("Stats",error));const auto st=doc.graphs.size()-1;
    NX_EXPECT(doc.editFunctionSignature(st,"struct(health:int, name:string)",error));
    NX_EXPECT(doc.addFunction("Notify",error));const auto ev=doc.graphs.size()-1;
    NX_EXPECT(doc.editFunctionSignature(ev,"event(stats:BP_Data_Stats)",error));
    NX_EXPECT(doc.addNode(0,"data:make-struct",0,0,error,"Stats")&&doc.setInputValue(0,1,"health","42",error)&&doc.setInputValue(0,1,"name","hero",error));
    NX_EXPECT(doc.addNode(0,"call:event",0,0,error,"Notify")&&doc.connect(0,0,"then",2,"in",error)&&doc.connect(0,1,"result",2,"arg_stats",error));
    NX_EXPECT(doc.addNode(ev,"data:break-struct",0,0,error,"Stats")&&doc.connect(ev,0,"arg_stats",1,"value",error));
    NX_EXPECT(doc.serialize(bytes,error));BlueprintDocument copy;NX_EXPECT(copy.load(bytes.data(),bytes.size(),error));
    NX_EXPECT(compileBlueprint(copy,source,error)&&source.find("enum BP_Data_State")!=std::string::npos&&source.find("class BP_Data_Stats")!=std::string::npos);
}

NX_TEST(nativeCoroutineGraphAndListenerCompilation)
{
    BlueprintDocument doc;doc.create();std::string error,source,bytes;
    NX_EXPECT(doc.addFunction("Later",error));const auto co=doc.graphs.size()-1;
    NX_EXPECT(doc.editFunctionSignature(co,"coroutine()",error));
    NX_EXPECT(doc.addNode(co,"async:wait",0,0,error)&&doc.connect(co,0,"then",1,"in",error));
    NX_EXPECT(doc.addConnectedNode(co,1,"then","async:next",0,0,error));
    NX_EXPECT(!doc.addNode(0,"async:wait",0,0,error));
    NX_EXPECT(doc.addNode(0,"async:start",0,0,error,"Later")&&doc.connect(0,0,"then",1,"in",error));
    NX_EXPECT(doc.addFunction("Signal",error)&&doc.addFunction("Receiver",error));
    NX_EXPECT(doc.addNode(0,"dispatch:bind-listener",0,0,error,"Signal")&&doc.connect(0,1,"then",2,"in",error)&&doc.setInputValue(0,2,"handler","Receiver",error));
    NX_EXPECT(doc.addNode(0,"dispatch:broadcast",0,0,error,"Signal")&&doc.connect(0,2,"then",3,"in",error));
    NX_EXPECT(doc.serialize(bytes,error));BlueprintDocument copy;NX_EXPECT(copy.load(bytes.data(),bytes.size(),error));
    NX_EXPECT(compileBlueprint(copy,source,error)&&source.find("Coroutine(this.bp_fn_Later)")!=std::string::npos&&source.find("bp_snapshot_0")!=std::string::npos);
}

NX_TEST(nativeSharedLibraryTransferAndTypedVariables)
{
    BlueprintDocument doc;doc.create();std::string error,package,source;
    NX_EXPECT(doc.addFunction("State",error));const auto en=doc.graphs.size()-1;
    NX_EXPECT(doc.editFunctionSignature(en,"enum(Idle, Running)",error));
    NX_EXPECT(doc.addFunction("Shared",error));const auto fn=doc.graphs.size()-1;
    NX_EXPECT(doc.setSharedLibrary(fn,true,error)&&doc.exportLibrary(package,error));
    BlueprintDocument other;other.create();
    NX_EXPECT(other.importLibrary(package.data(),package.size(),"Screen",error));
    NX_EXPECT(other.libraryFunction("Shared")&&other.dataType("State"));
    NX_EXPECT(!other.importLibrary(package.data(),package.size(),"Screen",error));
    NX_EXPECT(other.editVariableDeclaration("Screen","field state:BP_Data_State",error));
    NX_EXPECT(other.addNode(0,"data:enum-value",0,0,error,"State")&&other.setInputValue(0,1,"member","Running",error));
    NX_EXPECT(other.addVariableNode(0,"state",false,true,0,0,error)&&other.connect(0,0,"then",2,"in",error)&&other.connect(0,1,"result",2,"value",error));
    NX_EXPECT(compileBlueprint(other,source,error)&&source.find("BP_Data_State bp_var_state")!=std::string::npos);
}

NX_TEST(nativeNestedStructureAndEnumSwitchValidation)
{
    BlueprintDocument doc;doc.create();std::string error,source;
    NX_EXPECT(doc.addFunction("State",error));const auto en=doc.graphs.size()-1;
    NX_EXPECT(doc.editFunctionSignature(en,"enum(Idle, Running)",error));
    NX_EXPECT(doc.addFunction("Stats",error));const auto st=doc.graphs.size()-1;
    NX_EXPECT(doc.editFunctionSignature(st,"struct(state:BP_Data_State, health:int)",error));
    NX_EXPECT(!doc.editFunctionSignature(st,"struct(self:BP_Data_Stats)",error));
    NX_EXPECT(doc.addNode(0,"data:enum-value",0,0,error,"State")&&!doc.setInputValue(0,1,"member","Invalid",error));
    NX_EXPECT(doc.setInputValue(0,1,"member","Running",error));
    NX_EXPECT(doc.addNode(0,"data:enum-switch",0,0,error,"State")&&doc.connect(0,0,"then",2,"in",error)&&doc.connect(0,1,"result",2,"value",error));
    NX_EXPECT(doc.addNode(0,"api:Save:Save::set:string,string",0,0,error)&&doc.connect(0,2,"case_Running",3,"in",error)&&doc.setInputValue(0,3,"key","state",error)&&doc.setInputValue(0,3,"value","run",error));
    NX_EXPECT(!doc.editFunctionSignature(en,"enum(Idle, Jumping)",error));
    NX_EXPECT(compileBlueprint(doc,source,error)&&source.find("BP_Data_State::bp_Running")!=std::string::npos);
}


NX_TEST(nativeEditorConstructionChecksVirtualHelpersAndParentCalls)
{
    BlueprintDocument doc;doc.create();std::string base,child,parent,derived,error;
    NX_EXPECT(doc.createVisualAsset("actor",base,error)&&doc.createVisualActorChild(base,child,error));
    NX_EXPECT(doc.ensureActorBehaviour(child,derived,error)&&doc.ensureActorBehaviour(base,parent,error));
    NX_EXPECT(doc.addFunction("Adjust",error,parent)&&doc.addFunction("Construction",error,parent));
    const auto* construction=doc.function(parent,"Construction");if(!construction){NX_EXPECT(false);return;}
    const auto gi=static_cast<size_t>(construction-doc.graphs.data());
    NX_EXPECT(doc.addNode(gi,"call:function",0,0,error,"Adjust")&&doc.connect(gi,0,"then",1,"in",error));
    NX_EXPECT(doc.validateEditorConstruction(parent,error)&&doc.validateEditorConstruction(derived,error));
    NX_EXPECT(doc.addFunction("Construction",error,derived));
    const auto* derivedConstruction=doc.function(derived,"Construction");if(!derivedConstruction){NX_EXPECT(false);return;}
    const auto ci=static_cast<size_t>(derivedConstruction-doc.graphs.data());
    NX_EXPECT(doc.addConnectedNode(ci,0,"then","call:parent",0,0,error));
    NX_EXPECT(doc.validateEditorConstruction(derived,error));
    NX_EXPECT(doc.addFunction("Adjust",error,derived));
    const auto* helper=doc.function(derived,"Adjust");if(!helper){NX_EXPECT(false);return;}
    const auto hi=static_cast<size_t>(helper-doc.graphs.data());
    NX_EXPECT(doc.addNode(hi,"api:Save:Save::set:string,string",0,0,error));
    NX_EXPECT(doc.connect(hi,0,"then",1,"in",error));
    NX_EXPECT(doc.setInputValue(hi,1,"key","test",error)&&doc.setInputValue(hi,1,"value","must not run",error));
    NX_EXPECT(doc.validateEditorConstruction(parent,error));
    NX_EXPECT(!doc.validateEditorConstruction(derived,error)&&error.find("Runtime-only node")!=std::string::npos);
    NX_EXPECT(doc.deleteNode(hi,1,error)&&doc.validateEditorConstruction(derived,error));
}

NX_TEST(nativeEditorConstructionChecksSharedLibraryAndTypedGeometry)
{
    BlueprintDocument doc;doc.create();std::string actor,owner,error;
    NX_EXPECT(doc.createVisualAsset("actor",actor,error)&&doc.ensureActorBehaviour(actor,owner,error));
    NX_EXPECT(doc.addFunction("SharedGeometry",error));
    const auto library=doc.graphs.size()-1;
    NX_EXPECT(doc.setSharedLibrary(library,true,error));
    NX_EXPECT(doc.addFunction("Construction",error,owner));
    const auto* fn=doc.function(owner,"Construction");if(!fn){NX_EXPECT(false);return;}
    const auto gi=static_cast<size_t>(fn-doc.graphs.data());
    NX_EXPECT(doc.addNode(gi,"call:library",0,0,error,"SharedGeometry")&&doc.connect(gi,0,"then",1,"in",error));
    NX_EXPECT(doc.addNode(gi,"event:entity",0,0,error));
    NX_EXPECT(doc.addNode(gi,"api:Entity:set_size:Entity@,Vec3",0,0,error));
    NX_EXPECT(doc.connect(gi,1,"then",3,"in",error)&&doc.connect(gi,2,"value",3,"target",error));
    NX_EXPECT(doc.setInputValue(gi,3,"value","2,3,4",error));
    NX_EXPECT(doc.validateEditorConstruction(owner,error));
    std::string source;NX_EXPECT(compileBlueprint(doc,source,error));
    NX_EXPECT(source.find("bp_lib_SharedGeometry()")!=std::string::npos);
}

NX_TEST(nativeUpstreamThreadChannelAndAudioNodes)
{
    NX_EXPECT(blueprintSpec("api:Window:Window::resize:uint,uint"));
    NX_EXPECT(blueprintSpec("api:System:System::get_processors:"));
    NX_EXPECT(blueprintSpec("api:System:System::get_hour:")&&blueprintSpec("api:System:System::get_minute:")&&blueprintSpec("api:System:System::get_second:")&&blueprintSpec("api:System:System::clock:bool"));
    NX_EXPECT(blueprintSpec("api:AudioStream:AudioStream:uint,uint,float"));
    NX_EXPECT(blueprintSpec("api:Samples:Samples:uint,uint,uint"));
    NX_EXPECT(blueprintSpec("api:Thread:Thread:string"));
    NX_EXPECT(!blueprintSpec("api:Thread:wait:Thread@"));
    NX_EXPECT(!blueprintSpec("api:Channel:demand:Channel@,float"));
    BlueprintDocument doc;doc.create();std::string error,source,bytes;
    NX_EXPECT(doc.editVariableDeclaration("Screen","field channel:Channel@",error));
    NX_EXPECT(doc.addNode(0,"api:Channel:Channel:string",0,0,error));
    NX_EXPECT(doc.setInputValue(0,1,"name","bp-results",error));
    NX_EXPECT(doc.addVariableNode(0,"channel",false,true,0,0,error));
    NX_EXPECT(doc.connect(0,0,"then",1,"in",error));
    NX_EXPECT(doc.connect(0,1,"then",2,"in",error));
    NX_EXPECT(doc.connect(0,1,"result",2,"value",error));
    NX_EXPECT(doc.addVariableNode(1,"channel",false,false,0,0,error));
    NX_EXPECT(doc.addNode(1,"api:Channel:pop:Channel@:string out",0,0,error));
    NX_EXPECT(doc.connect(1,0,"then",2,"in",error));
    NX_EXPECT(doc.connect(1,1,"value",2,"target",error));
    NX_EXPECT(doc.serialize(bytes,error));
    BlueprintDocument copy;NX_EXPECT(copy.load(bytes.data(),bytes.size(),error));
    NX_EXPECT(compileBlueprint(copy,source,error));
    NX_EXPECT(source.find("Channel(\"bp-results\")")!=std::string::npos);
    NX_EXPECT(source.find("@bp_var_channel = ")!=std::string::npos);
    NX_EXPECT(source.find(".pop(")!=std::string::npos);
}

NX_TEST(nativeAudioBusInputsAndSynthesisExport)
{
    BlueprintDocument doc;doc.create();std::string error,source,bytes;
    NX_EXPECT(doc.editVariableDeclaration("Screen","field voice:Voice",error));
    NX_EXPECT(doc.editVariableDeclaration("Screen","field sound:Sound@",error));
    NX_EXPECT(doc.addNode(0,"api:Audio:Audio::distortion:Bus,float,float",0,0,error));
    NX_EXPECT(!doc.setInputValue(0,1,"bus","BusEffects); quit();",error));
    NX_EXPECT(doc.setInputValue(0,1,"bus","BusMusic",error)&&doc.setInputValue(0,1,"amount","0.2",error));
    NX_EXPECT(doc.connect(0,0,"then",1,"in",error));
    NX_EXPECT(doc.addNode(0,"api:Samples:Samples:uint,uint,uint",0,0,error)&&doc.setInputValue(0,2,"frames","64",error));
    NX_EXPECT(doc.addNode(0,"audio:render-tone",0,0,error)&&doc.connect(0,1,"then",2,"in",error)&&doc.connect(0,2,"then",3,"in",error)&&doc.connect(0,2,"result",3,"samples",error));
    NX_EXPECT(doc.addNode(0,"api:Samples:play:Samples@,float,float,float,bool",0,0,error)&&doc.connect(0,3,"then",4,"in",error)&&doc.connect(0,2,"result",4,"target",error));
    NX_EXPECT(doc.addVariableNode(0,"voice",false,true,0,0,error)&&doc.connect(0,4,"then",5,"in",error)&&doc.connect(0,4,"result",5,"value",error));
    NX_EXPECT(doc.addVariableNode(1,"voice",false,false,0,0,error)&&doc.addNode(1,"api:Voice:stop:Voice,float",0,0,error));
    NX_EXPECT(doc.connect(1,0,"then",2,"in",error)&&doc.connect(1,1,"value",2,"target",error));
    NX_EXPECT(doc.serialize(bytes,error));
    BlueprintDocument copy;NX_EXPECT(copy.load(bytes.data(),bytes.size(),error)&&compileBlueprint(copy,source,error));
    NX_EXPECT(source.find("Audio::distortion(BusMusic,")!=std::string::npos);
    NX_EXPECT(source.find("bp_audio_render(")!=std::string::npos&&source.find("float bp_out_3_nextPhase;")!=std::string::npos);
    NX_EXPECT(source.find("Voice bp_var_voice;")!=std::string::npos&&source.find("bp_var_voice = ")!=std::string::npos);
    NX_EXPECT(source.find("@bp_var_voice = ")==std::string::npos);
    BlueprintDocument empty;empty.create();NX_EXPECT(compileBlueprint(empty,source,error)&&source.find("bp_audio_wave(")==std::string::npos);
}

NX_TEST(nativeWideIntegerLiteralsAndLosslessConversions)
{
    BlueprintDocument doc;doc.create();std::string error,source,bytes;
    NX_EXPECT(doc.editVariableDeclaration("Screen","field low:int64=-9223372036854775808",error));
    NX_EXPECT(doc.editVariableDeclaration("Screen","field high:uint64=18446744073709551615",error));
    NX_EXPECT(!doc.editVariableDeclaration("Screen","field bad:uint64=18446744073709551616",error));
    NX_EXPECT(doc.addNode(0,"literal:int64",0,0,error));
    NX_EXPECT(doc.setLiteral(0,1,"9223372036854775807",error));
    NX_EXPECT(!doc.setLiteral(0,1,"9223372036854775808",error));
    NX_EXPECT(!doc.setLiteral(0,1,"-9223372036854775809",error));
    NX_EXPECT(!doc.setLiteral(0,1,"1e9",error));
    NX_EXPECT(doc.addNode(0,"literal:uint64",0,0,error));
    NX_EXPECT(doc.setLiteral(0,2,"18446744073709551615",error));
    NX_EXPECT(!doc.setLiteral(0,2,"18446744073709551616",error));
    NX_EXPECT(!doc.setLiteral(0,2,"-1",error));
    NX_EXPECT(blueprintLosslessConversion("uint","int64"));
    NX_EXPECT(blueprintLosslessConversion("uint","uint64"));
    NX_EXPECT(!blueprintLosslessConversion("int64","double"));
    NX_EXPECT(!blueprintLosslessConversion("int","uint64"));
    NX_EXPECT(blueprintSpec("api:Packed:writeI64:Packed@,int64"));
    NX_EXPECT(blueprintSpec("api:Packed:readI64:Packed@"));
    NX_EXPECT(doc.serialize(bytes,error));
    BlueprintDocument copy;NX_EXPECT(copy.load(bytes.data(),bytes.size(),error)&&compileBlueprint(copy,source,error));
    NX_EXPECT(source.find("(-int64(9223372036854775807)-int64(1))")!=std::string::npos);
    NX_EXPECT(source.find("uint64(18446744073709551615)")!=std::string::npos);
}

NX_TEST(nativeImportUpstreamObjectsAndWideIntegers)
{
    BlueprintDocument doc;std::string error,source,bytes;
    const char* code=R"(class Example : Screen {
        Curve@ path;
        Batch@ batch;
        Packed@ payload;
        int64 counter = int64(9007199254740993);
        void onEnter() {
            @path = Curve();
            path.add(Vec2(0.0f, 0.0f));
            @batch = Batch();
            @payload = Packed();
            payload.writeI64(counter);
            Packed@ copy = Packed(payload.bytes);
            int64 got = copy.readI64();
            if (got == counter) { Save::set("packed", "yes"); }
            @path = null;
        }
    })";
    const bool ok=importBlueprintScreen(code,doc,error);
    if(!ok)NX_ERROR("Upstream object import: %s",error.c_str());
    NX_EXPECT(ok);if(!ok)return;
    NX_EXPECT(doc.serialize(bytes,error)&&compileBlueprint(doc,source,error));
    NX_EXPECT(source.find("int64(9007199254740993)")!=std::string::npos);
    NX_EXPECT(source.find("Curve()")!=std::string::npos&&source.find("Batch()")!=std::string::npos);
    NX_EXPECT(source.find(".readI64()")!=std::string::npos);
    NX_EXPECT(source.find("@bp_var_path = null")!=std::string::npos);
}

NX_TEST(nativePhysicsJointProjectionPreservesOtherAxesAndConnections)
{
    BlueprintDocument doc;doc.create();std::string error;
    const char* joint="api:Space:Space::hinge:Entity@,Entity@,Vec3,Vec3,float,float";
    NX_EXPECT(blueprintPhysicsJoint(joint)&&!blueprintPhysicsJoint("api:Space:Space::raycast:"));
    NX_EXPECT(doc.addNode(0,joint,0,0,error));
    NX_EXPECT(doc.setInputValue(0,1,"at","1,2,3",error));
    NX_EXPECT(doc.setJointProjection(0,1,"at",1,4,5,error));
    const auto value=[&](){for(const auto& v:doc.graphs[0].nodes[1].values)if(v.pin=="at")return v.value;return std::string();};
    NX_EXPECT(value()=="4,2,5");
    NX_EXPECT(doc.undo()&&value()=="1,2,3");
    NX_EXPECT(doc.redo()&&value()=="4,2,5");
    NX_EXPECT(!doc.setJointProjection(0,1,"axis",0,0,0,error));
    NX_EXPECT(doc.addNode(0,"make:Vec3",0,0,error)&&doc.connect(0,2,"result",1,"at",error));
    NX_EXPECT(!doc.setJointProjection(0,1,"at",0,9,9,error));
}
NX_TEST(nativeCubicCurveVisualEditIsAtomic)
{
    BlueprintDocument doc;doc.create();std::string error,source,bytes;
    NX_EXPECT(doc.addNode(0,"curve:cubic",0,0,error)&&doc.connect(0,0,"then",1,"in",error));
    std::vector<std::string> original,edited;
    NX_EXPECT(doc.cubicPoints(0,1,original,error)&&original.size()==4);
    NX_EXPECT(doc.setCubicPoints(0,1,{"0,0","0,100","100,100","100,0"},error));
    NX_EXPECT(doc.cubicPoints(0,1,edited,error)&&edited[1]=="0,100");
    NX_EXPECT(doc.undo()&&doc.cubicPoints(0,1,edited,error)&&edited==original);
    NX_EXPECT(doc.redo()&&doc.cubicPoints(0,1,edited,error)&&edited[2]=="100,100");
    NX_EXPECT(!doc.setCubicPoints(0,1,{"0,0","nan,0","100,100","100,0"},error));
    NX_EXPECT(doc.serialize(bytes,error));
    BlueprintDocument copy;NX_EXPECT(copy.load(bytes.data(),bytes.size(),error)&&compileBlueprint(copy,source,error));
    NX_EXPECT(source.find("bp_curve_cubic(")!=std::string::npos&&source.find("Curve@ c = Curve();")!=std::string::npos);
    NX_EXPECT(copy.addNode(0,"make:Vec2",0,0,error)&&copy.connect(0,2,"result",1,"p0",error));
    NX_EXPECT(!copy.cubicPoints(0,1,edited,error)&&!copy.setCubicPoints(0,1,original,error));
}

NX_TEST(nativeEngineEnumChoicesAreValidatedAndExported)
{
    BlueprintDocument doc;doc.create();std::string error,source,bytes;
    const auto choices=blueprintEnumChoices("TitleLook");
    NX_EXPECT(choices.size()==3&&blueprintEnumValue("TitleLook","TitleDark"));
    NX_EXPECT(!blueprintEnumValue("TitleLook","BackdropMica"));
    NX_EXPECT(blueprintEnumValue("Corners","CornersSmall")&&blueprintEnumValue("Backdrop","BackdropTabbed"));
    NX_EXPECT(blueprintEnumValue("Bus","BusMusic")&&blueprintEnumValue("Align","AlignJustify"));
    NX_EXPECT(blueprintInputLiteralType("Pointer")&&blueprintInputLiteralType("Blend"));
    NX_EXPECT(doc.addNode(0,"api:Windows:Windows::set_titleLook:TitleLook",0,0,error)&&doc.connect(0,0,"then",1,"in",error));
    NX_EXPECT(!doc.setInputValue(0,1,"value","TitleDark; quit()",error));
    NX_EXPECT(!doc.setInputValue(0,1,"value","BackdropMica",error));
    NX_EXPECT(doc.setInputValue(0,1,"value","TitleDark",error));
    NX_EXPECT(doc.addNode(0,"api:Windows:Windows::set_backdrop:Backdrop",0,0,error)&&doc.connect(0,1,"then",2,"in",error));
    NX_EXPECT(doc.setInputValue(0,2,"value","BackdropMica",error));
    NX_EXPECT(doc.serialize(bytes,error));
    BlueprintDocument copy;NX_EXPECT(copy.load(bytes.data(),bytes.size(),error)&&compileBlueprint(copy,source,error));
    NX_EXPECT(source.find("Windows::titleLook = TitleDark")!=std::string::npos);
    NX_EXPECT(source.find("Windows::backdrop = BackdropMica")!=std::string::npos);
}

NX_TEST(nativeImportWindowsFrameEnumProperties)
{
    BlueprintDocument doc;std::string error,source;
    const bool imported=importBlueprintScreen(R"(class Frame : Screen {
        void onEnter() {
            if (Windows::titleColors) {
                Windows::titleLook = TitleDark;
                Windows::corners = CornersSmall;
            }
            if (Windows::backdrops) { Windows::backdrop = BackdropMica; }
            Window::pointer = PointerHand;
        }
    })",doc,error);
    if(!imported)NX_ERROR("Frame enum import: %s",error.c_str());
    NX_EXPECT(imported);if(!imported)return;
    NX_EXPECT(compileBlueprint(doc,source,error));
    NX_EXPECT(source.find("Windows::titleLook = TitleDark")!=std::string::npos);
    NX_EXPECT(source.find("Windows::corners = CornersSmall")!=std::string::npos);
    NX_EXPECT(source.find("Windows::backdrop = BackdropMica")!=std::string::npos);
    NX_EXPECT(source.find("Window::pointer = PointerHand")!=std::string::npos);
}

NX_TEST(nativeProjectTargetProfileRoundTrip)
{
    BlueprintDocument doc;doc.create();std::string error,source;
    NX_EXPECT(doc.target().xbox()&&doc.target().ram==64&&doc.target().width()==640&&doc.target().height()==480);
    NX_EXPECT(doc.target().surfaceBytes()==3686400u);
    auto target=doc.target();target.ram=256;target.resolution=3;
    NX_EXPECT(doc.setTarget(target)&&doc.dirty()&&doc.target().highResolutionWarning());
    NX_EXPECT(doc.undo()&&doc.target().ram==64&&!doc.dirty());
    NX_EXPECT(doc.redo()&&doc.target()==target);
    NX_EXPECT(doc.serialize(source,error));BlueprintDocument copy;
    NX_EXPECT(copy.load(source.data(),source.size(),error)&&copy.target()==target);
    target.ram=512;NX_EXPECT(!copy.setTarget(target));
    target.ram=128;target.resolution=1;NX_EXPECT(copy.setTarget(target)&&copy.target().pixelAspect()>1.f);
    target.platform=5;NX_EXPECT(copy.setTarget(target)&&!copy.target().xbox());
}

NX_TEST(nativeAdvancedWidgetSlotPropertiesAndDesignMetadata)
{
    BlueprintDocument doc;doc.create();std::string id,error,source,page;
    auto target=doc.target();target.resolution=3;NX_EXPECT(doc.setTarget(target));
    NX_EXPECT(doc.createVisualAsset("widget",id,error)&&doc.appendVisualWidget(id,"row","",error));
    NX_EXPECT(doc.appendVisualWidget(id,"progress","",error,"item-1"));
    NX_EXPECT(doc.setVisualWidgetProperty(id,"item-2","grow","3",error));
    NX_EXPECT(doc.setVisualWidgetProperty(id,"item-2","padding","1,2,3,4",error));
    NX_EXPECT(doc.setVisualWidgetProperty(id,"item-2","maxWidth","280",error));
    NX_EXPECT(doc.setVisualWidgetProperty(id,"item-2","high","150",error));
    NX_EXPECT(!doc.setVisualWidgetProperty(id,"item-2","padding","1,2,3",error));
    NX_EXPECT(!doc.setVisualWidgetProperty(id,"item-2","columns","2.5",error));
    NX_EXPECT(doc.visualWidgetPage(id,page,error)&&doc.connectVisualWidget("Screen",id,error)&&compileBlueprint(doc,source,error));
    JsonTree tree;NX_EXPECT(tree.parse(page.data(),page.size()));
    const auto item=tree.root()[0u]["row"][0u];
    NX_EXPECT(item["padding"].count()==4&&item["padding"][3u].number()==4&&item["grow"].number()==3);
    NX_EXPECT(source.find("designResolution")!=std::string::npos);
    NX_EXPECT(source.find("Ui::draw(canvas)")!=std::string::npos);
}

NX_TEST(nativeDebugSourceFingerprint)
{
    NX_EXPECT(scriptFingerprint("",0)=="cbf29ce484222325");
    NX_EXPECT(scriptFingerprint("a",1)=="af63dc4c8601ec8c");
    NX_EXPECT(scriptFingerprint("a\n",2)!=scriptFingerprint("a\r\n",3));
    NX_EXPECT(scriptFingerprint("abc",3)==scriptFingerprint("abcd",3));
}

NX_TEST(nativeWidgetGridSlotAndHeightProperties)
{
    BlueprintDocument doc;doc.create();std::string page,error,bytes;
    NX_EXPECT(doc.createVisualAsset("widget",page,error)&&doc.appendVisualWidget(page,"grid","",error));
    NX_EXPECT(doc.appendVisualWidget(page,"label","Wide",error,"item-1"));
    NX_EXPECT(doc.setVisualWidgetProperty(page,"item-2","gridSlot","1,2,2,3",error));
    NX_EXPECT(doc.setVisualWidgetProperty(page,"item-2","minHeight","24",error)&&doc.setVisualWidgetProperty(page,"item-2","maxHeight","100",error));
    const auto revision=doc.revision();
    NX_EXPECT(!doc.setVisualWidgetProperty(page,"item-2","gridSlot","0,63,1,2",error));
    NX_EXPECT(!doc.setVisualWidgetProperty(page,"item-2","gridSlot","0,0,0,1",error));
    NX_EXPECT(!doc.setVisualWidgetProperty(page,"item-1","gridSlot","0,0,1,1",error));
    NX_EXPECT(!doc.reparentVisualWidget(page,"item-2","",error)&&doc.revision()==revision);
    NX_EXPECT(doc.serialize(bytes,error));BlueprintDocument copy;
    NX_EXPECT(copy.load(bytes.data(),bytes.size(),error)&&copy.visualAsset(page)->source==doc.visualAsset(page)->source);
    NX_EXPECT(copy.setVisualWidgetProperty(page,"item-2","gridSlot","",error)&&copy.reparentVisualWidget(page,"item-2","",error));
}

NX_TEST(nativeSizeBoxSingleChild)
{
    BlueprintDocument doc;doc.create();std::string page,error,bytes;
    NX_EXPECT(doc.createVisualAsset("widget",page,error)&&doc.appendVisualWidget(page,"sizeBox","",error));
    NX_EXPECT(doc.appendVisualWidget(page,"label","Child",error,"item-1"));
    NX_EXPECT(!doc.appendVisualWidget(page,"label","Second",error,"item-1"));
    NX_EXPECT(doc.appendVisualWidget(page,"label","Outside",error));
    NX_EXPECT(!doc.reparentVisualWidget(page,"item-3","item-1",error));
    NX_EXPECT(doc.setVisualWidgetProperty(page,"item-1","width","100",error)&&doc.setVisualWidgetProperty(page,"item-1","minHeight","40",error));
    NX_EXPECT(doc.serialize(bytes,error));BlueprintDocument copy;
    NX_EXPECT(copy.load(bytes.data(),bytes.size(),error)&&copy.visualAsset(page)->source==doc.visualAsset(page)->source);
}

NX_TEST(nativeWidgetNavigationAndAutoplayPersist)
{
    BlueprintDocument doc;doc.create();std::string page,error,bytes;
    NX_EXPECT(doc.createVisualAsset("widget",page,error)&&doc.appendVisualWidget(page,"button","First",error));
    NX_EXPECT(doc.appendVisualWidget(page,"button","Second",error));
    NX_EXPECT(doc.setVisualWidgetProperty(page,"item-1","navDown","item-2",error));
    NX_EXPECT(doc.setVisualWidgetProperty(page,"item-2","navUp","@stop",error));
    NX_EXPECT(doc.setWidgetKey(page,"item-1","autoPlay","false",0,error));
    NX_EXPECT(doc.setWidgetKey(page,"item-1","x","0",0,error)&&doc.setWidgetKey(page,"item-1","x","100",1,error));
    NX_EXPECT(doc.serialize(bytes,error));BlueprintDocument copy;
    NX_EXPECT(copy.load(bytes.data(),bytes.size(),error)&&copy.visualAsset(page)->source==doc.visualAsset(page)->source);
    NX_EXPECT(copy.setVisualWidgetProperty(page,"item-2","navUp","",error));
    NX_EXPECT(copy.undo()&&copy.visualAsset(page)->source==doc.visualAsset(page)->source);
}
NX_TEST(nativeStudioLaunchPathValidation)
{
    NX_EXPECT(studioLaunchPath("/E/Games/Preview")=="E:\\Games\\Preview\\default.xbe");
    NX_EXPECT(studioLaunchPath("/F/Games/Folder/Project 2")=="F:\\Games\\Folder\\Project 2\\default.xbe");
    NX_EXPECT(studioLaunchPath("/C/Games/Preview").empty());
    NX_EXPECT(studioLaunchPath("/E/Games/../Other").empty());
    NX_EXPECT(studioLaunchPath("/E/Games/Preview/").empty());
    NX_EXPECT(studioLaunchPath("/E/Games/Preview\\Other").empty());
    NX_EXPECT(studioLaunchPath("/E/Games/Preview:Other").empty());
    NX_EXPECT(studioLaunchPath("/G/Games/"+std::string(43,'a')).empty());
    NX_EXPECT(studioLaunchIdentity(std::string(32,'a')));
    NX_EXPECT(!studioLaunchIdentity(std::string(32,'z')));
}


NX_TEST(nativeScaleBoxDocumentAndMode)
{
    BlueprintDocument doc;doc.create();std::string page,error,bytes;
    NX_EXPECT(doc.createVisualAsset("widget",page,error)&&doc.appendVisualWidget(page,"scaleBox","",error));
    NX_EXPECT(doc.appendVisualWidget(page,"button","Child",error,"item-1"));
    NX_EXPECT(!doc.appendVisualWidget(page,"button","Second",error,"item-1"));
    NX_EXPECT(doc.setVisualWidgetProperty(page,"item-1","scaleMode","down-only",error));
    NX_EXPECT(!doc.setVisualWidgetProperty(page,"item-1","scaleMode","invalid",error));
    NX_EXPECT(!doc.setVisualWidgetProperty(page,"item-2","scaleMode","fit",error));
    NX_EXPECT(doc.serialize(bytes,error));BlueprintDocument copy;
    NX_EXPECT(copy.load(bytes.data(),bytes.size(),error)&&copy.visualAsset(page)->source==doc.visualAsset(page)->source);
}

NX_TEST(nativeLiveSourceTransfer)
{
    StudioLiveSource transfer;
    const std::string code="abc\n";
    const auto hash=scriptFingerprint(code.data(),code.size());
    NX_EXPECT(transfer.begin("Screen",hash,hash,code.size()));
    NX_EXPECT(transfer.append(0,"6162")&&transfer.append(2,"630a"));
    NX_EXPECT(transfer.commit()&&transfer.ready&&transfer.source==code);
    transfer.clear();
    NX_EXPECT(!transfer.begin("../bad",hash,hash,code.size()));
    NX_EXPECT(!transfer.begin("Screen",hash,hash,StudioLiveSource::limit+1));
    NX_EXPECT(transfer.begin("Screen",hash,hash,code.size())&&!transfer.append(1,"61"));
    NX_EXPECT(transfer.begin("Screen",hash,hash,code.size())&&!transfer.append(0,"00"));
    NX_EXPECT(transfer.begin("Screen",hash,hash,code.size())&&transfer.append(0,"61626362")&&!transfer.commit());
}

NX_TEST(nativeStudioActorValueBounds)
{
    NX_EXPECT(studioActorValue("position","-12.5, 8",false));
    NX_EXPECT(studioActorValue("place","1, 2, 3",true));
    NX_EXPECT(studioActorValue("color","0.1, 0.2, 0.3, 1",true));
    NX_EXPECT(!studioActorValue("place","1, 2, nan",true));
    NX_EXPECT(!studioActorValue("place","1, 2, 3 junk",true));
    NX_EXPECT(!studioActorValue("place","1, 2, 3, 4",true));
    NX_EXPECT(!studioActorValue("position","1e20,2",false));
    NX_EXPECT(!studioActorValue("size","0,1,1",true));
    NX_EXPECT(!studioActorValue("mass","-1",true));
    NX_EXPECT(!studioActorValue("shown","yes",false));
    NX_EXPECT(!studioActorValue("name","renamed",false));
}

NX_TEST(nativePausedPinLocalMapping)
{
    BlueprintDocument doc;doc.create();
    std::string error;
    NX_EXPECT(doc.addNode(0,"spawn:box",200,120,error));
    NX_EXPECT(blueprintPausedPinLocal(doc,0,1,"result")=="bp_value_1");
    NX_EXPECT(blueprintPausedPinLocal(doc,0,1,"then").empty());
    NX_EXPECT(blueprintPausedPinLocal(doc,0,999,"result").empty());
    doc.graphs[0].inlineMacro=true;
    NX_EXPECT(blueprintPausedPinLocal(doc,0,1,"result").empty());
}

NX_TEST(nativeStudioPreviewCodec)
{
    const u16 pixels[]={studioRgb565(255,0,0),studioRgb565(0,255,0),studioRgb565(0,0,255)};
    const auto wire=studioPreviewEncode(pixels,3,1);
    NX_EXPECT(wire=="f80007e0001f");
    std::vector<u8> rgba;
    NX_EXPECT(studioPreviewDecode(wire,3,1,rgba)&&rgba.size()==12);
    NX_EXPECT(rgba[0]==255&&rgba[1]==0&&rgba[3]==255&&rgba[5]==255&&rgba[10]==255);
    NX_EXPECT(!studioPreviewDecode("garbage",3,1,rgba)&&rgba.size()==12);
    NX_EXPECT(!studioPreviewDecode(wire,129,1,rgba));
}
