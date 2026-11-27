#include "screens/screens.h"
#include "ui/ui.h"
#include "base/json.h"
#include "base/log.h"
#include "bindings/bindings.h"
#include "video/video.h"
#include "save/save.h"
#include <algorithm>

#include <stdio.h>
#include <string.h>

namespace nx
{
    namespace
    {
        const char kResume[] = "Resume";
        const char kRestart[] = "Restart";
        const char kRetry[] = "Try again";
        const char kHome[] = "Main menu";
        const char kQuit[] = "Quit";
        const char kBlueprint[] = "Blueprint Studio";
    }

    bool Screens::init(Services& services, const char* folder, const char* home)
    {
        m_services = &services;
        services.screens = this;
        snprintf(m_folder, sizeof(m_folder), "%s", folder);
        services.platform->listFolders(folder, [](const char* name, void* self) { static_cast<Screens*>(self)->addInfo(name); }, this);

        // Insertion sort: the order in screen.json, then by title.
        for (u32 i = 1; i < m_count; i++)
        {
            const ScreenInfo info = m_infos[i];
            u32 j = i;
            while (j > 0 && (m_infos[j - 1].order > info.order || (m_infos[j - 1].order == info.order && strcmp(m_infos[j - 1].title, info.title) > 0)))
            {
                m_infos[j] = m_infos[j - 1];
                j--;
            }
            m_infos[j] = info;
        }

        services.canvas->design(1280, 720);
        m_font = services.assets->font("global/freesans.sfn", 34);
        m_smallFont = services.assets->font("global/freesans.sfn", 22);

        const ScreenInfo* first = findInfo(home);
        if (first == nullptr)
        {
            NX_ERROR("no screen called %s in %s", home, folder);
            return false;
        }
        return open(*first);
    }

    void Screens::shutdown()
    {
        if (m_services == nullptr)
        {
            return;
        }
#ifdef NX_DESKTOP_BLUEPRINT
        stopPreview();
#endif
        while (m_depth)
        {
            close(true);
        }
        m_services->assets->release(m_font);
        m_services->assets->release(m_smallFont);
        m_services = nullptr;
    }

    void Screens::addInfo(const char* folderName)
    {
        if (m_count == kMaxScreens || strlen(folderName) >= sizeof(ScreenInfo::id))
        {
            NX_WARN("screen left out: %s", folderName);
            return;
        }
        char path[192];
        snprintf(path, sizeof(path), "%s/%s/screen.json", m_folder, folderName);
        FileData file;
        ScreenInfo info;
        if (!m_services->platform->readFile(path, file, MemTag::General) || !jsonString(reinterpret_cast<const char*>(file.data), "title", info.title, sizeof(info.title)))
        {
            NX_WARN("%s needs a title", path);
            return;
        }
        const char* text = reinterpret_cast<const char*>(file.data);
        snprintf(info.id, sizeof(info.id), "%s", folderName);
        jsonString(text, "description", info.description, sizeof(info.description));
        jsonString(text, "category", info.category, sizeof(info.category));
        jsonString(text, "demo", info.demo, sizeof(info.demo));
        double seconds = 0;
        jsonNumber(text, "seconds", seconds);
        info.seconds = static_cast<float>(seconds);
        double order = 1000;
        jsonNumber(text, "order", order);
        info.order = static_cast<int>(order);
        m_infos[m_count++] = info;
    }

