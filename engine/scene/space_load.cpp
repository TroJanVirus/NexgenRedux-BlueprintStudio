#include "scene/space.h"
#include "scene/world.h"
#include "platform/platform.h"

#include <angelscript.h>

#include <stdio.h>
#include <string.h>

// Things in space, from a file: a region of a map, or a whole one.
namespace nx
{
    using Json = JsonTree::Value;

    namespace
    {
        Vec3 triple(Json json, const Vec3& otherwise)
        {
            if (json.isNumber())
            {
                return Vec3(1, 1, 1) * json.number();
            }
            return json.isArray() ? Vec3(json[0u].number(otherwise.x), json[1u].number(otherwise.y), json[2u].number(otherwise.z)) : otherwise;
        }
    }

    int Space::load(const char* path, const char* folder, const Vec3& at, bool streamed)
    {
        FileData file;
        if (!m_assets.platform().readFile(path, file, MemTag::Scene))
        {
            m_world.m_error[0] = 0;
            m_world.fail(path, 0, "%s", "cannot be read");
            return -1;
        }
        return load(path, file, folder, at, streamed);
    }

    int Space::load(const char* path, const FileData& file, const char* folder, const Vec3& at, bool streamed)
    {
        m_world.m_error[0] = 0;
        snprintf(m_world.m_folder, sizeof(m_world.m_folder), "%s", folder);
        JsonTree json;
        if (!json.parse(reinterpret_cast<const char*>(file.data), file.size))
        {
            m_world.fail(path, json.errorLine(), "%s", json.error());
            return -1;
        }
        int count = 0;
        for (Json v = json.root()["entities"].first(); v.exists(); v = v.next())
        {
            u32 id = 0;
            if (!readThing(v, at, streamed, path, id))
            {
                return -1;
            }
            count++;
        }
        return count;
    }

    u32 Space::make(Json json, const char* folder, const Vec3& at)
    {
        m_world.m_error[0] = 0;
        snprintf(m_world.m_folder, sizeof(m_world.m_folder), "%s", folder);
        u32 id = 0;
        if (readThing(json, at, false, "json", id))
        {
            return id;
        }
        if (id)
        {
            m_world.destroyEntity(id);
        }
        return 0;
    }

    u32 Space::makeActor(Json json, const char* folder, const Vec3& at)
    {
        m_world.m_error[0]=0;
        const auto entities=json["entities"],parents=json["parents"];
        const u32 count=entities.count();
        const auto invalid=[&]()->u32
        {
            m_world.fail("actor",json.line(),"%s","Invalid actor hierarchy");
            return 0;
        };
        if(!json.isObject()||!entities.isArray()||!count||count>32||
            json.count()!=(parents.exists()?2u:1u)||
            (parents.exists()?(!parents.isArray()||parents.count()!=count):count!=1))return invalid();
        u32 parent[32]={},depth[32]={},ids[32]={};
        for(u32 i=0;i<count;++i)
        {
            const auto e=entities[i];
            if(!e.isObject()||(i&&e["onLand"].exists()))return invalid();
            if(!parents.exists())continue;
            const auto p=parents[i];
            if(!p.isNumber())return invalid();
            const double n=p.exact();
            if(!i){if(n!=-1)return invalid();continue;}
            if(!(n>=0&&n<i))return invalid();
            parent[i]=static_cast<u32>(n);
            if(parent[i]!=n)return invalid();
            depth[i]=depth[parent[i]]+1;
            if(depth[i]>8)return invalid();
        }
        for(u32 i=0;i<count;++i)
        {
            // Only the root receives the placement offset. Children retain
            // their authored local transforms under their native parent.
            ids[i]=make(entities[i],folder,i?Vec3():at);
            if(!ids[i])
            {
                if(i)m_world.destroyEntity(ids[0]);
                return 0;
            }
            if(i)m_world.setParent(ids[i],ids[parent[i]]);
        }
        for(u32 i=0;i<count;++i)if(const auto* camera=getSpatial(ids[i]);camera&&camera->cameraFov>0)m_cameraEntity=ids[i];
        refreshActorCamera(false);
        return ids[0];
    }

