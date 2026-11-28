#include "platform/posix/platform_posix.h"
#include "base/log.h"

#include <dirent.h>
#include <fcntl.h>
#include <sys/file.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <spawn.h>
#include <sys/wait.h>

extern char** environ;

namespace nx
{
    namespace
    {
        double now()
        {
            timespec t = {};
            clock_gettime(CLOCK_MONOTONIC, &t);
            return static_cast<double>(t.tv_sec) + static_cast<double>(t.tv_nsec) / 1e9;
        }

        bool readWhole(const char* path, FileData& out, MemTag tag)
        {
            FILE* file = fopen(path, "rb");
            if (file == nullptr)
            {
                return false;
            }
            struct stat info = {};
            u8* data = nullptr;
            size_t size = 0;
            // A folder opens too, but isn't a file.
            if (fstat(fileno(file), &info) == 0 && S_ISREG(info.st_mode) && info.st_size < 0x7FFFFFFF)
            {
                size = static_cast<size_t>(info.st_size);
                data = static_cast<u8*>(memAlloc(size + 1, tag));
            }
            const bool ok = data && fread(data, 1, size, file) == size;
            fclose(file);
            if (!ok)
            {
                memFree(data);
                return false;
            }
            out.take(data, size);
            return true;
        }

        // Each folder of a path that isn't there yet.
        void makeFolders(const char* path)
        {
            char part[1024];
            const size_t length = strlen(path);
            for (size_t i = 1; i <= length && length < sizeof(part); i++)
            {
                if (path[i] == '/' || path[i] == 0)
                {
                    memcpy(part, path, i);
                    part[i] = 0;
                    mkdir(part, 0755);
                }
            }
        }
    }

    // Written beside the old one and then swapped in, so a crash never leaves half a file.
    static bool swapIn(const char* full, const char* fresh, const char* name, const void* data, size_t size)
    {
        FILE* file = fopen(fresh, "wb");
        if (file == nullptr)
        {
            NX_ERROR("cannot write %s (%s)", name, strerror(errno));
            return false;
        }
        const bool ok = fwrite(data, 1, size, file) == size;
        return fclose(file) == 0 && ok && rename(fresh, full) == 0;
    }

    PlatformPosix::~PlatformPosix(){releaseBlueprintLease();pollBlueprintProcess();}
    bool PlatformPosix::blueprintOwnPath(const char* path,std::string& out)
    {
        char full[kMaxPath];
        if(strncmp(path,"own/",4)!=0||strstr(path,"..")||!mediaPath(path,full))return false;
        out=full;return true;
    }
    bool PlatformPosix::startBlueprintProcess(const std::string& program,const std::vector<std::string>& args,const std::string& secret)
    {
        if(m_blueprintChild>=0||program.empty()||secret.size()>256)return false;
        int pipeEnds[2];if(pipe(pipeEnds)!=0)return false;
        const std::string data=secret+"\n";
        if(write(pipeEnds[1],data.data(),data.size())!=static_cast<ssize_t>(data.size()))
        {close(pipeEnds[0]);close(pipeEnds[1]);return false;}
        close(pipeEnds[1]);
        std::vector<std::string> words={program};words.insert(words.end(),args.begin(),args.end());
        std::vector<char*> argv;for(auto& word:words)argv.push_back(word.data());argv.push_back(nullptr);
        posix_spawn_file_actions_t actions;
        if(posix_spawn_file_actions_init(&actions)!=0){close(pipeEnds[0]);return false;}
        const bool configured=posix_spawn_file_actions_adddup2(&actions,pipeEnds[0],STDIN_FILENO)==0&&
            (pipeEnds[0]==STDIN_FILENO||posix_spawn_file_actions_addclose(&actions,pipeEnds[0])==0);
        pid_t child=0;
        const bool started=configured&&posix_spawnp(&child,argv[0],&actions,nullptr,argv.data(),environ)==0;
        posix_spawn_file_actions_destroy(&actions);close(pipeEnds[0]);
        if(started)m_blueprintChild=static_cast<int>(child);
        return started;
    }
    int PlatformPosix::pollBlueprintProcess()
    {
        if(m_blueprintChild<0)return -2;
        int status=0;const auto result=waitpid(m_blueprintChild,&status,WNOHANG);
        if(result==0||(result<0&&errno==EINTR))return -1;
        m_blueprintChild=-1;
        return result>0&&WIFEXITED(status)?WEXITSTATUS(status):1;
    }

    bool PlatformPosix::acquireBlueprintLease()
    {
        if(m_blueprintLease>=0)return false;
        char folder[kMaxPath],full[kMaxPath];
        if(!mediaPath("own",folder)||!mediaPath("own/.blueprint-studio.lock",full))return false;
        makeFolders(folder);
        const int fd=open(full,O_CREAT|O_RDWR|O_CLOEXEC|O_NOFOLLOW,0600);
        if(fd<0)return false;
        if(flock(fd,LOCK_EX|LOCK_NB)!=0){close(fd);return false;}
        m_blueprintLease=fd;return true;
    }
    void PlatformPosix::releaseBlueprintLease()
    {
        // Never unlink: another process could acquire the same inode between close and unlink.
        if(m_blueprintLease>=0){close(m_blueprintLease);m_blueprintLease=-1;}
    }

