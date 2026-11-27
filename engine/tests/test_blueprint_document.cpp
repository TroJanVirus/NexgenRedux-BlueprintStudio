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
