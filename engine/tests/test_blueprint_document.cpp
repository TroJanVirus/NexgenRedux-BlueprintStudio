#include "test.h"
#include "tools/blueprint/native_document.h"
#include "tools/blueprint/native_compile.h"
#include <cstring>

using namespace nx;
NX_TEST(nativeCatalogueContainsReferenceAPI)
{
    NX_EXPECT(blueprintCatalogue().size()==842);
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