    bool PlatformPosix::setFolders(const char* media, const char* saves)
    {
        m_start = now();
        const int a = snprintf(m_media, sizeof(m_media), "%s", media);
        const int b = snprintf(m_saves, sizeof(m_saves), "%s", saves);
        return a > 0 && b > 0 && static_cast<size_t>(a) < sizeof(m_media) && static_cast<size_t>(b) < sizeof(m_saves);
    }

    double PlatformPosix::seconds()
    {
        return now() - m_start;
    }

    void PlatformPosix::sleep(double seconds)
    {
        timespec t = {};
        t.tv_sec = static_cast<time_t>(seconds);
        t.tv_nsec = static_cast<long>((seconds - static_cast<double>(t.tv_sec)) * 1e9);
        nanosleep(&t, nullptr);
    }

    bool PlatformPosix::mediaPath(const char* path, char (&full)[kMaxPath]) const
    {
        const bool own = strncmp(path, "own", 3) == 0 && (path[3] == '/' || path[3] == 0);
        const int length = snprintf(full, sizeof(full), "%s/%s", own ? m_saves : m_media, path);
        return length > 0 && static_cast<size_t>(length) < sizeof(full);
    }

    bool PlatformPosix::savePath(const char* name, const char* suffix, char (&full)[kMaxPath]) const
    {
        const int length = snprintf(full, sizeof(full), "%s/%s.sav%s", m_saves, name, suffix);
        return length > 0 && static_cast<size_t>(length) < sizeof(full);
    }

    bool PlatformPosix::readFile(const char* path, FileData& out, MemTag tag)
    {
        char full[kMaxPath];
        if (!mediaPath(path, full) || !readWhole(full, out, tag))
        {
            NX_ERROR("cannot read %s (%s)", path, strerror(errno));
            return false;
        }
        return true;
    }

    u64 PlatformPosix::fileTime(const char* path)
    {
        char full[kMaxPath];
        struct stat info = {};
        if (!mediaPath(path, full) || stat(full, &info) != 0)
        {
            return 0;
        }
#if defined(__APPLE__)
        const timespec& time = info.st_mtimespec;
#else
        const timespec& time = info.st_mtim;
#endif
        return static_cast<u64>(time.tv_sec) * 1000000000u + static_cast<u64>(time.tv_nsec);
    }

    bool PlatformPosix::loadSave(const char* name, FileData& out)
    {
        char full[kMaxPath];
        return savePath(name, "", full) && readWhole(full, out, MemTag::General);
    }

    bool PlatformPosix::writeSave(const char* name, const void* data, size_t size)
    {
        char full[kMaxPath];
        char fresh[kMaxPath];
        if (!savePath(name, "", full) || !savePath(name, ".new", fresh))
        {
            return false;
        }
        makeFolders(m_saves);
        return swapIn(full, fresh, name, data, size);
    }

    void PlatformPosix::removeSave(const char* name)
    {
        char full[kMaxPath];
        if (savePath(name, "", full))
        {
            unlink(full);
        }
    }

    bool PlatformPosix::setSaveFolder(const char* path)
    {
        if(m_blueprintLease>=0)return false;
        const int length = snprintf(m_saves, sizeof(m_saves), "%s", path);
        return length > 0 && static_cast<size_t>(length) < sizeof(m_saves);
    }

    void PlatformPosix::keepOwn(const char* from)
    {
        // Under names that are the same on every machine.
        const char* slash = strrchr(from, '/');
        std::string name;
        for (const char* c = slash ? slash + 1 : from; *c; c++)
        {
            name += plainLetter(*c);
        }
        struct stat info = {};
        if (stat(from, &info) != 0 || !S_ISREG(info.st_mode) || info.st_size > 64 * 1024 * 1024 || name.empty() || name.size() > 60 || name[0] == '.')
        {
            NX_WARN("a file that was dropped is too large, or isn't one");
            return;
        }
        FileData data;
        char folder[kMaxPath], full[kMaxPath + 96], fresh[kMaxPath + 102];
        if (!mediaPath("own", folder) || !readWhole(from, data, MemTag::General))
        {
            return;
        }
        makeFolders(folder);
        if(m_blueprintLease>=0||protectedBlueprintDrop(name))
        {
            // Write to an exclusive temporary file, then publish with link()
            // (which fails rather than replacing an existing destination).
            // Keep the extension so Studio still recognizes the imported type.
            char temporary[kMaxPath+32];
            snprintf(temporary,sizeof(temporary),"%s/.bp-drop-XXXXXX",folder);
            const int fd=mkstemp(temporary);
            if(fd<0){NX_WARN("cannot stage dropped Blueprint file");return;}
            FILE* out=fdopen(fd,"wb");
            if(!out){close(fd);unlink(temporary);return;}
            const bool written=fwrite(data.data,1,data.size,out)==data.size;
            const bool closed=fclose(out)==0;
            if(written&&closed)
            {
                for(unsigned attempt=0;attempt<100;++attempt)
                {
                    const auto candidate=blueprintDropName(name,attempt);
                    snprintf(full,sizeof(full),"%s/%s",folder,candidate.c_str());
                    if(link(temporary,full)==0)
                    {m_dropped.push_back(candidate);unlink(temporary);return;}
                    if(errno!=EEXIST)break;
                    FileData existing;
                    if(readWhole(full,existing,MemTag::General)&&existing.size==data.size&&
                        (!data.size||memcmp(existing.data,data.data,data.size)==0))
                    {m_dropped.push_back(candidate);unlink(temporary);return;}
                }
            }
            unlink(temporary);
            NX_WARN("cannot keep dropped Blueprint file without replacing existing data");return;
        }
        snprintf(full, sizeof(full), "%s/%s", folder, name.c_str());
        snprintf(fresh, sizeof(fresh), "%s.new", full);
        if (swapIn(full, fresh, name.c_str(), data.data, data.size))
        {
            m_dropped.push_back(name);
        }
    }