    u32 Screens::count() const
    {
#ifdef NX_DESKTOP_BLUEPRINT
        if(previewing())return static_cast<u32>(m_previewScreens.size());
#endif
        return m_count;
    }
    const ScreenInfo* Screens::info(u32 index) const
    {
#ifdef NX_DESKTOP_BLUEPRINT
        if(previewing())return index<m_previewScreens.size()?&m_previewScreens[index].info:nullptr;
#endif
        return index<m_count?&m_infos[index]:nullptr;
    }
    const ScreenInfo* Screens::findInfo(const char* id) const
    {
#ifdef NX_DESKTOP_BLUEPRINT
        if(previewing())
        {
            for(const auto& screen:m_previewScreens)if(strcmp(screen.info.id,id)==0)return &screen.info;
            return nullptr;
        }
#endif
        for (u32 i = 0; i < m_count; i++)
        {
            if (strcmp(m_infos[i].id, id) == 0)
            {
                return &m_infos[i];
            }
        }
        return nullptr;
    }

#ifdef NX_DESKTOP_BLUEPRINT
    bool Screens::startPreview(const std::string& source, std::string& error)
    {
        PreviewScreen screen;
        snprintf(screen.info.id,sizeof(screen.info.id),"__blueprint_preview");
        snprintf(screen.info.title,sizeof(screen.info.title),"Blueprint play test");
        screen.source=source;
        const bool valid=startProjectPreview({screen},screen.info.id,error);
        if(valid){m_previewNavigation=false;m_change=Change::None;}
        return valid;
    }

    bool Screens::startProjectPreview(const std::vector<PreviewScreen>& screens,const std::string& start,std::string& error,const std::string& assetRoot)
    {
        const auto selected=std::find_if(screens.begin(),screens.end(),[&](const PreviewScreen& s){return start==s.info.id;});
        if (!m_services || m_depth != 1 || !m_menuOpen || previewing() || screens.empty() ||
            screens.size()>kMaxScreens || selected==screens.end())
        {
            error = "Preview requires the home screen and a valid generated script.";
            return false;
        }
        if(!assetRoot.empty()&&(assetRoot.size()>32||assetRoot.rfind("bpa-",0)!=0||assetRoot.find_first_not_of("0123456789-",4)!=std::string::npos))
        {error="Invalid preview asset root.";return false;}
        for(size_t i=0;i<screens.size();++i)
        {
            const auto& p=screens[i];
            if(!p.info.id[0]||p.source.empty()||p.source.size()>1024*1024)
            {error="Invalid preview screen.";return false;}
            for(size_t j=0;j<i;++j)if(strcmp(p.info.id,screens[j].info.id)==0)
            {error="Duplicate preview screen ID.";return false;}
        }
        m_previewSave = create<Save>(MemTag::General);
        m_previewSettings = create<Save>(MemTag::General);
        if (!m_previewSave || !m_previewSettings)
        {
            destroy(m_previewSave);destroy(m_previewSettings);m_previewSave=m_previewSettings=nullptr;
            error = "Not enough memory for preview."; return false;
        }
        m_previewSave->setReadOnly(); // No platform or filename: entirely transient.
        m_originalSave = m_services->save;
        m_services->save = m_previewSettings;
        saveSettings(*m_services); // Snapshot settings without writing to the real save.
        m_previewRandom = m_services->random;
        m_previewDevelopment = m_services->development;
        m_previewScreens=screens;m_previewNavigation=true;
        m_previewRoot=assetRoot.empty()?"__blueprint_preview":"own/"+assetRoot;
        m_services->save = m_previewSave;
        m_services->development = false;
        m_services->previewAssets = true;
        m_change = Change::None;
        // Home must already be paused by the workspace. Do not run its suspend/resume
        // callbacks against the temporary save or generated module.
        m_services->audio->pauseGroup(m_stack[0].group, true);
        m_menuOpen = false;
        if (!open(selected->info))
        {
            error = this->error();
            stopPreview();
            return false;
        }
        m_services->input->consume();
        error.clear();
        return true;
    }

    void Screens::stopPreview()
    {
        if (!previewing()) return;
        // Never close or resume the original home while preview state is active.
        while (m_depth > 1) close(true);
        m_change = Change::None;
        m_services->save = m_previewSettings;
        loadSettings(*m_services);
        destroy(m_previewSettings);m_previewSettings=nullptr;
        m_services->save = m_originalSave;
        m_originalSave = nullptr;
        m_services->random = m_previewRandom;
        m_services->development = m_previewDevelopment;
        m_services->previewAssets = false;
        destroy(m_previewSave);
        m_previewSave = nullptr;
        m_previewScreens.clear();m_previewRoot.clear();m_previewNavigation=false;
        if (m_depth) enter(m_stack[m_depth-1]);
        m_menuOpen = false;
        setMenu(true); // The workspace is visible again; keep the home screen paused.
        m_services->input->consume();
    }
#endif

