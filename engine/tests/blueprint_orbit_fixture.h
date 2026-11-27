#pragma once
#include "tools/blueprint/native_document.h"

// Identical to examples/camera-orbit.bp.json; CI checks the source fixture.
namespace nx::blueprint_orbit
{
    inline constexpr char project[]=R"BP_ORBIT({
  "version": 2,
  "kind": "NativeBlueprint",
  "name": "Camera Orbit",
  "graphs": [
    {
      "owner": "Screen",
      "name": "onEnter",
      "kind": "event",
      "nodes": [
        {
          "id": "entry",
          "kind": "entry",
          "x": 24,
          "y": 60
        },
        {
          "id": "floor",
          "kind": "spawn:plane",
          "x": 285,
          "y": 60,
          "values": {
            "name": "Floor"
          }
        },
        {
          "id": "cube",
          "kind": "spawn:box",
          "x": 565,
          "y": 60,
          "values": {
            "name": "Cube"
          }
        }
      ],
      "links": [
        {
          "from": "entry",
          "to": "floor",
          "output": "then",
          "input": "in"
        },
        {
          "from": "floor",
          "to": "cube",
          "output": "then",
          "input": "in"
        }
      ],
      "fields": [
        {
          "name": "elapsed",
          "type": "float",
          "value": "0"
        }
      ]
    },
    {
      "owner": "Screen",
      "name": "update",
      "kind": "event",
      "nodes": [
        {
          "id": "entry",
          "kind": "entry",
          "x": 24,
          "y": 60
        },
        {
          "id": "elapsed",
          "kind": "var:get:float",
          "x": 24,
          "y": 205,
          "value": "elapsed"
        },
        {
          "id": "dt",
          "kind": "event:delta",
          "x": 24,
          "y": 310
        },
        {
          "id": "add",
          "kind": "add:float",
          "x": 275,
          "y": 205
        },
        {
          "id": "limit",
          "kind": "api:Maths:min:float,float",
          "x": 525,
          "y": 205,
          "values": {
            "b": "8"
          }
        },
        {
          "id": "store",
          "kind": "var:set:float",
          "x": 775,
          "y": 60,
          "value": "elapsed"
        }
      ],
      "links": [
        {
          "from": "entry",
          "to": "store",
          "output": "then",
          "input": "in"
        },
        {
          "from": "elapsed",
          "to": "add",
          "output": "value",
          "input": "a"
        },
        {
          "from": "dt",
          "to": "add",
          "output": "value",
          "input": "b"
        },
        {
          "from": "add",
          "to": "limit",
          "output": "result",
          "input": "a"
        },
        {
          "from": "limit",
          "to": "store",
          "output": "result",
          "input": "value"
        }
      ]
    },
    {
      "owner": "Screen",
      "name": "draw",
      "kind": "event",
      "nodes": [
        {
          "id": "entry",
          "kind": "entry",
          "x": 24,
          "y": 60
        },
        {
          "id": "elapsed",
          "kind": "var:get:float",
          "x": 24,
          "y": 205,
          "value": "elapsed"
        },
        {
          "id": "divide",
          "kind": "divide:float",
          "x": 275,
          "y": 205,
          "values": {
            "b": "8"
          }
        },
        {
          "id": "center",
          "kind": "make:Vec3",
          "x": 24,
          "y": 340,
          "values": {
            "x": "0",
            "y": "0.5",
            "z": "0"
          }
        },
        {
          "id": "orbit",
          "kind": "math:orbit-position",
          "x": 525,
          "y": 205,
          "values": {
            "radius": "6",
            "height": "2"
          }
        },
        {
          "id": "camera",
          "kind": "api:Space:Space::camera:Vec3,Vec3,float",
          "x": 775,
          "y": 60,
          "values": {
            "angle": "60"
          }
        },
        {
          "id": "canvas",
          "kind": "event:canvas",
          "x": 775,
          "y": 345
        },
        {
          "id": "render",
          "kind": "api:Space:Space::draw:Canvas@",
          "x": 1025,
          "y": 60
        }
      ],
      "links": [
        {
          "from": "entry",
          "to": "camera",
          "output": "then",
          "input": "in"
        },
        {
          "from": "camera",
          "to": "render",
          "output": "then",
          "input": "in"
        },
        {
          "from": "elapsed",
          "to": "divide",
          "output": "value",
          "input": "a"
        },
        {
          "from": "divide",
          "to": "orbit",
          "output": "result",
          "input": "turns"
        },
        {
          "from": "center",
          "to": "orbit",
          "output": "result",
          "input": "center"
        },
        {
          "from": "center",
          "to": "camera",
          "output": "result",
          "input": "to"
        },
        {
          "from": "orbit",
          "to": "camera",
          "output": "result",
          "input": "from"
        },
        {
          "from": "canvas",
          "to": "render",
          "output": "value",
          "input": "canvas"
        }
      ]
    },
    {
      "owner": "Screen",
      "name": "onSuspend",
      "kind": "event",
      "nodes": [
        {
          "id": "entry",
          "kind": "entry",
          "x": 24,
          "y": 60
        }
      ],
      "links": []
    },
    {
      "owner": "Screen",
      "name": "onResume",
      "kind": "event",
      "nodes": [
        {
          "id": "entry",
          "kind": "entry",
          "x": 24,
          "y": 60
        }
      ],
      "links": []
    },
    {
      "owner": "Screen",
      "name": "onExit",
      "kind": "event",
      "nodes": [
        {
          "id": "entry",
          "kind": "entry",
          "x": 24,
          "y": 60
        }
      ],
      "links": []
    }
  ]
}
)BP_ORBIT";
    inline constexpr char timelineProject[]=R"BP_TIME({
  "version": 2,
  "kind": "NativeBlueprint",
  "name": "Camera Orbit Timeline",
  "graphs": [
    {
      "owner": "Screen",
      "name": "onEnter",
      "kind": "event",
      "nodes": [
        {
          "id": "entry",
          "kind": "entry",
          "x": 24,
          "y": 60
        },
        {
          "id": "floor",
          "kind": "spawn:plane",
          "x": 285,
          "y": 60,
          "values": {
            "name": "Floor"
          }
        },
        {
          "id": "cube",
          "kind": "spawn:box",
          "x": 565,
          "y": 60,
          "values": {
            "name": "Cube"
          }
        }
      ],
      "links": [
        {
          "from": "entry",
          "to": "floor",
          "output": "then",
          "input": "in"
        },
        {
          "from": "floor",
          "to": "cube",
          "output": "then",
          "input": "in"
        }
      ]
    },
    {
      "owner": "Screen",
      "name": "update",
      "kind": "event",
      "nodes": [
        {
          "id": "entry",
          "kind": "entry",
          "x": 24,
          "y": 60
        },
        {
          "id": "timeline",
          "kind": "time:timeline",
          "x": 285,
          "y": 60,
          "values": {
            "duration": "8",
            "loop": "false"
          }
        },
        {
          "id": "center",
          "kind": "make:Vec3",
          "x": 24,
          "y": 245,
          "values": {
            "x": "0",
            "y": "0.5",
            "z": "0"
          }
        },
        {
          "id": "orbit",
          "kind": "math:orbit-position",
          "x": 550,
          "y": 245,
          "values": {
            "radius": "6",
            "height": "2"
          }
        },
        {
          "id": "camera",
          "kind": "api:Space:Space::camera:Vec3,Vec3,float",
          "x": 810,
          "y": 60,
          "values": {
            "angle": "60"
          }
        }
      ],
      "links": [
        {
          "from": "entry",
          "to": "timeline",
          "output": "then",
          "input": "in"
        },
        {
          "from": "timeline",
          "to": "camera",
          "output": "then",
          "input": "in"
        },
        {
          "from": "timeline",
          "to": "orbit",
          "output": "progress",
          "input": "turns"
        },
        {
          "from": "center",
          "to": "orbit",
          "output": "result",
          "input": "center"
        },
        {
          "from": "center",
          "to": "camera",
          "output": "result",
          "input": "to"
        },
        {
          "from": "orbit",
          "to": "camera",
          "output": "result",
          "input": "from"
        }
      ]
    },
    {
      "owner": "Screen",
      "name": "draw",
      "kind": "event",
      "nodes": [
        {
          "id": "entry",
          "kind": "entry",
          "x": 24,
          "y": 60
        },
        {
          "id": "canvas",
          "kind": "event:canvas",
          "x": 24,
          "y": 200
        },
        {
          "id": "render",
          "kind": "api:Space:Space::draw:Canvas@",
          "x": 300,
          "y": 60
        }
      ],
      "links": [
        {
          "from": "entry",
          "to": "render",
          "output": "then",
          "input": "in"
        },
        {
          "from": "canvas",
          "to": "render",
          "output": "value",
          "input": "canvas"
        }
      ]
    },
    {
      "owner": "Screen",
      "name": "onSuspend",
      "kind": "event",
      "nodes": [
        {
          "id": "entry",
          "kind": "entry",
          "x": 24,
          "y": 60
        }
      ],
      "links": []
    },
    {
      "owner": "Screen",
      "name": "onResume",
      "kind": "event",
      "nodes": [
        {
          "id": "entry",
          "kind": "entry",
          "x": 24,
          "y": 60
        }
      ],
      "links": []
    },
    {
      "owner": "Screen",
      "name": "onExit",
      "kind": "event",
      "nodes": [
        {
          "id": "entry",
          "kind": "entry",
          "x": 24,
          "y": 60
        }
      ],
      "links": []
    }
  ]
}
)BP_TIME";
    inline constexpr char timedProject[]=R"BP_TIMED({
  "version": 2,
  "kind": "NativeBlueprint",
  "name": "Camera Orbit - Timed Action",
  "graphs": [
    {
      "owner": "Screen",
      "name": "onEnter",
      "kind": "event",
      "nodes": [
        {
          "id": "entry",
          "kind": "entry",
          "x": 24,
          "y": 60
        },
        {
          "id": "floor",
          "kind": "spawn:plane",
          "x": 285,
          "y": 60,
          "values": {
            "name": "Floor"
          }
        },
        {
          "id": "cube",
          "kind": "spawn:box",
          "x": 565,
          "y": 60,
          "values": {
            "name": "Cube"
          }
        }
      ],
      "links": [
        {
          "from": "entry",
          "to": "floor",
          "output": "then",
          "input": "in"
        },
        {
          "from": "floor",
          "to": "cube",
          "output": "then",
          "input": "in"
        }
      ]
    },
    {
      "owner": "Screen",
      "name": "update",
      "kind": "event",
      "nodes": [
        {
          "id": "entry",
          "kind": "entry",
          "x": 24,
          "y": 60
        },
        {
          "id": "orbit",
          "kind": "camera:orbit-timed",
          "x": 300,
          "y": 60,
          "values": {
            "name": "Cube",
            "duration": "8"
          }
        }
      ],
      "links": [
        {
          "from": "entry",
          "to": "orbit",
          "output": "then",
          "input": "in"
        }
      ]
    },
    {
      "owner": "Screen",
      "name": "draw",
      "kind": "event",
      "nodes": [
        {
          "id": "entry",
          "kind": "entry",
          "x": 24,
          "y": 60
        },
        {
          "id": "render",
          "kind": "render:space",
          "x": 300,
          "y": 60
        }
      ],
      "links": [
        {
          "from": "entry",
          "to": "render",
          "output": "then",
          "input": "in"
        }
      ]
    },
    {
      "owner": "Screen",
      "name": "onSuspend",
      "kind": "event",
      "nodes": [
        {
          "id": "entry",
          "kind": "entry",
          "x": 24,
          "y": 60
        }
      ],
      "links": []
    },
    {
      "owner": "Screen",
      "name": "onResume",
      "kind": "event",
      "nodes": [
        {
          "id": "entry",
          "kind": "entry",
          "x": 24,
          "y": 60
        }
      ],
      "links": []
    },
    {
      "owner": "Screen",
      "name": "onExit",
      "kind": "event",
      "nodes": [
        {
          "id": "entry",
          "kind": "entry",
          "x": 24,
          "y": 60
        }
      ],
      "links": []
    }
  ]
}
)BP_TIMED";
    // Rebuild through the same document operations used by native Studio. This
    // intentionally does not reverse-import handwritten AngelScript.
    inline bool author(BlueprintDocument& result,std::string& error,int variant=0)
    {
        BlueprintDocument expected;
        if(!expected.load(variant==2?timedProject:variant==1?timelineProject:project,variant==2?sizeof(timedProject)-1:variant==1?sizeof(timelineProject)-1:sizeof(project)-1,error))return false;
        BlueprintDocument doc;doc.create();doc.name=expected.name;
        if(!variant&&!doc.editVariableDeclaration("Screen","field elapsed:float=0",error))return false;
        for(size_t g=0;g<expected.graphs.size();++g)
        {
            const auto& graph=expected.graphs[g];
            for(size_t i=1;i<graph.nodes.size();++i)
            {
                const auto& n=graph.nodes[i];
                if(!doc.addNode(g,n.kind,n.x,n.y,error))return false;
                if(!n.ref.empty()&&!doc.setReference(g,i,n.ref,error))return false;
                for(const auto& v:n.values)if(!doc.setInputValue(g,i,v.pin,v.value,error))return false;
            }
            for(const auto& l:graph.links)if(!doc.connect(g,l.from,l.output,l.to,l.input,error))return false;
        }
        std::string checked;
        if(!doc.serialize(checked,error)||!result.load(checked.data(),checked.size(),error))return false;
        return true;
    }
}
