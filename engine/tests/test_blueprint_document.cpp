#include "test.h"
#include "tools/blueprint/native_document.h"
#include <cstring>

using namespace nx;
namespace
{
    const char graph[] = R"JSON({"version":1,"name":"BP_Test","kind":"Screen","graphs":[{"kind":"event","name":"onEnter","nodes":[{"id":"a","kind":"entry","x":20,"y":40},{"id":"b","kind":"api:Save:Save::set:string,string","x":300,"y":40}],"links":[{"from":{"node":"a","pin":"then"},"to":{"node":"b","pin":"in"}}]}]})JSON";
}
NX_TEST(nativeBlueprintLoadsAndKeepsLinks)
{
    BlueprintDocument doc;
    std::string error;
    NX_EXPECT(doc.load(graph,strlen(graph),error));
    NX_EXPECT(doc.name=="BP_Test" && doc.graphs.size()==1);
    if(doc.graphs.size()!=1)return;
    const auto& g=doc.graphs[0];
    NX_EXPECT(g.nodes.size()==2 && g.links.size()==1);
    if(g.nodes.size()!=2||g.links.size()!=1)return;
    NX_EXPECT(g.nodes[1].x==300 && g.links[0].from==0 && g.links[0].to==1 && g.links[0].output=="then");
    const std::string project=std::string("{\"version\":1,\"kind\":\"BlueprintProject\",\"name\":\"Project\",\"screens\":[{\"id\":\"first\",\"blueprint\":")+graph+"},{\"id\":\"second\",\"blueprint\":"+graph+"}]}";
    NX_EXPECT(doc.load(project.data(),project.size(),error));
    NX_EXPECT(doc.name=="Project" && doc.graphs.size()==2);
    if(doc.graphs.size()==2)NX_EXPECT(doc.graphs[1].owner=="second");
}
NX_TEST(nativeBlueprintRejectsBrokenImportsWithoutReplacingDocument)
{
    BlueprintDocument doc;
    std::string error;
    NX_EXPECT(doc.load(graph,strlen(graph),error));
    for(const char* bad:{"{","{\"version\":2,\"name\":\"Future\"}","{\"version\":1,\"name\":\"Wrong\",\"kind\":\"Screen\",\"graphs\":[]}"})
    {
        NX_EXPECT(!doc.load(bad,strlen(bad),error) && !error.empty());
        NX_EXPECT(doc.name=="BP_Test" && doc.graphs.size()==1);
    }
    std::string bad=graph;
    const auto at=bad.find("\"node\":\"b\"");
    if(at!=std::string::npos)bad.replace(at,10,"\"node\":\"z\"");
    NX_EXPECT(!doc.load(bad.data(),bad.size(),error));
    NX_EXPECT(doc.name=="BP_Test" && doc.graphs.size()==1);
}

NX_TEST(nativeBlueprintMovesOnlyCoordinateTokens)
{
    const std::string original=R"JSON({"version":1,"name":"BP_Test","kind":"Screen","future":{"escaped":"\uD83D\uDE80","precise":1.234567890123456789},"graphs":[{"kind":"event","name":"onEnter","nodes":[{"id":"a","kind":"entry","x":2e1,"y":40.0000,"defaults":{"x":20,"y":40}}],"links":[]}]})JSON";
    BlueprintDocument doc;
    std::string error, result;
    NX_EXPECT(doc.load(original.data(),original.size(),error));
    NX_EXPECT(doc.serialize(result,error) && result==original && !doc.dirty());
    NX_EXPECT(doc.move(0,0,125.5f,-2.25f) && doc.dirty() && doc.canUndo());
    std::string expected=original;
    expected.replace(expected.find("2e1"),3,"125.5");
    expected.replace(expected.find("40.0000"),7,"-2.25");
    NX_EXPECT(doc.serialize(result,error) && result==expected);
    doc.markSaved();
    NX_EXPECT(!doc.dirty() && doc.undo() && doc.dirty());
    NX_EXPECT(doc.serialize(result,error) && result==original);
    NX_EXPECT(doc.redo() && !doc.dirty());
    NX_EXPECT(doc.undo() && doc.move(0,0,3,4) && !doc.canRedo() && doc.dirty());
    NX_EXPECT(!doc.move(99,0,0,0) && !doc.move(0,0,2000000,0));
}
NX_TEST(nativeBlueprintPreservesNestedComponentsAndProjectAssets)
{
    std::string root=graph;
    root.pop_back();
    root+=",\"components\":["+std::string(graph)+"],\"future\":true}";
    const std::string project="{\"version\":1,\"kind\":\"BlueprintProject\",\"name\":\"Game\",\"screens\":[{\"id\":\"one\",\"blueprint\":"+root+",\"assets\":[{\"path\":\"asset.bin\",\"data\":\"AAECAw==\"}]}]}";
    BlueprintDocument doc;
    std::string error, result;
    NX_EXPECT(doc.load(project.data(),project.size(),error) && doc.graphs.size()==2);
    if(doc.graphs.size()!=2)return;
    NX_EXPECT(doc.isProject() && doc.move(1,0,81,40));
    std::string expected=project;
    const auto at=expected.rfind("\"x\":20");
    expected.replace(at,6,"\"x\":81");
    NX_EXPECT(doc.serialize(result,error) && result==expected);
    std::string duplicate=graph;
    duplicate.replace(duplicate.find("\"x\":20"),6,"\"x\":20,\"x\":21");
    NX_EXPECT(!doc.load(duplicate.data(),duplicate.size(),error) && doc.dirty());
}