    // --- Requests ---

    void Screens::request(Change change, const char* id)
    {
#ifdef NX_DESKTOP_BLUEPRINT
        if (previewing())
        {
            if(!m_previewNavigation)return;
            if((change==Change::Push||change==Change::Replace)&&!findInfo(id))
            {NX_WARN("Screen %s is not in this preview project",id);return;}
            if((change==Change::Pop&&m_depth<=2)||change==Change::Home)
            {
                if(change==Change::Pop)return;
                // Home within the preview means its root, not the original Redux home.
            }
        }
#endif
        m_change = change;
        snprintf(m_changeId, sizeof(m_changeId), "%s", id);
    }

    void Screens::push(const char* id) { request(Change::Push, id); }
    void Screens::pop() { request(Change::Pop, ""); }
    void Screens::replace(const char* id) { request(Change::Replace, id); }
    void Screens::home() { request(Change::Home, ""); }
    void Screens::restart() { request(Change::Restart, ""); }

    void Screens::applyChanges()
    {
        const Change change = m_change;
        if (change == Change::None)
        {
            return;
        }
        m_change = Change::None;
        setMenu(false);
        m_services->input->consume();

        const ScreenInfo* next = nullptr;
        if (change == Change::Push || change == Change::Replace)
        {
            next = findInfo(m_changeId);
            if (next == nullptr)
            {
                NX_ERROR("no screen called %s", m_changeId);
                return;
            }
        }
        u32 floor=1;
#ifdef NX_DESKTOP_BLUEPRINT
        if(previewing())floor=2;
#endif
        switch (change)
        {
        case Change::Push:
            if (m_depth == kMaxDepth)
            {
                NX_ERROR("too many screens open");
                return;
            }
            callOptional(m_stack[m_depth - 1], "void onSuspend()");
            m_services->audio->pauseGroup(m_stack[m_depth - 1].group, true);
            open(*next);
            break;
        case Change::Pop:
            if (m_depth > floor)
            {
                close(true);
                m_services->audio->pauseGroup(m_stack[m_depth - 1].group, false);
                callOptional(m_stack[m_depth - 1], "void onResume()");
            }
            break;
        case Change::Replace:
            close(true);
            open(*next);
            break;
        case Change::Home:
            if (m_depth > floor)
            {
                while (m_depth > floor)
                {
                    close(true);
                }
                m_services->audio->pauseGroup(m_stack[floor-1].group, false);
                callOptional(m_stack[floor-1], "void onResume()");
            }
            break;
        case Change::Restart:
            if (m_depth)
            {
                Entry& entry = m_stack[m_depth - 1];
                unload(entry, true);
                load(entry);
            }
            break;
        case Change::None:
            break;
        }
        saveSettings(*m_services);
        m_services->save->flush();
        // Anything asked for while closing was meant for a screen that is gone.
        m_change = change == Change::Push || change == Change::Replace || change == Change::Restart ? m_change : Change::None;
    }

    bool Screens::open(const ScreenInfo& info)
    {
        m_services->input->stopRumble();
        m_services->audio->clearEffects(0);
        Entry& entry = m_stack[m_depth++];
        entry = Entry();
        entry.info = info;
        return load(entry);
    }

    void Screens::close(bool callExit)
    {
        m_services->input->stopRumble();
        m_services->audio->clearEffects(0);
        unload(m_stack[m_depth - 1], callExit);
        m_depth--;
    }