    bool PlatformPosix::takeDropped(std::string& name)
    {
        if (m_dropped.empty())
        {
            return false;
        }
        name = m_dropped.front();
        m_dropped.erase(m_dropped.begin());
        return true;
    }

    bool PlatformPosix::createOwnFolder(const char* name)
    {
        if(!name||!*name||strlen(name)>166)return false;
        unsigned slash=0;bool empty=true;
        for(const char* c=name;*c;++c)
        {
            if(*c=='/'){if(empty||++slash>7)return false;empty=true;continue;}
            if(!((*c>='a'&&*c<='z')||(*c>='A'&&*c<='Z')||(*c>='0'&&*c<='9')||*c=='-'||*c=='_'))return false;
            empty=false;
        }
        if(empty)return false;
        char root[kMaxPath], full[kMaxPath], relative[176];
        if (!mediaPath("own",root)) return false;
        makeFolders(root);
        snprintf(relative,sizeof(relative),"own/%s",name);
        return mediaPath(relative,full)&&mkdir(full,0755)==0;
    }

    bool PlatformPosix::writeFile(const char* path, const void* data, size_t size)
    {
        char full[kMaxPath];
        char fresh[kMaxPath + 4];
        if (!mediaPath(path, full))
        {
            return false;
        }
        if (strncmp(path, "own/", 4) == 0)
        {
            char folder[kMaxPath];
            if (!mediaPath("own", folder)) return false;
            makeFolders(folder);
        }
        snprintf(fresh, sizeof(fresh), "%s.new", full);
        return swapIn(full, fresh, path, data, size);
    }

    void PlatformPosix::list(const char* path, bool folders, FolderCallback callback, void* user)
    {
        char full[kMaxPath];
        DIR* folder = mediaPath(path, full) ? opendir(full) : nullptr;
        if (folder == nullptr)
        {
            return;
        }
        while (const dirent* entry = readdir(folder))
        {
            char inside[kMaxPath + 256];
            struct stat info = {};
            snprintf(inside, sizeof(inside), "%s/%s", full, entry->d_name);
            if (entry->d_name[0] != '.' && stat(inside, &info) == 0 && (S_ISDIR(info.st_mode) != 0) == folders)
            {
                callback(entry->d_name, user);
            }
        }
        closedir(folder);
    }

    u64 PlatformPosix::totalMemory()
    {
        return static_cast<u64>(sysconf(_SC_PHYS_PAGES)) * static_cast<u64>(sysconf(_SC_PAGESIZE));
    }

    // From LC_ALL, LC_MESSAGES or LANG, as "en_GB.UTF-8": "en-GB".
    std::string PlatformPosix::locale()
    {
        const char* names[] = { "LC_ALL", "LC_MESSAGES", "LANG" };
        for (const char* name : names)
        {
            const char* value = getenv(name);
            if (value == nullptr || value[0] == 0)
            {
                continue;
            }
            std::string out;
            for (const char* c = value; *c && *c != '.' && *c != '@'; c++)
            {
                out += *c == '_' ? '-' : *c;
            }
            // C and POSIX, as "C.UTF-8", aren't a language: as if unset.
            if (out != "C" && out != "POSIX" && !out.empty())
            {
                return out;
            }
        }
        return std::string();
    }

    bool PlatformPosix::localTime(u32& hour, u32& minute, u32& second)
    {
        const time_t now = time(nullptr);
        struct tm t;
        if (localtime_r(&now, &t) == nullptr)
        {
            return false;
        }
        hour = static_cast<u32>(t.tm_hour);
        minute = static_cast<u32>(t.tm_min);
        second = static_cast<u32>(t.tm_sec);
        return true;
    }

    u32 PlatformPosix::processors()
    {
        const long count = sysconf(_SC_NPROCESSORS_ONLN);
        return count > 0 ? static_cast<u32>(count) : 1;
    }

    u64 PlatformPosix::freeMemory()
    {
#if defined(_SC_AVPHYS_PAGES)
        return static_cast<u64>(sysconf(_SC_AVPHYS_PAGES)) * static_cast<u64>(sysconf(_SC_PAGESIZE));
#else
        return 0;
#endif
    }
}
