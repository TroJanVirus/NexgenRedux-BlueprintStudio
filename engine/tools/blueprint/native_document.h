#pragma once
#include <cstddef>
#include <string>
#include <vector>

namespace nx
{
    struct BlueprintPin { std::string name, type; };
    struct BlueprintNodeSpec
    {
        std::string id, title, category;
        std::vector<BlueprintPin> inputs, outputs;
        std::string call,property,result;
        bool member=false,pure=false,hasOut=false;
        // name=in/out is encoded as a pin pair; defaults are trusted generated expressions.
        std::vector<BlueprintPin> arguments, defaults;
        // Trusted builtin operator or cast from the native catalogue, never from document text.
        std::string operation{};
    };
    const std::vector<BlueprintNodeSpec>& blueprintCatalogue();
    const BlueprintNodeSpec* blueprintSpec(const std::string& kind);
    bool blueprintPhysicsJoint(const std::string& kind);
    bool blueprintLiteralType(const std::string& type);
    bool blueprintInputLiteralType(const std::string& type);
    std::vector<std::string> blueprintEnumChoices(const std::string& type);
    bool blueprintEnumValue(const std::string& type,const std::string& value);
    std::string blueprintEnumType(const std::string& value);
    struct BlueprintCameraKey { float time=0,from[3]{},to[3]{},fov=60; bool cut=false,smooth=false; std::string event{}; };
    bool blueprintCameraKeys(const std::string& text,std::vector<BlueprintCameraKey>& keys,std::string& error);
    std::string blueprintCameraKeyText(const std::vector<BlueprintCameraKey>& keys);
    BlueprintCameraKey blueprintCameraSample(const std::vector<BlueprintCameraKey>& keys,float time);
    bool blueprintInputComponents(const std::string& type,const std::string& value,std::vector<std::string>& components);
    std::vector<std::string> blueprintInputComponentLabels(const std::string& type);
    std::string blueprintArrayElement(const std::string& type);
    bool blueprintVariableType(const std::string& type);
    bool blueprintLosslessConversion(const std::string& from,const std::string& to);
    bool blueprintNamedCall(const std::string& kind);
    bool blueprintInvokesFunction(const std::string& kind);
    bool blueprintBehaviourReference(const std::string& kind);
    // Managed authoring files are direct children of Redux's own folder.
    bool blueprintFileName(const std::string& file);
    bool blueprintManagedName(const std::string& name,std::string& file);
    bool blueprintAssetRoot(const std::string& root);
    bool blueprintParseRGB(const std::string& text,unsigned& colour);
    std::string blueprintRGBHex(unsigned colour);
    bool blueprintAssetName(const std::string& name,bool folder=false);