    bool Screens::load(Entry& entry)
    {
        ScriptHost& host = *m_services->host;
        entry.failed = false;
        entry.error[0] = 0;
        entry.loading = false;
        entry.loadingFor = 0;
        entry.group = ++m_nextModule;
        snprintf(entry.module, sizeof(entry.module), "screen%u", static_cast<unsigned>(entry.group));
        const bool small = m_services->platform->totalMemory() && m_services->platform->totalMemory() <= 128u * 1024 * 1024;
        entry.world = create<World>(MemTag::Scene, *m_services->assets, *m_services->host, entry.module, small ? 4096u : 16384u, small ? 1500u : 8000u);
        if (entry.world)
        {
            entry.world->space().init(m_services->solids, m_services->audio);
        }
        char folder[160];
        const char* root=m_folder;
#ifdef NX_DESKTOP_BLUEPRINT
        if(previewing())root=m_previewRoot.c_str();
#endif
        snprintf(folder, sizeof(folder), "%s/%s", root, entry.info.id);
        entry.ui = create<Ui>(MemTag::UI, *m_services->canvas, *m_services->assets, *m_services->audio, *m_services->input, host, *m_services->platform, folder);
        enter(entry);
#ifdef NX_DESKTOP_BLUEPRINT
        if (previewing() && (!entry.world || !entry.ui))
        {entry.failed=true;snprintf(entry.error,sizeof(entry.error),"Cannot allocate preview world or UI");return false;}
#endif

        char path[192];
        snprintf(path, sizeof(path), "%s/main.as", m_services->folder);
        FileData source;
        entry.scriptTime = m_services->platform->fileTime(path);
#ifdef NX_DESKTOP_BLUEPRINT
        if (previewing())
        {
            const auto found=std::find_if(m_previewScreens.begin(),m_previewScreens.end(),[&](const PreviewScreen& p){return strcmp(p.info.id,entry.info.id)==0;});
            if(found==m_previewScreens.end()){entry.failed=true;snprintf(entry.error,sizeof(entry.error),"Missing preview source");return false;}
            const auto& code=found->source;
            auto* bytes=static_cast<u8*>(memAlloc(code.size()+1,MemTag::Script));
            if(!bytes){entry.failed=true;snprintf(entry.error,sizeof(entry.error),"Cannot allocate preview source");return false;}
            memcpy(bytes,code.data(),code.size());bytes[code.size()]=0;
            source.take(bytes,code.size());
        }
        else
#endif
        if (!m_services->platform->readFile(path, source, MemTag::Script))
        {
            entry.failed = true;
            snprintf(entry.error, sizeof(entry.error), "cannot read %s", path);
            return false;
        }
        // `#include "global:scene.as"`: other files, which the screen shares. The line itself is made blank.
        std::string included;
        m_included.clear();
        if (!include(source, shown(entry), included, 0))
        {
            entry.failed = true;
            snprintf(entry.error, sizeof(entry.error), "%s", included.c_str());
            return false;
        }
        // Its path is in every error message, so leave out the part that is always the same.
        char shown[96];
        snprintf(shown, sizeof(shown), "%s/main.as", entry.info.id);
        host.setSource(shown, path);
        if (!host.compile(entry.module, shown, reinterpret_cast<const char*>(source.data), source.size))
        {
            entry.failed = true;
            snprintf(entry.error, sizeof(entry.error), "%s. The log has the details.", host.lastError());
            return false;
        }
        asITypeInfo* type = host.findClass(entry.module, "Screen");
        if (type == nullptr)
        {
            entry.failed = true;
            snprintf(entry.error, sizeof(entry.error), "%s needs a class that implements Screen", shown);
            return false;
        }
        entry.object = host.create(type);
        if (entry.object == nullptr)
        {
            fail(entry);
            return false;
        }
        entry.update = type->GetMethodByDecl("void update(float)");
        entry.draw = type->GetMethodByDecl("void draw(Canvas@)");
        // Its fields have asked for its assets by now: it enters when they are all here.
        entry.loading = true;
        arrive(entry);
        return !entry.failed;
    }

    const char* Screens::shown(const Entry& entry)
    {
        static char text[96];
        snprintf(text, sizeof(text), "%s/main.as", entry.info.id);
        return text;
    }

