#include "test.h"
#include "tools/blueprint/native_document.h"
#include "tools/blueprint/native_compile.h"
#include "tools/blueprint/native_import.h"
#include <cstring>
#include <algorithm>

using namespace nx;
NX_TEST(nativeCatalogueContainsReferenceAPI)
{
    NX_EXPECT(blueprintCatalogue().size()==961);
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
    NX_EXPECT(operators==65);
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
    // Over 2,000 editable nodes distributed over the native event graphs.
    for(size_t graph=0;graph<4;++graph)
    {
        for(size_t i=0;i<512;++i)
            if(!doc.addNode(graph,"literal:int",static_cast<float>(i%16)*252.f,
                static_cast<float>(i/16)*120.f,error)){NX_EXPECT(false);return;}
        NX_EXPECT(doc.setLiteral(graph,512,"42",error));
    }
    // Include real connected execution in the workload, not only disconnected data.
    for(size_t i=0;i<32;++i)
    {
        const size_t n=doc.graphs[0].nodes.size();
        if(!doc.addNode(0,"api:Save:Save::set:string,string",360.f,static_cast<float>(i)*120.f,error)||
            !doc.setInputValue(0,n,"key","stress"+std::to_string(i),error)||
            !doc.setInputValue(0,n,"value","ok",error)||
            !doc.connect(0,i?n-1:0,"then",n,"in",error)){NX_EXPECT(false);return;}
    }
    NX_EXPECT(doc.serialize(saved,error)&&compileBlueprint(doc,source,error));
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
        class Example : Screen {int unused;int score=-4;string label="initial";float speed=1.5f;bool flag=true;uint count=3u;double precise=1.25;void onEnter(){Save::set("label",label);}})";
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