    bool Space::readThing(Json json, const Vec3& at, bool streamed, const char* file, u32& id)
    {
        id = m_world.spawn({});
        if (id == 0)
        {
            return m_world.fail(file, json.line(), "%s", "too many entities");
        }
        Spatial* s = spatial(id);
        if (s == nullptr)
        {
            return false;
        }
        const u32 index = id & 0xFFFF;
        s->place = at + triple(json["place"], {});
        s->streamed = streamed;
        if (json["onLand"].flag())
        {
            // Up from the ground there.
            s->place.y = heightAt(s->place.x, s->place.z) + triple(json["place"], {}).y;
        }
        const Json angles = json["angles"];
        s->turn = Quat::fromAngles(angles.isNumber() ? Vec3(0, angles.number(), 0) : triple(angles, {}));
        s->size = triple(json["size"], { 1, 1, 1 });
        u32 group = 0, hits = 0;
        if (json["group"].exists())
        {
            u32 mask = 0;
            if (!m_world.readGroups(json["group"], mask, file))
            {
                return false;
            }
            while (group < 15 && !(mask & (1u << group)))
            {
                group++;
            }
        }
        if (json["hits"].exists() && !m_world.readGroups(json["hits"], hits, file))
        {
            return false;
        }
        for (Json v = json.first(); v.exists(); v = v.next())
        {
            const char* key = v.key();
            const auto is = [key](const char* name) { return strcmp(key, name) == 0; };
            if (is("place") || is("onLand") || is("angles") || is("size") || is("group") || is("hits") || is("colliderAt") || is("wall") || is("range") || is("rate") || is("reach") || is("volume") || is("seconds") || is("pathTime"))
            {
                continue;
            }
            if (is("name"))
            {
                snprintf(m_world.m_entities[index].name, sizeof(m_world.m_entities[index].name), "%s", v.text());
            }
            else if (is("model"))
            {
                Asset* model = m_world.readAsset(v, AssetType::Model, 0, file);
                if (model == nullptr)
                {
                    return false;
                }
                setModel(id, model);
                m_assets.release(model);
            }
            else if (is("skin"))
            {
                Asset* texture = m_world.readAsset(v, AssetType::Texture, 0, file);
                if (texture == nullptr)
                {
                    return false;
                }
                setTexture(id, texture);
                m_assets.release(texture);
            }
            else if (is("sprite"))
            {
                // A picture that faces the camera; "views" across it and "frames" down.
                Asset* sheet = m_world.readAsset(v, AssetType::Texture, 0, file);
                if (sheet == nullptr)
                {
                    return false;
                }
                const Appearance* a = appearance(id);
                setSprite(id, sheet, a && a->spriteAngles ? a->spriteAngles : 1, a && a->spriteFrames ? a->spriteFrames : 1);
                m_assets.release(sheet);
            }
            else if (is("views") || is("frames"))
            {
                if (Appearance* a = appearance(id))
                {
                    const double n = v.number();
                    const u8 count = static_cast<u8>(n < 1 ? 1 : n > 64 ? 64 : n);
                    (is("views") ? a->spriteAngles : a->spriteFrames) = count;
                }
            }
            else if (is("sound"))
            {
                Asset* sound = m_world.readAsset(v, AssetType::Sound, 0, file);
                if (sound == nullptr)
                {
                    return false;
                }
                const float reach = json["reach"].number(12);
                if (!m_waiting.push({ sound, id, json["volume"].number(1), reach > 0.1f ? reach : 0.1f }))
                {
                    m_assets.release(sound);
                }
            }
            else if(is("camera"))
            {
                if(!v.isNumber()||!(v.exact()>=5&&v.exact()<=150))return m_world.fail(file,v.line(),"%s","Camera FOV must be 5 to 150 degrees");
                s->cameraFov=v.number();
            }
            else if (is("shape"))
            {
                static const char* const names[] = { "none", "box", "sphere", "cylinder", "cone", "torus", "capsule", "plane" };
                u32 solid = 0;
                while (solid < 8 && strcmp(names[solid], v.text()) != 0)
                {
                    solid++;
                }
                if (solid == 8)
                {
                    return m_world.fail(file, v.line(), "%s isn't a shape", v.text());
                }
                setSolid(id, static_cast<Solid>(solid));
            }
            else if (is("color"))
            {
                Color color;
                if (!m_world.readColor(v, color, file))
                {
                    return false;
                }
                if (Appearance* a = appearance(id))
                {
                    a->color = color;
                    // What can be seen through.
                    if (color.a < 1 && !json["glow"].exists())
                    {
                        a->blend = gpu::Blend::Alpha;
                        a->casts = false;
                    }
                }
            }
            else if (is("shine") || is("glowing") || is("twoSided") || is("shadow") || is("sway") || is("impostor"))
            {
                if (Appearance* a = appearance(id))
                {
                    a->shine = is("shine") ? v.number() : a->shine;
                    a->sway = is("sway") ? v.number() : a->sway;
                    a->impostor = is("impostor") ? v.flag() : a->impostor;
                    a->glow = is("glowing") ? v.number() : a->glow;
                    a->twoSided = is("twoSided") ? v.flag() : a->twoSided;
                    a->casts = is("shadow") ? v.flag() : a->casts;
                }
            }
            else if (is("glow"))
            {
                if (Appearance* a = appearance(id))
                {
                    a->glowBall = true;
                    a->casts = false;
                    a->blend = gpu::Blend::Additive;
                    getSpatial(id)->size = Vec3(1, 1, 1) * v.number();
                }
            }
            else if (is("light"))
            {
                Color color;
                if (!m_world.readColor(v, color, file))
                {
                    return false;
                }
                if (Light3* l = light(id))
                {
                    l->color = color;
                    l->range = json["range"].number(8);
                }
            }
            else if (is("sphere") || is("cube") || is("capsule"))
            {
                if (Collider3* c = collider(id))
                {
                    c->shape = is("sphere") ? Shape3::Sphere : is("cube") ? Shape3::Box : Shape3::Capsule;
                    c->half = is("capsule") ? Vec3(v[0u].number(0.5f), v[1u].number(2) * 0.5f, 0) : triple(v, { 1, 1, 1 }) * (is("cube") ? 0.5f : 1.0f);
                    c->group = static_cast<u8>(group);
                    c->hits = static_cast<u16>(hits);
                    c->wall = json["wall"].flag();
                    c->middle = triple(json["colliderAt"], {});
                }
            }
            else if (is("exact"))
            {
                Collider3* c = v.flag() ? collider(id) : nullptr;
                if (c)
                {
                    c->shape = Shape3::Shell;
                    c->half = { 0.01f, 0.01f, 0.01f };
                    c->group = static_cast<u8>(group);
                    c->hits = static_cast<u16>(hits);
                    c->wall = json["wall"].flag();
                }
            }
            else if (is("mass") || is("friction") || is("bounce") || is("gravity") || is("drag") || is("walker"))
            {
                if (Body3* b = body(id))
                {
                    if (is("mass"))
                    {
                        b->mass = v.number();
                        b->gravity = json["gravity"].exists() ? b->gravity : 1;
                    }
                    b->friction = is("friction") ? v.number() : b->friction;
                    b->bounce = is("bounce") ? v.number() : b->bounce;
                    b->gravity = is("gravity") ? v.number() : b->gravity;
                    b->drag = is("drag") ? v.number() : b->drag;
                    if (is("walker"))
                    {
                        b->walker = v.flag();
                        b->gravity = json["gravity"].exists() ? b->gravity : 1;
                    }
                }
            }
            else if (is("animate"))
            {
                // Once its model has come, which is soon after.
                if (!play(id, v.text(), true, 0))
                {
                    pendingAnimation(id, v.text());
                }
            }
            else if (is("moves"))
            {
                // To there and back, for ever.
                if (Body3* b = body(id))
                {
                    b->pathFrom = getSpatial(id)->place;
                    b->pathBy = triple(v, {});
                    b->pathSeconds = json["seconds"].number(6);
                    b->pathTime = json["pathTime"].number();
                }
            }
            else if (is("emit"))
            {
                if (!emit(id, v.text(), json["rate"].number(20)))
                {
                    return m_world.fail(file, v.line(), "no particles called %s", v.text());
                }
            }
            else if (is("behaviour"))
            {
                asITypeInfo* type = m_world.m_host.findType(m_world.m_module, v.text());
                asIScriptObject* object = type ? m_world.m_host.create(type) : nullptr;
                const bool added = object && m_world.addBehaviour(id, object);
                if (object)
                {
                    m_world.m_host.release(object);
                }
                if (!added)
                {
                    return m_world.fail(file, v.line(), "no behaviour called %s", v.text());
                }
            }
            else
            {
                return m_world.fail(file, v.line(), "what is \"%s\"?", key);
            }
        }
        transform(index);
        return true;
    }
}
