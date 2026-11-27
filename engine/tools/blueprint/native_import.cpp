#include "tools/blueprint/native_import.h"
#include "tools/blueprint/native_compile.h"
#include <algorithm>
#include <cctype>
#include <utility>
#include <unordered_map>
#include <unordered_set>

namespace nx
{
    namespace
    {
        enum class Kind { End, Name, Number, String, Symbol };
        struct Token { Kind kind; std::string text; size_t line; };
        bool alpha(char c){return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||c=='_';}
        bool digit(char c){return c>='0'&&c<='9';}
        bool lex(const std::string& source,std::vector<Token>& tokens,std::string& error)
        {
            size_t i=0,line=1;
            auto fail=[&](const char* why){error="Source line "+std::to_string(line)+": "+why;return false;};
            if(source.empty()||source.size()>512*1024||source.find('\0')!=std::string::npos)return fail("Empty, oversized or binary source.");
            if(source.rfind("\xef\xbb\xbf",0)==0)i=3;
            while(i<source.size())
            {
                if(tokens.size()>=8192)return fail("Import token limit exceeded.");
                const char c=source[i];const size_t start=i,atLine=line;
                if(c==' '||c=='\t'||c=='\r'||c=='\n'){++i;if(c=='\n')++line;continue;}
                if(source.compare(i,2,"//")==0){while(i<source.size()&&source[i]!='\n')++i;continue;}
                if(source.compare(i,2,"/*")==0)
                {
                    i+=2;
                    while(i<source.size()&&source.compare(i,2,"*/")!=0){if(source[i++]=='\n')++line;}
                    if(i>=source.size())return fail("Unclosed comment.");
                    i+=2;continue;
                }
                if(alpha(c))
                {
                    ++i;while(i<source.size()&&(alpha(source[i])||digit(source[i])))++i;
                    tokens.push_back({Kind::Name,source.substr(start,i-start),atLine});continue;
                }
                if(digit(c)||(c=='.'&&i+1<source.size()&&digit(source[i+1])))
                {
                    while(i<source.size()&&digit(source[i]))++i;
                    if(i<source.size()&&source[i]=='.'){++i;while(i<source.size()&&digit(source[i]))++i;}
                    if(i<source.size()&&(source[i]=='e'||source[i]=='E'))
                    {
                        ++i;if(i<source.size()&&(source[i]=='+'||source[i]=='-'))++i;
                        while(i<source.size()&&digit(source[i]))++i;
                    }
                    if(i<source.size()&&(source[i]=='f'||source[i]=='F'||source[i]=='u'||source[i]=='U'))++i;
                    tokens.push_back({Kind::Number,source.substr(start,i-start),atLine});continue;
                }
                if(c=='"')
                {
                    ++i;std::string value;
                    while(i<source.size()&&source[i]!='"')
                    {
                        char ch=source[i++];
                        if(ch=='\\')
                        {
                            if(i>=source.size())return fail("Unclosed string.");
                            ch=source[i++];
                            switch(ch){case 'n':ch='\n';break;case 'r':ch='\r';break;case 't':ch='\t';break;
                                case '\\':case '"':break;default:return fail("Unsupported string escape.");}
                        }
                        else if(static_cast<unsigned char>(ch)<32)return fail("Multiline/control-character string is unsupported.");
                        value+=ch;if(value.size()>4096)return fail("String is too long.");
                    }
                    if(i>=source.size())return fail("Unclosed string.");
                    ++i;tokens.push_back({Kind::String,std::move(value),atLine});continue;
                }
                const auto two=source.substr(i,2);
                if(two=="::"||two=="=="||two=="!="||two=="<="||two==">="||two=="&&"||two=="||")
                {tokens.push_back({Kind::Symbol,two,atLine});i+=2;continue;}
                if(std::string("{}();,.=@+-*/%<>!").find(c)!=std::string::npos)
                {tokens.push_back({Kind::Symbol,std::string(1,c),atLine});++i;continue;}
                return fail("Unsupported character or directive; source was not converted.");
            }
            tokens.push_back({Kind::End,"",line});return true;
        }