    bool Screens::include(FileData& source, const char* file, std::string& error, u32 depth)
    {
        char* text = reinterpret_cast<char*>(source.data);
        for (size_t at = 0; at < source.size;)
        {
            size_t end = at;
            while (end < source.size && text[end] != '\n')
            {
                end++;
            }
            size_t i = at;
            while (i < end && (text[i] == ' ' || text[i] == '\t'))
            {
                i++;
            }
            if (end - i > 10 && strncmp(text + i, "#include", 8) == 0)
            {
                const char* open = static_cast<const char*>(memchr(text + i, '"', end - i));
                const char* close = open ? static_cast<const char*>(memchr(open + 1, '"', static_cast<size_t>(text + end - open - 1))) : nullptr;
                const std::string name = close ? std::string(open + 1, close) : std::string();
                char path[192];
                FileData other;
                const bool known = !name.empty() && name.find("..") == std::string::npos && name.compare(0, 4, "http") != 0 && resolveAsset(*m_services, name.c_str(), path, sizeof(path));
                if (depth > 4 || !known || !m_services->platform->readFile(path, other, MemTag::Script))
                {
                    error = std::string(file) + ": cannot include \"" + name + "\"";
                    return false;
                }
                // The same file twice is once.
                bool had = false;
                for (const std::string& h : m_included)
                {
                    had = had || h == path;
                }
                if (!had)
                {
                    m_included.push_back(path);
                    if (!include(other, path, error, depth + 1))
                    {
                        return false;
                    }
                    m_services->host->addSection(path, reinterpret_cast<const char*>(other.data), other.size);
                }
                memset(text + at, ' ', end - at);
            }
            at = end + 1;
        }
        return true;
    }

    void Screens::arrive(Entry& entry)
    {
        if (entry.loading && m_services->assets->pending() == 0)
        {
            entry.loading = false;
            callOptional(entry, "void onEnter()");
        }
    }

    void Screens::setMenu(bool open)
    {
        if (open != m_menuOpen && m_depth)
        {
            if (open)
            {
                m_services->input->stopRumble();
            }
            m_services->audio->pauseGroup(m_stack[m_depth - 1].group, open);
        }
        m_menuOpen = open;
        if (!open) m_blueprintError = false;
    }

    void Screens::reloadChanged()
    {
#ifdef NX_DESKTOP_BLUEPRINT
        if (previewing()) return;
#endif
        if (m_depth == 0 || m_stack[m_depth - 1].loading)
        {
            return;
        }
        Entry& entry = m_stack[m_depth - 1];
        char path[192];
        snprintf(path, sizeof(path), "%s/%s/main.as", m_folder, entry.info.id);
        const u64 time = m_services->platform->fileTime(path);
        if ((time && time != entry.scriptTime) || (entry.world && entry.world->changed()) || (entry.ui && entry.ui->changed()))
        {
            NX_INFO("reloading %s", path);
            m_services->reloads++;
            restart();
            applyChanges();
        }
    }

    void Screens::unload(Entry& entry, bool callExit)
    {
        if (callExit && !entry.loading)
        {
            callOptional(entry, "void onExit()");
        }
        m_services->coroutines->stop(entry.group);
        m_services->platform->lockMouse(false);
        m_services->input->setWantsPointer(false);
        m_services->audio->stopGroup(entry.group, 0.15f);
        // Before its module goes: the behaviours are from there.
        destroy(entry.world);
        entry.world = nullptr;
        destroy(entry.ui);
        entry.ui = nullptr;
        ScriptHost& host = *m_services->host;
        host.release(entry.object);
        entry.object = nullptr;
        entry.update = entry.draw = nullptr;
        host.discard(entry.module);
        // Frees reference cycles too, and with them whatever assets they held.
        host.collectAll();
    }

    void Screens::enter(const Entry& entry)
    {
        const char* root=m_folder;
#ifdef NX_DESKTOP_BLUEPRINT
        if(previewing())root=m_previewRoot.c_str();
#endif
        snprintf(m_services->folder, sizeof(m_services->folder), "%s/%s", root, entry.info.id);
        m_services->world = entry.world;
        m_services->ui = entry.ui;
        m_services->audio->setGroup(entry.group);
        m_services->coroutines->setGroup(entry.group);
    }

