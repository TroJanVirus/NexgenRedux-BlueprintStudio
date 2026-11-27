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
                if(two=="::"||two=="=="||two=="!="||two=="<="||two==">="||two=="&&"||two=="||"||two=="+="||two=="-="||two=="*="||two=="/="||two=="%="||two=="++"||two=="--")
                {tokens.push_back({Kind::Symbol,two,atLine});i+=2;continue;}
                if(std::string("{}();:,.=@+-*/%<>!").find(c)!=std::string::npos)
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
            std::unordered_map<std::string,Value> locals;
            std::unordered_set<std::string> freeFunctions{};
            bool inFreeFunction=false;
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
            bool field(const std::string& id,Value& v,bool instanceOnly=false)
            {
                if(!instanceOnly)
                {
                    if(id==eventArg&&!eventArg.empty())
                    {
                        size_t n;if(!add(eventType=="float"?"event:delta":eventType=="Entity@"?"event:other":"event:canvas",n))return false;
                        v={eventType,"","value",n};return true;
                    }
                    const auto local=locals.find(id);if(local!=locals.end()){v=local->second;return true;}
                    const auto& g=doc.graphs[graph];
                    for(const auto& p:g.parameters)if(p.name==id){v={p.type,"","arg_"+p.name,0};return true;}
                }
                if(!inFreeFunction&&doc.graphs[graph].owner.rfind("behaviour:",0)==0&&id=="entity")
                {
                    size_t n;if(!add("event:entity",n))return false;
                    v={"Entity@","","value",n};return true;
                }
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
                    if(!pure&&spec.result!="void"&&!blueprintSpec("capture:"+spec.result))continue;
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
                const bool instanceOnly=id=="this";
                if(instanceOnly&&inFreeFunction)return fail("Standalone functions cannot use this.");
                if(instanceOnly&&(!expect(".")||!name(id)))return false;
                if(take("."))
                {
                    Value target;if(!field(id,target,instanceOnly)||!name(id))return false;
                    std::vector<Value> args;
                    if(is("("))return arguments(args)&&api(id,args,&target,true,"",v,nullptr);
                    return api(id,args,&target,true,"get",v,nullptr);
                }
                if(is("("))
                {
                    if(instanceOnly)return fail("Instance method calls must supply a complete execution value.");
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
                return field(id,v,instanceOnly);
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
            bool functionCall(const std::string& id,const std::vector<Value>& args,Flow& flow,Value& result,bool instanceOnly=false)
            {
                const bool standalone=doc.graphs[graph].owner=="Screen"&&freeFunctions.count(id)!=0;
                if((inFreeFunction&&!standalone)||(instanceOnly&&(inFreeFunction||standalone)))
                    return fail("Function call crosses unsupported standalone/instance scope: "+id+".");
                const auto* fn=doc.function(doc.graphs[graph].owner,id);
                if(!fn||args.size()!=fn->parameters.size())return fail("Unsupported function arguments: "+id+".");
                size_t n;if(!add("call:function",n,id))return false;
                for(size_t i=0;i<args.size();++i)if(!bind(n,"arg_"+fn->parameters[i].name,args[i]))return false;
                chain(flow,n);result={fn->returnType,"","result",n};return true;
            }
            bool directValue(Value& result,Flow& flow)
            {
                size_t methodAt=at;
                if(is("this")&&at+3<tokens.size()&&tokens[at+1].text==".")methodAt=at+2;
                if(tokens[methodAt].kind==Kind::Name&&methodAt+1<tokens.size()&&tokens[methodAt+1].text=="("&&doc.function(doc.graphs[graph].owner,tokens[methodAt].text))
                {
                    const bool instanceOnly=methodAt!=at;
                    const auto id=tokens[methodAt].text;at=methodAt+1;
                    std::vector<Value> args;
                    return arguments(args)&&functionCall(id,args,flow,result,instanceOnly);
                }
                // Direct action calls must be hoisted only when they are the complete value expression.
                // Leave pure or mixed-classification overload sets to the ordinary expression parser.
                if(peek().kind==Kind::Name)
                {
                    size_t scan=at+1;std::string id=peek().text;
                    while(scan+1<tokens.size()&&tokens[scan].text=="::"&&tokens[scan+1].kind==Kind::Name)
                    {id+="::"+tokens[scan+1].text;scan+=2;}
                    if(scan<tokens.size()&&tokens[scan].text=="("&&id.find("::")!=std::string::npos)
                    {
                        bool action=false,pure=false;
                        for(const auto& spec:blueprintCatalogue())if(!spec.member&&spec.call==id&&spec.property.empty())
                        {if(spec.pure)pure=true;else action=true;}
                        if(action&&!pure)
                        {
                            at=scan;std::vector<Value> args;
                            if(!arguments(args)||!api(id,args,nullptr,false,"",result,&flow))return false;
                            if(result.type=="void")return fail("Void action cannot supply a value.");
                            return true;
                        }
                    }
                }
                return expression(result);
            }
            struct LocalScope
            {
                std::unordered_map<std::string,Value>& live;
                std::unordered_map<std::string,Value> outer;
                ~LocalScope(){live.swap(outer);}
            };
            // A single controlled statement has the same isolated scope as a braced body.
            bool body(Flow flow,unsigned depth,bool& returned)
            {
                if(depth>32)return fail("Statement nesting limit exceeded.");
                if(is("{"))return block(flow,depth,&returned);
                LocalScope scope{locals,locals};
                returned=false;
                return statement(flow,depth,returned);
            }
            // Parsing the entire controlled statement before testing for else binds it to
            // the nearest unmatched if, including when the source has no braces.
            bool conditional(Flow& flow,unsigned depth,bool& terminal)
            {
                if(depth>=32)return fail("Statement nesting limit exceeded.");
                Value condition;if(!expect("(")||!expression(condition)||!expect(")"))return false;
                size_t branch;if(!add("branch",branch)||!bind(branch,"condition",condition))return false;
                chain(flow,branch);
                bool yes=false,no=false;
                if(!body({branch,"true"},depth+1,yes))return false;
                if(take("else")&&!body({branch,"false"},depth+1,no))return false;
                terminal=yes&&no;
                return true;
            }
            bool block(Flow flow,unsigned depth,bool* returned=nullptr,Flow* continuation=nullptr)
            {
                LocalScope scope{locals,locals};
                bool terminal=false;
                if(depth>32||!expect("{"))return fail("Statement nesting limit or missing block.");
                while(!take("}"))
                {
                    if(peek().kind==Kind::End)return fail("Unclosed method.");
                    if(terminal)return fail("Statements after an unconditional return cannot be imported.");
                    if(!statement(flow,depth,terminal))return false;
                }
                if(returned)*returned=terminal;
                if(continuation)*continuation=flow;
                return true;
            }
            bool statement(Flow& flow,unsigned depth,bool& terminal)
            {
                if(take(";"))return true;
                if(is("{"))return block(flow,depth+1,&terminal,&flow);
                if(take("return"))
                {
                    const auto& g=doc.graphs[graph];
                    if(g.kind!="function")return fail("Early returns from lifecycle methods are not supported.");
                    Value result;
                    if(g.returnType!="void"&&!directValue(result,flow))return false;
                    if(!expect(";"))return false;
                    size_t n;if(!add("return:function",n))return false;
                    if(g.returnType!="void"&&!bind(n,"value",result))return false;
                    chain(flow,n);terminal=true;return true;
                }
                if(take("for"))
                {
                    // Only the exact, statically bounded zero-based idiom has the same
                    // semantics as Repeat. Never clamp an imported dynamic loop silently.
                    std::string index,condition,increment;
                    if(!expect("(")||!expect("int")||!name(index)||!expect("=")||!expect("0")||
                        !expect(";")||!name(condition)||condition!=index)
                        return fail("Use a zero-based int loop with a constant upper bound.");
                    const bool inclusive=take("<=");
                    if(!inclusive&&!expect("<"))return false;
                    if(peek().kind!=Kind::Number)return fail("Loop limit must be a decimal integer constant.");
                    const auto limit=peek().text;
                    if(limit.empty()||limit.size()>5||limit.find_first_not_of("0123456789")!=std::string::npos||
                        (limit.size()>1&&limit[0]=='0'))return fail("Invalid decimal loop limit.");
                    unsigned count=0;for(char c:limit)count=count*10+static_cast<unsigned>(c-'0');
                    if(inclusive)++count;
                    if(count>10000)return fail("Loop limit exceeds the native Repeat maximum of 10000.");
                    ++at;
                    if(!expect(";"))return false;
                    if(take("++")){if(!name(increment))return false;}
                    else if(!name(increment)||!expect("++"))return false;
                    if(increment!=index||!expect(")"))return fail("Loop increment must advance its own index.");
                    const auto& params=doc.graphs[graph].parameters;
                    if(locals.count(index)||fields.count(index)||index==eventArg||
                        std::any_of(params.begin(),params.end(),[&](const BlueprintPin& p){return p.name==index;}))
                        return fail("Loop index shadowing is not supported.");
                    size_t n;if(!add("repeat",n)||!bind(n,"count",Value{"int",std::to_string(count),"",static_cast<size_t>(-1)}))return false;
                    chain(flow,n);locals.emplace(index,Value{"int","","index",n});
                    bool bodyReturned=false;
                    if(!body({n,"body"},depth+1,bodyReturned))return false;
                    locals.erase(index);return true;
                }
                if(take("if"))
                {
                    if(!conditional(flow,depth,terminal))return false;
                    return true;
                }
                std::string prefix;
                if(is("++")||is("--")){prefix=peek().text;++at;}
                const bool handleAssignment=take("@");
                std::string id;if(!name(id)||!qualified(id))return false;
                const bool instanceOnly=id=="this";
                if(instanceOnly&&inFreeFunction)return fail("Standalone functions cannot use this.");
                if(instanceOnly&&(!expect(".")||!name(id)))return false;
                if(!prefix.empty()&&(handleAssignment||id.find("::")!=std::string::npos||!is(";")))
                    return fail("Prefix increment requires a standalone plain local or field.");
                // A local declaration creates an execution-ordered capture, not an instance field.
                if(!instanceOnly&&id=="Entity"&&take("@"))id+="@";
                if(!instanceOnly&&!handleAssignment&&blueprintSpec("capture:"+id)&&peek().kind==Kind::Name)
                {
                    const auto type=id;std::string local;
                    if(!name(local))return false;
                    const auto& params=doc.graphs[graph].parameters;
                    if(locals.count(local)||fields.count(local)||local==eventArg||
                        std::any_of(params.begin(),params.end(),[&](const BlueprintPin& p){return p.name==local;}))
                        return fail("Local shadowing is not supported: "+local+".");
                    Value v;if(!expect("=")||!directValue(v,flow)||!expect(";"))return false;
                    size_t n;if(!add("capture:"+type,n,local)||!bind(n,"value",v))return false;
                    chain(flow,n);locals.emplace(local,Value{type,"","result",n});return true;
                }
                Value target;const Value* member=nullptr;
                if(take(".")){if(!field(id,target,instanceOnly)||!name(id))return false;member=&target;}
                const bool increment=!prefix.empty()||is("++")||is("--");
                const bool compound=is("+=")||is("-=")||is("*=")||is("/=")||is("%=");
                if(is("=")||increment||compound)
                {
                    const auto op=prefix.empty()?peek().text:prefix;
                    if(prefix.empty())++at;
                    Value v;
                    if(increment||compound)
                    {
                        // Compound operations are supported only for plain locals/fields. Do not
                        // move an effectful getter or receiver across a native action call.
                        if(member||id.find("::")!=std::string::npos||handleAssignment)
                            return fail("Compound property or handle assignment is not supported.");
                        Value before;if(!field(id,before,instanceOnly))return false;
                        if(increment)
                        {
                            if(before.type!="int"&&before.type!="uint"&&before.type!="float"&&before.type!="double")
                                return fail("Increment requires a numeric local or field.");
                            v={before.type,"1","",static_cast<size_t>(-1)};
                        }
                        else if(!expression(v))return false;
                        const char* operation=op[0]=='+'?"add":op[0]=='-'?"subtract":op[0]=='*'?"multiply":op[0]=='/'?"divide":"mod";
                        const auto kind=std::string(operation)+":"+before.type;
                        const auto* spec=blueprintSpec(kind);
                        if(!spec||v.type!=before.type||spec->result!=before.type)return fail("Unsupported compound assignment types.");
                        size_t n;if(!add(kind,n)||!bind(n,"a",before)||!bind(n,"b",v))return false;
                        v={spec->result,"","result",n};
                    }
                    else if(!directValue(v,flow))return false;
                    if(!expect(";"))return false;
                    if(!member&&id.find("::")==std::string::npos)
                    {
                        const auto local=locals.find(id);
                        if(!instanceOnly&&local!=locals.end())
                        {
                            const auto& ref=local->second;
                            if(ref.pin=="index")return fail("The imported loop index cannot be reassigned.");
                            if(handleAssignment!=(ref.type=="Entity@"))return fail("Entity handle reassignment requires explicit @; scalar assignment must not use @.");
                            size_t n;if(!add("local:set:"+ref.type,n)||!bind(n,"local",Value{"local:"+ref.type,"","local",ref.node})||!bind(n,"value",v))return false;
                            chain(flow,n);return true;
                        }
                        if(handleAssignment)return fail("Handle assignment requires a declared entity local.");
                        const auto found=fields.find(id);
                        const auto& params=doc.graphs[graph].parameters;
                        if(found==fields.end()||(!instanceOnly&&(id==eventArg||std::any_of(params.begin(),params.end(),[&](const BlueprintPin& p){return p.name==id;}))))
                            return fail("Assignment requires a declared instance field, not a method parameter.");
                        size_t n;if(!add("var:set:"+found->second,n,id)||!bind(n,"value",v))return false;
                        chain(flow,n);
                    }
                    else
                    {
                        if(handleAssignment)return fail("Handle property assignment is not supported.");
                        Value unused;if(!api(id,{v},member,false,"set",unused,&flow))return false;
                    }
                    return true;
                }
                if(handleAssignment)return fail("Expected explicit handle reassignment.");
                if(member&&member->type=="Entity@"&&id=="add")
                {
                    std::string behaviour;
                    if(!expect("(")||!name(behaviour))return false;
                    const auto names=doc.behaviours();
                    if(std::find(names.begin(),names.end(),behaviour)==names.end())return fail("Expected a declared Behaviour constructor.");
                    if(!expect("(")||!expect(")")||!expect(")")||!expect(";"))return false;
                    size_t n;if(!add("attach:direct",n,behaviour)||!bind(n,"target",*member))return false;
                    chain(flow,n);return true;
                }
                std::vector<Value> args;Value unused;
                if(!arguments(args)||!expect(";"))return false;
                if(!member&&doc.function(doc.graphs[graph].owner,id))
                {if(!functionCall(id,args,flow,unused,instanceOnly))return false;}
                else
                {
                    if(instanceOnly&&!member)return fail("Unknown instance method: "+id+".");
                    if(!api(id,args,member,false,"",unused,&flow))return false;
                }
                return true;
            }
            bool parse()
            {
                doc.create();
                // Register all declarations before converting any body, including forward calls.
                struct Method {size_t graph,body,end;std::string arg,type;bool standalone;};
                std::vector<Method> bodies;
                std::unordered_map<std::string,std::unordered_map<std::string,std::string>> classFields;
                std::unordered_set<std::string> classNames;
                auto declaration=[&](bool standalone,const std::string& owner)
                {
                    auto& declaredFields=classFields[owner];
                    const bool behaviour=owner.rfind("behaviour:",0)==0;
                    std::string type,id;if(!name(type)||!name(id))return false;
                    if(is(";")||is("="))
                    {
                        if(standalone)return fail("Global variables are not supported.");
                        if(!blueprintLiteralType(type)||(behaviour&&id=="entity")||!declaredFields.emplace(id,type).second)return fail("Only unique primitive instance fields are supported.");
                        BlueprintDocument::Field field{id,type,"",false};
                        if(take("="))
                        {
                            field.initialized=true;
                            const bool negative=take("-");Value v;
                            if(peek().kind==Kind::Number){if(!numeric(v))return false;}
                            else if(!negative&&peek().kind==Kind::String){v.type="string";v.literal=peek().text;++at;}
                            else if(!negative&&(is("true")||is("false"))){v.type="bool";v.literal=peek().text;++at;}
                            else return fail("Field initializer must be a supported typed literal.");
                            if(v.type!=type||(negative&&(type=="uint"||type=="bool"||type=="string")))return fail("Field initializer requires an exact literal type.");
                            field.value=(negative?"-":"")+v.literal;
                        }
                        if(!expect(";"))return false;
                        const auto entry=std::find_if(doc.graphs.begin(),doc.graphs.end(),[&](const auto& g){return g.owner==owner&&g.name==(behaviour?"start":"onEnter");});
                        if(entry==doc.graphs.end()||entry->fields.size()>=256)return fail("Field declaration limit exceeded.");
                        entry->fields.push_back(std::move(field));return true;
                    }
                    if(!methods.insert(owner+"/"+id).second)return fail("Overloaded or duplicate method "+id+".");
                    const auto found=std::find_if(doc.graphs.begin(),doc.graphs.end(),[&](const BlueprintDocument::Graph& g){return g.owner==owner&&g.name==id;});
                    const bool event=found!=doc.graphs.end();
                    if(standalone&&event)return fail("Standalone functions cannot use a lifecycle event name.");
                    if(standalone)freeFunctions.insert(id);
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
                        else if(behaviour&&(id=="onHit"||id=="onLeave"))
                        {
                            if(!expect("Entity")||!expect("@")||!name(arg)||arg=="entity")return fail("Expected collision partner Entity@ parameter.");
                            argType="Entity@";
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
                        if(!doc.addFunction(id,error,owner))return false;
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
                    bodies.push_back({methodGraph,body,at,arg,argType,standalone});
                    return true;
                };
                bool sawScreen=false;
                while(peek().kind!=Kind::End)
                {
                    if(take("class"))
                    {
                        std::string className,base;
                        if(!name(className)||!expect(":")||!name(base)||!expect("{"))return false;
                        if(!classNames.insert(className).second)return fail("Duplicate class name.");
                        std::string owner;
                        if(base=="Screen")
                        {
                            if(sawScreen)return fail("Only one Screen class is supported.");
                            owner="Screen";doc.name=className;sawScreen=true;
                        }
                        else if(base=="Behaviour")
                        {
                            if(!doc.addBehaviour(className,error))return false;
                            owner="behaviour:"+className;
                        }
                        else return fail("Only Screen and Behaviour classes are supported.");
                        while(!take("}"))if(!declaration(false,owner))return false;
                        take(";");
                    }
                    else if(!declaration(true,"Screen"))return false;
                }
                if(!sawScreen)return fail("Expected one Screen class.");
                for(const auto& id:freeFunctions)if(classFields["Screen"].count(id))return fail("Standalone function and Screen field have the same name: "+id+".");
                for(const auto& method:bodies)
                {
                    graph=method.graph;at=method.body;eventArg=method.arg;eventType=method.type;locals.clear();
                    inFreeFunction=method.standalone;
                    // A standalone function has no implicit instance state. Lower it to an
                    // owner-local function graph only when its entire body is convertible.
                    fields=inFreeFunction?std::unordered_map<std::string,std::string>{}:classFields[doc.graphs[graph].owner];
                    const bool ok=block({},0)&&at==method.end;
                    if(!ok)return fail("Method body could not be converted completely.");
                }
                return true;
            }
        };
        void layoutImportedGraphs(BlueprintDocument& doc)
        {
            for(auto& g:doc.graphs)
            {
                const size_t count=g.nodes.size();
                std::vector<bool> exec(count,false);
                std::vector<size_t> group(count,0),column(count,0);
                std::vector<float> height(count,72.f);
                size_t groups=0;
                for(size_t i=0;i<count;++i)
                {
                    BlueprintNodeSpec spec;
                    if(!doc.nodeSpec(g,g.nodes[i],spec))continue;
                    exec[i]=g.nodes[i].kind=="entry"||std::any_of(spec.inputs.begin(),spec.inputs.end(),
                        [](const BlueprintPin& p){return p.type=="exec";});
                    if(exec[i])group[i]=groups++;
                    height[i]=std::max(72.f,46.f+22.f*static_cast<float>(std::max(spec.inputs.size(),spec.outputs.size())));
                }
                if(!groups)continue;
                // Imported value nodes precede their consumers. Walk backwards to put
                // dependencies in columns left of their first execution consumer.
                for(size_t i=count;i-- >0;)
                {
                    if(exec[i])continue;
                    size_t first=groups,rank=1;
                    for(const auto& link:g.links)if(link.from==i&&link.to<count)
                    {
                        first=std::min(first,group[link.to]);
                        rank=std::max(rank,column[link.to]+1);
                    }
                    group[i]=first==groups?0:first;
                    column[i]=rank;
                }
                float y=60.f;
                for(size_t row=0;row<groups;++row)
                {
                    std::vector<float> offsets(count+1,0.f);
                    float tallest=0.f;
                    for(size_t i=0;i<count;++i)if(group[i]==row)
                    {
                        const size_t col=std::min(column[i],count);
                        g.nodes[i].x=360.f-252.f*static_cast<float>(col);
                        g.nodes[i].y=y+offsets[col];
                        offsets[col]+=height[i]+36.f;
                        tallest=std::max(tallest,offsets[col]);
                    }
                    y+=std::max(108.f,tallest);
                }
            }
        }
    }
    namespace
    {
        bool attachment(const BlueprintDocument::Node& n)
        {return n.kind=="attach:direct"||n.kind=="attach:behaviour"||n.kind=="level:attach";}
        bool sameIndependentBehaviour(const BlueprintDocument& a,const BlueprintDocument& b,const std::string& owner)
        {
            std::vector<const BlueprintDocument::Graph*> left,right;
            for(const auto& g:a.graphs)if(g.owner==owner)left.push_back(&g);
            for(const auto& g:b.graphs)if(g.owner==owner)right.push_back(&g);
            if(left.size()!=right.size()||left.empty())return false;
            for(const auto* x:left)
            {
                const auto found=std::find_if(right.begin(),right.end(),[&](const auto* g){return g->name==x->name;});
                if(found==right.end())return false;
                const auto& y=**found;
                if(x->kind!=y.kind||x->returnType!=y.returnType||x->parameters.size()!=y.parameters.size()||
                    x->nodes.size()!=y.nodes.size()||x->links.size()!=y.links.size()||x->fields.size()!=y.fields.size())return false;
                for(size_t i=0;i<x->fields.size();++i)
                {
                    const auto& f=x->fields[i];const auto& h=y.fields[i];
                    if(f.name!=h.name||f.type!=h.type||f.initialized!=h.initialized||f.value!=h.value)return false;
                }
                for(size_t i=0;i<x->parameters.size();++i)
                    if(x->parameters[i].name!=y.parameters[i].name||x->parameters[i].type!=y.parameters[i].type)return false;
                for(size_t i=0;i<x->nodes.size();++i)
                {
                    const auto& n=x->nodes[i];const auto& m=y.nodes[i];
                    // Do not deduplicate classes with dependencies on other Behaviours.
                    if(attachment(n)||attachment(m)||n.kind!=m.kind||n.ref!=m.ref||n.watch!=m.watch||n.values.size()!=m.values.size())return false;
                    for(size_t j=0;j<n.values.size();++j)
                        if(n.values[j].pin!=m.values[j].pin||n.values[j].value!=m.values[j].value)return false;
                }
                for(size_t i=0;i<x->links.size();++i)
                {
                    const auto& l=x->links[i];const auto& r=y.links[i];
                    if(l.from!=r.from||l.to!=r.to||l.output!=r.output||l.input!=r.input)return false;
                }
            }
            return true;
        }
    }
    bool mergeImportedBlueprintScreen(BlueprintDocument& project,BlueprintDocument screen,const std::string& owner,std::string& error)
    {
        BlueprintDocument probe;
        const auto existingScreens=project.screens();
        if(!probe.addScreen(owner,error)||std::find(existingScreens.begin(),existingScreens.end(),owner)!=existingScreens.end()||
            screen.screens()!=std::vector<std::string>{"Screen"})
        {error="Invalid or duplicate imported screen: "+owner;return false;}
        std::string verified;
        if(!screen.serialize(verified,error))return false;
        const auto existing=project.behaviours(),incoming=screen.behaviours();
        std::unordered_set<std::string> used(existing.begin(),existing.end()),skip;
        used.insert(incoming.begin(),incoming.end());
        std::unordered_map<std::string,std::string> names;
        for(const auto& name:incoming)
        {
            names[name]=name;
            if(std::find(existing.begin(),existing.end(),name)==existing.end())continue;
            if(sameIndependentBehaviour(project,screen,"behaviour:"+name)){skip.insert(name);continue;}
            std::string prefix=owner;
            std::replace(prefix.begin(),prefix.end(),'-','_');
            for(unsigned suffix=1;;++suffix)
            {
                const auto ending="_"+std::to_string(suffix);
                const auto renamed=(prefix+"_"+name).substr(0,64-ending.size())+ending;
                if(used.insert(renamed).second){names[name]=renamed;break;}
            }
        }
        std::vector<BlueprintDocument::Graph> additions;
        for(auto& g:screen.graphs)
        {
            if(g.owner=="Screen")g.owner=owner;
            else
            {
                const auto old=g.owner.substr(10);
                if(skip.count(old))continue;
                g.owner="behaviour:"+names.at(old);
            }
            for(auto& n:g.nodes)if(attachment(n))n.ref=names.at(n.ref);
            additions.push_back(std::move(g));
        }
        if(project.graphs.size()+additions.size()>64){error="Project exceeds 64 graph limit.";return false;}
        for(auto& g:additions)project.graphs.push_back(std::move(g));
        error.clear();return true;
    }
    bool resolveBlueprintIncludes(const std::string& source,const std::string& label,const BlueprintIncludeReader& read,std::string& expanded,std::string& error)
    {
        constexpr size_t limit=512*1024;
        std::unordered_set<std::string> active,done;
        std::string result;
        size_t total=0;
        auto portable=[](const std::string& name)
        {
            size_t start=name.rfind("asset:",0)==0?6:name.rfind("global:",0)==0?7:0;
            if(!start||name.size()>100||!name.ends_with(".as"))return false;
            while(start<name.size())
            {
                const auto slash=name.find('/',start);
                const auto part=name.substr(start,slash==std::string::npos?slash:slash-start);
                if(!blueprintAssetName(part,slash!=std::string::npos))return false;
                if(slash==std::string::npos)return true;
                start=slash+1;
            }
            return false;
        };
        std::function<bool(const std::string&,const std::string&,unsigned)> visit;
        visit=[&](const std::string& code,const std::string& file,unsigned depth)
        {
            if(depth>5||code.empty()||code.size()>limit-total||code.find('\0')!=std::string::npos)
            {error=file+": empty, binary, oversized or too-deep include tree.";return false;}
            total+=code.size();
            std::string body=code;
            if(body.rfind("\xef\xbb\xbf",0)==0)body.replace(0,3,"   ");
            size_t line=1;
            for(size_t at=0;at<body.size();++line)
            {
                const auto next=body.find('\n',at);
                const auto end=next==std::string::npos?body.size():next;
                size_t i=at;while(i<end&&(body[i]==' '||body[i]=='\t'))++i;
                if(i<end&&body[i]=='#')
                {
                    auto fail=[&](const std::string& why){error=file+":"+std::to_string(line)+": "+why;return false;};
                    if(body.compare(i,8,"#include")!=0||i+8>=end||(body[i+8]!=' '&&body[i+8]!='\t'))return fail("Unsupported preprocessor directive.");
                    i+=8;while(i<end&&(body[i]==' '||body[i]=='\t'))++i;
                    if(i>=end||body[i]!='"')return fail("Expected quoted include path.");
                    const auto close=body.find('"',++i);
                    if(close==std::string::npos||close>=end)return fail("Unclosed include path.");
                    const auto name=body.substr(i,close-i);
                    i=close+1;while(i<end&&(body[i]==' '||body[i]=='\t'||body[i]=='\r'))++i;
                    if(i<end&&body.compare(i,2,"//")!=0)return fail("Unexpected text after include.");
                    if(!portable(name))return fail("Include must be a portable asset: or global: .as path: "+name);
                    if(active.count(name))return fail("Cyclic include: "+name);
                    if(!done.count(name))
                    {
                        if(done.size()+active.size()>=32)return fail("Include file count limit exceeded.");
                        std::string dependency;
                        if(!read||!read(name,dependency))return fail("Missing include: "+name);
                        active.insert(name);
                        if(!visit(dependency,name,depth+1))return false;
                        active.erase(name);done.insert(name);
                    }
                    std::fill(body.begin()+static_cast<std::ptrdiff_t>(at),body.begin()+static_cast<std::ptrdiff_t>(end),' ');
                }
                at=next==std::string::npos?body.size():next+1;
            }
            if(body.size()+1>limit-result.size()){error=file+": expanded source exceeds import limit.";return false;}
            result+=body;result+='\n';return true;
        };
        if(!visit(source,label,0))return false;
        expanded.swap(result);error.clear();return true;
    }
    bool importBlueprintScreen(const std::string& source,BlueprintDocument& document,std::string& error)
    {
        error.clear();std::vector<Token> tokens;
        if(!lex(source,tokens,error))return false;
        Parser parser{{},tokens,error,0,0,0,{},{},{},{},{}};
        if(!parser.parse())
        {
            if(error.empty())error="Source conversion failed.";
            if(error.rfind("Source line ",0)!=0)error="Source line "+std::to_string(parser.peek().line)+": "+error;
            return false;
        }
        layoutImportedGraphs(parser.doc);
        std::string json,generated;
        if(!parser.doc.serialize(json,error)||!compileBlueprint(parser.doc,generated,error))
        {error="Imported graph cannot be represented safely: "+error;return false;}
        BlueprintDocument candidate;
        if(!candidate.load(json.data(),json.size(),error))return false;
        candidate.markRecovered();document=std::move(candidate);error.clear();return true;
    }
}