    // Native version 2: one canonical model. Node indices are internal; persisted links use stable IDs.
    // The pre-release browser format is deliberately not supported.
    struct BlueprintDocument
    {
        struct InputValue { std::string pin,value; };
        struct Node { std::string id, kind, ref; float x=0,y=0; std::vector<InputValue> values{}; bool watch=false; std::string displayName{},comment{}; unsigned colour=0; };
        struct Point { float x=0,y=0; };
        struct Link { size_t from=0,to=0; std::string output,input; std::vector<Point> route{}; };
        struct Comment { std::string title; std::vector<std::string> members; float x=0,y=0,width=0,height=0; unsigned colour=0; };
        struct Field { std::string name,type,value; bool initialized=false; bool exposed=false; };
        struct Graph
        {
            std::string owner,name,kind;
            std::vector<Node> nodes;
            std::vector<Link> links;
            std::vector<BlueprintPin> parameters{};
            std::string returnType="void";
            bool inlineMacro=false;
            bool sharedLibrary=false;
            bool collapsed=false; // private, function-backed editor subgraph
            std::string dataRole{}; // enum, struct or custom event
            // Explicit imported fields live on the owner lifecycle entry graph.
            std::vector<Field> fields{};
            std::vector<Field> globals{};
            std::vector<Comment> comments{};
            std::string moduleOwner{}; // optional screen scope on Behaviour start graph
            std::string parentBehaviour{}; // shared parent class, on start only
        };
        std::string name;
        std::string assetRoot; // external project directory, separate from graph history
        std::vector<Graph> graphs;
        struct ScreenManifest { std::string owner, source; };
        // Original screen.json bytes, optional for backwards-compatible v2 documents.
        std::vector<ScreenManifest> manifests;
        const std::string* screenManifest(const std::string& owner) const;
        bool importScreenManifest(const std::string& owner,const std::string& source,std::string& error);
        // Project-level reusable definitions. Native JSON source remains lossless;
        // the specific actor/widget editors will interpret it in later stages.
        struct VisualAsset
        {
            std::string id,kind,title,source;
            std::vector<std::string> dependencies;
        };
        const std::vector<VisualAsset>& visualAssets()const{return m_visualAssets;}
        const VisualAsset* visualAsset(const std::string& id)const;
        // Atomic replacement validates all references, including forward references.
        bool replaceVisualAssets(const std::vector<VisualAsset>& assets,std::string& error);
        bool createVisualAsset(const std::string& kind,std::string& id,std::string& error);
        bool cameraSequenceKeys(const std::string& id,std::vector<BlueprintCameraKey>& keys,std::string& error) const;
        bool setCameraSequenceKeys(const std::string& id,const std::vector<BlueprintCameraKey>& keys,std::string& error);
        bool promoteCameraSequence(size_t graph,size_t node,const std::string& title,const std::vector<BlueprintCameraKey>& keys,std::string& id,std::string& error);
        bool placeCameraSequence(const std::string& owner,const std::string& id,size_t& graph,size_t& node,std::string& error);
        bool renameVisualAsset(const std::string& id,const std::string& title,std::string& error);
        bool removeVisualAsset(const std::string& id,std::string& error);
        bool appendVisualWidget(const std::string& id,const std::string& kind,const std::string& label,std::string& error,const std::string& parent="");
        bool visualWidgetPage(const std::string& id,std::string& source,std::string& error)const;
        bool removeVisualWidgetComponent(const std::string& id,const std::string& widget,std::string& error);
        bool setVisualWidgetProperty(const std::string& id,const std::string& widget,const std::string& property,const std::string& value,std::string& error);
        bool setWidgetKey(const std::string& id,const std::string& widget,const std::string& track,const std::string& value,float time,std::string& error);
        bool positionVisualWidget(const std::string& id,const std::string& widget,const std::string& anchor,float x,float y,std::string& error);
        bool reparentVisualWidget(const std::string& id,const std::string& widget,const std::string& parent,std::string& error);
        bool moveVisualWidget(const std::string& id,const std::string& widget,int direction,std::string& error);
        // Native callbacks are ordinary typed screen function graphs.
        bool openVisualWidgetEvent(const std::string& owner,const std::string& id,const std::string& widget,const std::string& event,size_t& graph,std::string& error);
        bool clearVisualWidgetEvent(const std::string& id,const std::string& widget,const std::string& event,std::string& error);
        bool visualWidgetForOwner(const std::string& id,const std::string& owner,std::string& json,std::string& error)const;
        bool visualWidgetUsesFunction(const std::string& owner,const std::string& functionName)const;
        bool setVisualTheme(const std::string& page,const std::string& theme,std::string& error);
        bool visualWidgetTheme(const std::string& page,std::string& source,std::string& error)const;
        bool setVisualThemeProperty(const std::string& id,const std::string& style,const std::string& state,const std::string& property,const std::string& value,std::string& error);
        bool connectVisualWidget(const std::string& owner,const std::string& id,std::string& error);
        // Linked actor definitions use native entity JSON and a parent-first hierarchy.
        bool createVisualActorChild(const std::string& parentId,std::string& id,std::string& error);
        bool resetVisualActorOverrides(const std::string& id,std::string& error,size_t component=0);
        bool resolveVisualActor(const std::string& id,std::string& json,std::vector<std::string>& keys,std::string& error)const;
        bool visualActorRoot(const std::string& id,std::string& json,std::string& error)const;
        bool visualActorSource(const std::string& id,std::string& json,std::string& error)const;
        bool setVisualActorShape(const std::string& id,const std::string& shape,std::string& error,size_t component=0);
        bool appendVisualActorComponent(const std::string& id,size_t parent,const std::string& shape,const std::string& place,std::string& error);
        // Inspector properties use the native entity loader's names and units.
        bool setVisualActorProperty(const std::string& id,size_t component,const std::string& property,const std::string& value,std::string& error);
        bool visualActorInstance(const std::string& id,const std::string& owner,const std::vector<InputValue>& overrides,std::string& json,std::string& error)const;
        bool visualActorForOwner(const std::string& id,const std::string& owner,std::string& json,std::string& error)const;
        bool editVisualActorPlacement(const std::string& owner,const std::string& nodeId,const std::string& property,const std::string& value,std::string& error);
        bool placeVisualActor(const std::string& owner,const std::string& id,const std::string& place,std::string& error);
        void create();
        bool load(const char* data,size_t size,std::string& error);
        bool serialize(std::string& result,std::string& error) const;
        bool move(size_t graph,size_t node,float x,float y);
        bool moveNodes(size_t graph,const std::vector<size_t>& nodes,float dx,float dy);
        bool arrange(size_t graph,std::string& error,const std::vector<float>& widths={});
        bool alignNodes(size_t graph,const std::vector<size_t>& nodes,bool horizontal,std::string& error);
        bool addComment(size_t graph,const std::vector<size_t>& nodes,const std::string& title,std::string& error);
        bool renameComment(size_t graph,size_t comment,const std::string& title,std::string& error);
        bool resizeComment(size_t graph,size_t comment,float x,float y,float width,float height);
        bool editComment(size_t graph,size_t comment,const std::string& title,unsigned colour,std::string& error);
        bool deleteComment(size_t graph,size_t comment);
        bool insertRoutePoint(size_t graph,size_t link,size_t point,float x,float y);
        bool moveRoutePoint(size_t graph,size_t link,size_t point,float x,float y);
        bool deleteRoutePoint(size_t graph,size_t link,size_t point);
        bool addNode(size_t graph,const std::string& kind,float x,float y,std::string& error,const std::string& functionTarget="");
        bool placeContextNode(size_t graph,size_t existing,const std::string& pin,bool output,const std::string& kind,const std::string& reference,const std::string& otherPin,float x,float y,std::string& error);
        bool promotePin(size_t graph,size_t node,const std::string& pin,bool output,const std::string& variable,std::string& error);
        bool connectCompatible(size_t graph,size_t from,const std::string& output,size_t to,const std::string& input,std::string& error);
        bool canCallFunction(size_t graph,const std::string& target) const;
        bool addConnectedNode(size_t graph,size_t from,const std::string& output,const std::string& kind,float x,float y,std::string& error);
        bool addVariableNode(size_t graph,const std::string& variableName,bool global,bool setter,float x,float y,std::string& error);
        bool deleteNode(size_t graph,size_t node,std::string& error);
        bool deleteNodes(size_t graph,const std::vector<size_t>& nodes,std::string& error);
        bool connect(size_t graph,size_t from,const std::string& output,size_t to,const std::string& input,std::string& error);
        bool disconnect(size_t graph,size_t node,const std::string& pin,bool output);
        bool setLiteral(size_t graph,size_t node,const std::string& value,std::string& error);
        bool toggleWatch(size_t graph,size_t node);
        bool setInputValue(size_t graph,size_t node,const std::string& pin,const std::string& value,std::string& error);
        bool editNodeValues(size_t graph,size_t node,const std::vector<InputValue>& values,const std::string* literalValue,std::string& error,const std::string* displayName=nullptr,const std::string* comment=nullptr,const unsigned* colour=nullptr);
        bool disconnectNode(size_t graph,size_t node);
        bool moveConnection(size_t graph,size_t link,size_t node,const std::string& pin,bool output,std::string& error);
        std::string nodeDisplayName(size_t graph,const Node& node) const;
        bool clearInputValue(size_t graph,size_t node,const std::string& pin);
        // Constant four-point curves can be edited as a single undoable transaction.
        bool cubicPoints(size_t graph,size_t node,std::vector<std::string>& points,std::string& error)const;
        bool setCubicPoints(size_t graph,size_t node,const std::vector<std::string>& points,std::string& error);
        bool setJointProjection(size_t graph,size_t node,const std::string& pin,unsigned plane,float horizontal,float vertical,std::string& error);
        bool setReference(size_t graph,size_t node,const std::string& value,std::string& error);
        struct InputActionBinding { std::string name,key; unsigned button=14; };
        std::vector<InputActionBinding> inputActions() const;
        bool setInputAction(const std::string& action,const std::string& key,unsigned button,std::string& error);
        bool addFunction(const std::string& name,std::string& error,const std::string& owner="Screen");
        const Graph* function(const std::string& owner,const std::string& name) const;
        const Graph* libraryFunction(const std::string& name) const;
        const Graph* dataType(const std::string& name) const;
        std::vector<const Graph*> dispatcherListeners(const std::string& owner,const std::string& signal) const;
        bool setSharedLibrary(size_t graph,bool enabled,std::string& error);
        bool exportLibrary(std::string& result,std::string& error) const;
        bool importLibrary(const char* bytes,size_t count,const std::string& owner,std::string& error);
        bool classAssignable(const std::string& from,const std::string& to) const;
        bool nodeSpec(const Graph& graph,const Node& node,BlueprintNodeSpec& spec) const;
        bool setFunctionSignature(size_t graph,const std::vector<BlueprintPin>& parameters,const std::string& returnType,std::string& error);
        bool editFunctionSignature(size_t graph,const std::string& signature,std::string& error);
        std::string functionSignature(size_t graph) const;
        bool addScreen(const std::string& id,std::string& error);
        bool addLevelEvents(const std::string& owner,std::string& error);
        bool removeScreen(const std::string& id,std::string& error);
        std::vector<std::string> screens() const;
        std::vector<std::string> behaviours() const;
        bool addBehaviour(const std::string& name,std::string& error);
        std::string parentBehaviour(const std::string& owner)const;
        bool ensureActorBehaviour(const std::string& id,std::string& owner,std::string& error);
        bool setActorFieldExposed(const std::string& owner,const std::string& field,bool enabled,std::string& error);
        std::string actorBehaviour(const std::string& id)const;
        std::vector<Field> actorSpawnFields(const std::string& id)const;
        bool validateBehaviourInheritance(std::string& error)const;
        // True only for a synchronous, preview-safe Construction call tree.
        bool validateEditorConstruction(const std::string& classOwner,std::string& error)const;
        bool removeBehaviour(const std::string& name,std::string& error);
        bool removeFunction(size_t graph,std::string& error);
        bool setInlineMacro(size_t graph,bool enabled,std::string& error);
        bool renameFunction(size_t graph,const std::string& functionName,std::string& error);
        bool duplicateFunction(size_t graph,const std::string& functionName,std::string& error);
        bool collapseToFunction(size_t graph,const std::vector<size_t>& nodes,const std::string& functionName,std::string& error,bool collapsed=false);
        bool expandCollapsed(size_t graph,size_t node,std::string& error);
        bool functions(std::string& error) const;
        // Named instance variables and function names are scoped to their owning screen.
        std::string moduleForOwner(const std::string& owner) const;
        const std::vector<Field>& moduleGlobals(const std::string& owner="") const;
        bool canAttach(const std::string& owner,const std::string& behaviour) const;
        bool editVariableDeclaration(const std::string& owner,const std::string& command,std::string& error);
        bool variables(std::vector<BlueprintPin>& values,std::string& error,const std::string& owner="Screen") const;
        bool undo();
        bool redo();
        bool dirty()const{return m_revision!=m_savedRevision;}
        bool canUndo()const{return !m_undo.empty();}
        bool canRedo()const{return !m_redo.empty();}
        bool isProject()const{return screens().size()>1;}
        void markSaved(){m_savedRevision=m_revision;}
        size_t revision()const{return m_revision;}
        void markRecovered(){m_revision=++m_nextRevision;}
    private:
        struct Edit { size_t graph; Graph state; size_t revision; std::vector<Graph> all; std::vector<ScreenManifest> manifests{}; std::vector<VisualAsset> visualAssets{}; };
        void record(size_t graph);
        void recordGraphList();
        std::vector<VisualAsset> m_visualAssets;
        std::vector<Edit> m_undo,m_redo;
        size_t m_revision=0,m_savedRevision=0,m_nextRevision=0;
        unsigned m_nextId=0,m_visualSerial=0;
    };
}