    void Screens::callOptional(Entry& entry, const char* declaration)
    {
        if (entry.failed || entry.object == nullptr)
        {
            return;
        }
        if (asIScriptFunction* method = entry.object->GetObjectType()->GetMethodByDecl(declaration))
        {
            enter(entry);
            if (!m_services->host->callMethod(entry.object, method))
            {
                fail(entry);
            }
        }
    }

    void Screens::fail(Entry& entry)
    {
        entry.failed = true;
        snprintf(entry.error, sizeof(entry.error), "%s", m_services->host->lastError());
        m_services->input->consume();
        m_menuChoice = 0;
        m_services->coroutines->stop(entry.group);
        m_services->audio->stopGroup(entry.group, 0.1f);
    }

    // --- Running ---

    void Screens::update(float dt)
    {
        if (m_depth == 0)
        {
            return;
        }
        Entry& entry = m_stack[m_depth - 1];
        Input& input = *m_services->input;
        if (m_menuOpen || entry.failed)
        {
#ifdef NX_DESKTOP_BLUEPRINT
            if(previewing()&&entry.failed)return;
#endif
            updateMenu();
        }
        else if (input.pressed(Action::Menu))
        {
            setMenu(true);
            m_menuChoice = 0;
            input.consume();
        }
        else if (entry.loading)
        {
            entry.loadingFor += dt;
            arrive(entry);
        }
        else
        {
            enter(entry);
            // First, so that what it does with a press isn't done again by the screen.
            if (entry.ui && !entry.ui->update(dt))
            {
                fail(entry);
            }
            if (!entry.failed && entry.update && !m_services->host->callMethod(entry.object, entry.update, dt))
            {
                fail(entry);
            }
            entry.updated = true;
            if (!entry.failed && entry.world && !entry.world->update(dt))
            {
                fail(entry);
            }
            if (!entry.failed && !m_services->coroutines->update(entry.group, dt))
            {
                fail(entry);
            }
            Video::updateGroup(entry.group, dt);
        }
        applyChanges();
    }

    void Screens::draw()
    {
        if (m_depth == 0)
        {
            return;
        }
        Entry& entry = m_stack[m_depth - 1];
        Canvas& canvas = *m_services->canvas;
        if ((entry.loading || !entry.updated) && !entry.failed)
        {
            // Not until it has been updated once: before then a 3D screen's camera is wherever it began.
            drawLoading(entry);
        }
        else if (!entry.failed && entry.draw)
        {
            enter(entry);
            if (!m_services->host->callMethod(entry.object, entry.draw, static_cast<void*>(&canvas)))
            {
                fail(entry);
            }
        }
        if (entry.failed)
        {
            drawError(entry);
        }
        else if (m_menuOpen)
        {
            drawMenu();
        }
    }

    // --- The engine's own menu ---

    u32 Screens::menuItems(const char** items) const
    {
        u32 count = 0;
        const bool broken = m_stack[m_depth - 1].failed;
        if (!broken)
        {
            items[count++] = kResume;
        }
        items[count++] = broken ? kRetry : kRestart;
        if (m_depth > 1)
        {
            items[count++] = kHome;
        }
        if (!broken && m_depth == 1 && m_blueprintLauncher)
        {
            items[count++] = kBlueprint;
        }
        if (m_canQuit && m_depth == 1)
        {
            items[count++] = kQuit;
        }
        return count;
    }