        struct Value
        {
            std::string type, literal, pin;
            size_t node=static_cast<size_t>(-1);
        };
        struct Flow { size_t node=0; std::string pin="then"; };
        struct Parser
        {
            BlueprintDocument doc;
            const std::vector<Token>& tokens;
            std::string& error;
            size_t at=0,graph=0;
            unsigned nesting=0;
            std::unordered_map<std::string,std::string> fields;
            std::unordered_set<std::string> methods;
            std::string eventArg,eventType;
            const Token& peek()const{return tokens[at];}
            bool is(const char* s)const{return peek().kind!=Kind::String&&peek().kind!=Kind::End&&peek().text==s;}
            bool take(const char* s){if(!is(s))return false;++at;return true;}
            bool fail(const std::string& why)
            {if(error.empty())error="Source line "+std::to_string(peek().line)+": "+why;return false;}
            bool expect(const char* s){return take(s)||fail(std::string("Expected '")+s+"'.");}
            bool name(std::string& out)
            {
                if(peek().kind!=Kind::Name||peek().text.size()>64)return fail("Expected an identifier.");
                out=peek().text;++at;return true;
            }
            bool add(const std::string& kind,size_t& index,const std::string& ref="")
            {
                auto& g=doc.graphs[graph];index=g.nodes.size();
                if(index>=256||!blueprintSpec(kind))return fail("Unsupported node or import graph size limit.");
                const auto* spec=blueprintSpec(kind);
                const bool exec=std::any_of(spec->inputs.begin(),spec->inputs.end(),[](const BlueprintPin& p){return p.type=="exec";});
                g.nodes.push_back({"import"+std::to_string(index),kind,ref,exec?360.f:24.f,60.f+static_cast<float>(index)*126.f});
                return true;
            }
            bool bind(size_t node,const std::string& pin,const Value& v)
            {
                auto& g=doc.graphs[graph];BlueprintNodeSpec spec;
                if(!doc.nodeSpec(g,g.nodes[node],spec))return fail("Unknown imported node.");
                const auto found=std::find_if(spec.inputs.begin(),spec.inputs.end(),[&](const BlueprintPin& p){return p.name==pin;});
                if(found==spec.inputs.end()||found->type!=v.type)return fail("Implicit conversions are not imported: "+v.type+" to "+(found==spec.inputs.end()?"?":found->type)+".");
                if(v.node==static_cast<size_t>(-1))g.nodes[node].values.push_back({pin,v.literal});
                else g.links.push_back({v.node,node,v.pin,pin});
                return true;
            }
            void chain(Flow& flow,size_t node)
            {doc.graphs[graph].links.push_back({flow.node,node,flow.pin,"in"});flow={node,"then"};}
            bool field(const std::string& id,Value& v)
            {
                if(id==eventArg&&!eventArg.empty())
                {
                    size_t n;if(!add(eventType=="float"?"event:delta":"event:canvas",n))return false;
                    v={eventType,"","value",n};return true;
                }
                const auto& g=doc.graphs[graph];
                for(const auto& p:g.parameters)if(p.name==id){v={p.type,"","arg_"+p.name,0};return true;}
                const auto found=fields.find(id);
                if(found==fields.end())return fail("Unknown field or unsupported expression: "+id+".");
                size_t n;if(!add("var:get:"+found->second,n,id))return false;
                v={found->second,"","value",n};return true;
            }
            bool numeric(Value& v)
            {
                auto s=peek().text;++at;
                std::string type="int";
                if(s.ends_with("f")||s.ends_with("F")){type="float";s.pop_back();}
                else if(s.ends_with("u")||s.ends_with("U")){type="uint";s.pop_back();}
                else if(s.find_first_of(".eE")!=std::string::npos)type="double";
                if((type=="int"||type=="uint")&&(s.find_first_not_of("0123456789")!=std::string::npos||s.empty()||(s.size()>1&&s[0]=='0')))
                    return fail("Only ordinary decimal integer literals are supported.");
                // A floating suffix is meaningful in AngelScript; the compiler emits the same explicit type.
                v={type,s,"",static_cast<size_t>(-1)};return true;
            }
            bool qualified(std::string& id)
            {
                while(take("::"))
                {std::string part;if(!name(part))return false;id+="::"+part;}
                return true;
            }
            static std::string propertyName(const BlueprintNodeSpec& spec)
            {
                auto s=spec.call;const auto sep=s.rfind("::");const size_t pos=sep==std::string::npos?0:sep+2;
                if(s.size()<pos+4)return "";
                s.erase(pos,4);return s;
            }
            bool api(const std::string& id,const std::vector<Value>& args,const Value* target,bool pure,const std::string& property,Value& result,Flow* flow)
            {
                const BlueprintNodeSpec* selected=nullptr;
                for(const auto& spec:blueprintCatalogue())
                {
                    if(spec.id.rfind("api:",0)!=0||spec.member!=(target!=nullptr)||spec.property!=property||spec.hasOut||spec.pure!=pure)continue;
                    if((property.empty()?spec.call:propertyName(spec))!=id)continue;
                    // Never guess an overload or silently introduce a numeric/handle conversion.
                    if(!pure&&spec.result!="void"&&!blueprintLiteralType(spec.result))continue;
                    if(pure&&(spec.result.empty()||spec.result=="void"))continue;
                    if(target)
                    {
                        const auto t=std::find_if(spec.inputs.begin(),spec.inputs.end(),[](const BlueprintPin& p){return p.name=="target";});
                        if(t==spec.inputs.end()||t->type!=target->type)continue;
                    }
                    if(args.size()>spec.arguments.size())continue;
                    bool match=true;
                    for(size_t i=0;i<spec.arguments.size();++i)
                    {
                        const auto& arg=spec.arguments[i];
                        if(arg.name!="in"){match=false;break;}
                        if(i>=args.size())
                        {
                            if(std::none_of(spec.defaults.begin(),spec.defaults.end(),[&](const BlueprintPin& p){return p.name==arg.type;})){match=false;break;}
                            continue;
                        }
                        const auto pin=std::find_if(spec.inputs.begin(),spec.inputs.end(),[&](const BlueprintPin& p){return p.name==arg.type;});
                        if(pin==spec.inputs.end()||pin->type!=args[i].type){match=false;break;}
                    }
                    if(!match)continue;
                    if(selected)return fail("Ambiguous API overload: "+id+".");
                    selected=&spec;
                }
                if(!selected)return fail("No exact supported "+std::string(pure?"value":"action")+" API signature for "+id+".");
                size_t node;if(!add(selected->id,node))return false;
                if(target&&!bind(node,"target",*target))return false;
                for(size_t i=0;i<args.size();++i)if(!bind(node,selected->arguments[i].type,args[i]))return false;
                if(flow)chain(*flow,node);
                result={selected->result,"","result",node};return true;
            }
            bool arguments(std::vector<Value>& values)
            {
                if(!expect("("))return false;
                if(take(")"))return true;
                do
                {
                    if(values.size()>=16)return fail("Argument limit exceeded.");
                    Value v;if(!expression(v))return false;values.push_back(std::move(v));
                }while(take(","));
                return expect(")");
            }
            bool primary(Value& v)
            {
                if(take("("))return expression(v)&&expect(")");
                if(peek().kind==Kind::String){v={"string",peek().text,"",static_cast<size_t>(-1)};++at;return true;}
                if(peek().kind==Kind::Number)return numeric(v);
                if(is("true")||is("false")){v={"bool",peek().text,"",static_cast<size_t>(-1)};++at;return true;}
                std::string id;if(!name(id)||!qualified(id))return false;
                if(take("."))
                {
                    Value target;if(!field(id,target)||!name(id))return false;
                    std::vector<Value> args;
                    if(is("("))return arguments(args)&&api(id,args,&target,true,"",v,nullptr);
                    return api(id,args,&target,true,"get",v,nullptr);
                }
                if(is("("))
                {
                    std::vector<Value> args;if(!arguments(args))return false;
                    if(id=="int"||id=="uint"||id=="float"||id=="double")
                    {
                        if(args.size()!=1)return fail("Expected one numeric conversion argument.");
                        if(args[0].type==id){v=args[0];return true;}
                        size_t n;if(!add("cast:"+args[0].type+":"+id,n)||!bind(n,"a",args[0]))return false;
                        v={id,"","result",n};return true;
                    }
                    if(id=="Vec2"||id=="Vec3"||id=="Color")
                    {
                        const auto kind="make:"+id;const auto* spec=blueprintSpec(kind);
                        if(!spec||args.size()!=spec->inputs.size())return fail("Constructor requires all typed components.");
                        size_t n;if(!add(kind,n))return false;
                        for(size_t i=0;i<args.size();++i)if(!bind(n,spec->inputs[i].name,args[i]))return false;
                        v={id,"","result",n};return true;
                    }
                    return api(id,args,nullptr,true,"",v,nullptr);
                }
                if(id.find("::")!=std::string::npos)return api(id,{},nullptr,true,"get",v,nullptr);
                return field(id,v);
            }
            struct Depth {unsigned& n;explicit Depth(unsigned& d):n(d){++n;}~Depth(){--n;}};
            bool unary(Value& v)
            {
                Depth d(nesting);if(nesting>64)return fail("Expression nesting limit exceeded.");
                const bool neg=take("-"),notOp=!neg&&take("!");
                if(!neg&&!notOp)return primary(v);
                Value a;if(!unary(a))return false;
                const std::string kind=notOp?"not:bool":"negate:"+a.type;
                const auto* spec=blueprintSpec(kind);
                if(!spec||spec->inputs[0].type!=a.type)return fail("Unsupported unary operator.");
                size_t n;if(!add(kind,n)||!bind(n,"a",a))return false;
                v={spec->result,"","result",n};return true;
            }
            static int precedence(const std::string& s)
            {
                if(s=="||")return 1;
                if(s=="&&")return 2;
                if(s=="=="||s=="!=")return 3;
                if(s=="<"||s==">"||s=="<="||s==">=")return 4;
                if(s=="+"||s=="-")return 5;
                if(s=="*"||s=="/"||s=="%")return 6;
                return 0;
            }
            bool expression(Value& v,int minimum=1)
            {
                if(!unary(v))return false;
                while(peek().kind==Kind::Symbol&&precedence(peek().text)>=minimum)
                {
                    const auto op=peek().text;const int p=precedence(op);++at;
                    Value b;if(!expression(b,p+1))return false;
                    if(v.type!=b.type)return fail("Mixed-type operators need explicit conversions.");
                    const std::unordered_map<std::string,std::string> kinds={{"+","add"},{"-","subtract"},{"*","multiply"},{"/","divide"},{"%","mod"},{"==","equal"},{"!=","not_equal"},{"<","less"},{"<=","less_equal"},{">","greater"},{">=","greater_equal"},{"&&","and"},{"||","or"}};
                    const auto kind=kinds.at(op)+":"+v.type;const auto* spec=blueprintSpec(kind);
                    size_t n;if(!spec)return fail("Unsupported typed operator.");
                    if(!add(kind,n)||!bind(n,"a",v)||!bind(n,"b",b))return false;
                    v={spec->result,"","result",n};
                }
                return true;
            }
            bool functionCall(const std::string& id,const std::vector<Value>& args,Flow& flow,Value& result)
            {
                const auto* fn=doc.function("Screen",id);
                if(!fn||args.size()!=fn->parameters.size())return fail("Unsupported function arguments: "+id+".");
                size_t n;if(!add("call:function",n,id))return false;
                for(size_t i=0;i<args.size();++i)if(!bind(n,"arg_"+fn->parameters[i].name,args[i]))return false;
                chain(flow,n);result={fn->returnType,"","result",n};return true;
            }
            bool directValue(Value& result,Flow& flow)
            {
                if(peek().kind==Kind::Name&&at+1<tokens.size()&&tokens[at+1].text=="("&&doc.function("Screen",peek().text))
                {
                    const auto id=peek().text;++at;
                    std::vector<Value> args;
                    return arguments(args)&&functionCall(id,args,flow,result);
                }
                return expression(result);
            }
            bool block(Flow flow,unsigned depth,bool* returned=nullptr)
            {
                bool terminal=false;
                if(depth>32||!expect("{"))return fail("Statement nesting limit or missing block.");
                while(!take("}"))
                {
                    if(peek().kind==Kind::End)return fail("Unclosed method.");
                    if(terminal)return fail("Statements after an unconditional return cannot be imported.");
                    if(take("return"))
                    {
                        const auto& g=doc.graphs[graph];
                        if(g.kind!="function")return fail("Early returns from lifecycle methods are not supported.");
                        Value result;
                        if(g.returnType!="void"&&!directValue(result,flow))return false;
                        if(!expect(";"))return false;
                        size_t n;if(!add("return:function",n))return false;
                        if(g.returnType!="void"&&!bind(n,"value",result))return false;
                        chain(flow,n);terminal=true;continue;
                    }
                    if(take("if"))
                    {
                        Value condition;if(!expect("(")||!expression(condition)||!expect(")"))return false;
                        size_t branch;if(!add("branch",branch)||!bind(branch,"condition",condition))return false;
                        chain(flow,branch);
                        bool yes=false,no=false;
                        if(!block({branch,"true"},depth+1,&yes))return false;
                        if(take("else")&&!block({branch,"false"},depth+1,&no))return false;
                        terminal=yes&&no;continue;
                    }
                    std::string id;if(!name(id)||!qualified(id))return false;
                    Value target;const Value* member=nullptr;
                    if(take(".")){if(!field(id,target)||!name(id))return false;member=&target;}
                    if(take("="))
                    {
                        Value v;if(!directValue(v,flow)||!expect(";"))return false;
                        if(!member&&id.find("::")==std::string::npos)
                        {
                            const auto found=fields.find(id);
                            const auto& params=doc.graphs[graph].parameters;
                            if(found==fields.end()||id==eventArg||std::any_of(params.begin(),params.end(),[&](const BlueprintPin& p){return p.name==id;}))
                                return fail("Assignment requires a declared instance field, not a method parameter.");
                            size_t n;if(!add("var:set:"+found->second,n,id)||!bind(n,"value",v))return false;
                            chain(flow,n);
                        }
                        else {Value unused;if(!api(id,{v},member,false,"set",unused,&flow))return false;}
                        continue;
                    }
                    std::vector<Value> args;Value unused;
                    if(!arguments(args)||!expect(";"))return false;
                    if(!member&&doc.function("Screen",id))
                    {if(!functionCall(id,args,flow,unused))return false;}
                    else if(!api(id,args,member,false,"",unused,&flow))return false;
                }
                if(returned)*returned=terminal;
                return true;
            }
            bool parse()
            {
                doc.create();std::string className;
                if(!expect("class")||!name(className)||!expect(":")||!expect("Screen")||!expect("{"))return false;
                doc.name=className;
                // First collect simple fields and event declaration/body ranges; fields can follow methods.
                struct Method {size_t graph,body,end;std::string arg,type;};
                std::vector<Method> bodies;
                while(!take("}"))
                {
                    std::string type,id;if(!name(type)||!name(id))return false;
                    if(take(";"))
                    {
                        if(!blueprintLiteralType(type)||!fields.emplace(id,type).second)return fail("Only unique, uninitialized primitive instance fields are supported.");
                        continue;
                    }
                    if(!methods.insert(id).second)return fail("Overloaded or duplicate method "+id+".");
                    const auto found=std::find_if(doc.graphs.begin(),doc.graphs.end(),[&](const BlueprintDocument::Graph& g){return g.name==id;});
                    const bool event=found!=doc.graphs.end();
                    const size_t methodGraph=event?static_cast<size_t>(found-doc.graphs.begin()):doc.graphs.size();
                    if(!expect("("))return false;
                    std::string arg,argType;
                    if(event)
                    {
                        if(type!="void")return fail("Lifecycle methods must return void.");
                        if(id=="update"||id=="draw")
                        {
                            if(!expect(id=="update"?"float":"Canvas"))return false;
                            if(id=="draw"&&!expect("@"))return false;
                            if(!name(arg))return false;
                            argType=id=="update"?"float":"Canvas@";
                        }
                    }
                    else
                    {
                        // Preserve only types supported by the native function signature model.
                        const auto supported=[](const std::string& t)
                        {return blueprintLiteralType(t)||t=="Vec2"||t=="Vec3"||t=="Color";};
                        if(type!="void"&&!supported(type))return fail("Unsupported function return type.");
                        std::vector<BlueprintPin> parameters;
                        if(!is(")"))
                        {
                            do
                            {
                                BlueprintPin p;
                                if(parameters.size()>=8||!name(p.type)||!name(p.name)||!supported(p.type))
                                    return fail("Unsupported function parameter.");
                                if(std::any_of(parameters.begin(),parameters.end(),[&](const BlueprintPin& q){return q.name==p.name;}))
                                    return fail("Duplicate function parameter.");
                                parameters.push_back(std::move(p));
                            }while(take(","));
                        }
                        if(!doc.addFunction(id,error))return false;
                        auto& g=doc.graphs[methodGraph];g.parameters=std::move(parameters);g.returnType=type;
                    }
                    if(!expect(")")||!is("{"))return fail("Expected method body.");
                    const size_t body=at;unsigned braces=0;
                    do
                    {
                        if(peek().kind==Kind::End)return fail("Unclosed method.");
                        if(is("{"))++braces;
                        if(is("}"))--braces;
                        ++at;
                    }while(braces);
                    bodies.push_back({methodGraph,body,at,arg,argType});
                }
                take(";");
                if(peek().kind!=Kind::End)return fail("Additional declarations or classes are not supported.");
                for(const auto& method:bodies)
                {
                    graph=method.graph;at=method.body;eventArg=method.arg;eventType=method.type;
                    if(!block({},0)||at!=method.end)return fail("Method body could not be converted completely.");
                }
                return true;
            }
        };
    }
    bool importBlueprintScreen(const std::string& source,BlueprintDocument& document,std::string& error)
    {
        error.clear();std::vector<Token> tokens;
        if(!lex(source,tokens,error))return false;
        Parser parser{{},tokens,error,0,0,0,{},{},{},{}};
        if(!parser.parse())
        {
            if(error.empty())error="Source conversion failed.";
            if(error.rfind("Source line ",0)!=0)error="Source line "+std::to_string(parser.peek().line)+": "+error;
            return false;
        }
        std::string json,generated;
        if(!parser.doc.serialize(json,error)||!compileBlueprint(parser.doc,generated,error))
        {error="Imported graph cannot be represented safely: "+error;return false;}
        BlueprintDocument candidate;
        if(!candidate.load(json.data(),json.size(),error))return false;
        candidate.markRecovered();document=std::move(candidate);error.clear();return true;
    }
}