    void Screens::updateMenu()
    {
        Input& input = *m_services->input;
        const char* items[4];
        const u32 count = menuItems(items);
        m_menuChoice = m_menuChoice < count ? m_menuChoice : 0;
        if (input.repeated(Action::Down))
        {
            m_menuChoice = (m_menuChoice + 1) % count;
        }
        if (input.repeated(Action::Up))
        {
            m_menuChoice = (m_menuChoice + count - 1) % count;
        }
        const bool broken = m_stack[m_depth - 1].failed;
        if (!broken && (input.pressed(Action::Menu) || input.pressed(Action::Cancel)))
        {
            setMenu(false);
            input.consume();
        }
        else if (input.pressed(Action::Accept))
        {
            const char* item = items[m_menuChoice];
            input.consume();
            if (item == kBlueprint)
            {
                m_blueprintError = !m_blueprintLauncher();
                if (!m_blueprintError) setMenu(false);
                return;
            }
            setMenu(false);
            if (item == kRestart || item == kRetry)
            {
                restart();
            }
            else if (item == kHome)
            {
                home();
            }
            else if (item == kQuit)
            {
                m_quit = true;
            }
        }
    }

    void Screens::drawMenu()
    {
        Canvas& c = *m_services->canvas;
        c.designHeight(720);
        const float middle = c.width() * 0.5f;
        c.rect(0, 0, c.width(), 720, { 0.02f, 0.03f, 0.06f, 0.75f });
        const char* items[4];
        const u32 count = menuItems(items);
        const float height = 110 + static_cast<float>(count) * 62;
        const float top = (720 - height) * 0.5f;
        c.roundRect(middle - 200, top, 400, height, 18, { 0.1f, 0.13f, 0.2f, 1 });
        c.text(*m_smallFont, m_stack[m_depth - 1].info.title, middle, top + 26, { 0.6f, 0.68f, 0.8f, 1 }, Align::Center);
        for (u32 i = 0; i < count; i++)
        {
            const float y = top + 76 + static_cast<float>(i) * 62;
            const bool chosen = i == m_menuChoice;
            if (chosen)
            {
                c.roundRect(middle - 176, y, 352, 52, 12, { 0.22f, 0.45f, 0.9f, 1 });
            }
            c.text(*m_font, items[i], middle, y + 11, chosen ? Color{ 1, 1, 1, 1 } : Color{ 0.75f, 0.8f, 0.88f, 1 }, Align::Center);
        }
        if (m_blueprintError)
        {
            c.text(*m_smallFont, "Studio could not open. Check the log and installation.", middle, top + height + 12, { 1, 0.65f, 0.65f, 1 }, Align::Center);
        }
    }

    void Screens::drawLoading(const Entry& entry)
    {
        // Most screens open at once: nothing is shown for those, so it doesn't flicker.
        if (entry.loadingFor < 0.2f)
        {
            return;
        }
        Canvas& c = *m_services->canvas;
        c.designHeight(720);
        const float middle = c.width() * 0.5f;
        c.text(*m_smallFont, entry.info.title, middle, 400, { 0.6f, 0.68f, 0.8f, 1 }, Align::Center);
        const float turn = entry.loadingFor * 240;
        c.arc(middle, 330, 36, 7, turn, turn + 270, { 0.27f, 0.55f, 0.96f, 1 });
    }

    void Screens::drawError(const Entry& entry)
    {
        Canvas& c = *m_services->canvas;
        c.designHeight(720);
        c.rect(0, 0, c.width(), 720, { 0.16f, 0.04f, 0.06f, 1 });
        c.text(*m_font, "This screen stopped", 80, 130, { 1, 1, 1, 1 });
        c.text(*m_smallFont, entry.info.title, 80, 180, { 1, 0.7f, 0.7f, 1 });
        c.paragraph(*m_smallFont, entry.error, 80, 240, c.width() - 160, { 1, 0.9f, 0.85f, 1 });
        const char* items[4];
        const u32 count = menuItems(items);
        for (u32 i = 0; i < count; i++)
        {
            const float y = 440 + static_cast<float>(i) * 62;
            if (i == m_menuChoice)
            {
                c.roundRect(70, y, 320, 52, 12, { 0.8f, 0.25f, 0.28f, 1 });
            }
            c.text(*m_font, items[i], 94, y + 11, { 1, 1, 1, 1 });
        }
    }
}
