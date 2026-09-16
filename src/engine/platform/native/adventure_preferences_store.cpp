#include "engine/platform/native/adventure_preferences_store.hpp"
#include <atomic>
#include <cerrno>
#include <system_error>
#if defined(__linux__)
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace voxy::platform {
using namespace game::adventure;
namespace {
#if defined(__linux__)
struct Descriptor {
    int value=-1;
    explicit Descriptor(int fd=-1):value(fd){}
    ~Descriptor(){if(value>=0)(void)::close(value);}
    Descriptor(const Descriptor&)=delete;
    Descriptor& operator=(const Descriptor&)=delete;
};
bool validPath(const std::filesystem::path& path) {
    const auto name=path.filename().string();
    return path.is_absolute()&&!name.empty()&&name!="."&&name!=".."&&name.size()<=96
        &&path.native().find('\0')==std::string::npos;
}
AdventurePreferencesStatus unavailable(std::string& message) {
    message="Settings could not be saved. Your current controls still work.";
    return AdventurePreferencesStatus::Unavailable;
}
AdventurePreferencesStatus unreadable(std::string& message) {
    message="Saved settings could not be read. Your current controls were kept.";
    return AdventurePreferencesStatus::Unavailable;
}
#endif
}
AdventurePreferencesStatus loadNativeAdventurePreferences(const std::filesystem::path& path,
    AdventurePreferences& output,std::string& message) {
#if defined(__linux__)
    message="Saved settings are invalid. Your current controls were kept.";
    try {
        if(!validPath(path))return AdventurePreferencesStatus::Invalid;
        Descriptor directory(::open(path.parent_path().c_str(),O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW));
        if(directory.value<0) {
            if(errno==ENOENT){message="No saved settings. Your current controls were kept.";return AdventurePreferencesStatus::Missing;}
            return unreadable(message);
        }
        Descriptor fd(::openat(directory.value,path.filename().c_str(),O_RDONLY|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK));
        if(fd.value<0) {
            if(errno==ENOENT){message="No saved settings. Your current controls were kept.";return AdventurePreferencesStatus::Missing;}
            return unreadable(message);
        }
        struct stat before{},after{};
        if(::fstat(fd.value,&before)!=0||!S_ISREG(before.st_mode)||before.st_size<=0
            ||static_cast<uint64_t>(before.st_size)>kMaximumAdventurePreferencesBytes)return AdventurePreferencesStatus::Invalid;
        std::string text(static_cast<size_t>(before.st_size),'\0');size_t offset=0;
        while(offset<text.size()) {
            const auto count=::read(fd.value,text.data()+offset,text.size()-offset);
            if(count<0&&errno==EINTR)continue;
            if(count<=0)return AdventurePreferencesStatus::Invalid;
            offset+=static_cast<size_t>(count);
        }
        char extra=0;
        ssize_t trailing=0;
        do {trailing=::read(fd.value,&extra,1);}while(trailing<0&&errno==EINTR);
        if(trailing!=0||::fstat(fd.value,&after)!=0||before.st_size!=after.st_size
            ||before.st_mtim.tv_sec!=after.st_mtim.tv_sec||before.st_mtim.tv_nsec!=after.st_mtim.tv_nsec
            ||before.st_ctim.tv_sec!=after.st_ctim.tv_sec||before.st_ctim.tv_nsec!=after.st_ctim.tv_nsec)
            return AdventurePreferencesStatus::Invalid;
        AdventurePreferences candidate;
        if(!parseAdventurePreferences(text,candidate,message))return AdventurePreferencesStatus::Invalid;
        output=std::move(candidate);message="Settings loaded.";return AdventurePreferencesStatus::Loaded;
    }catch(const std::exception&){return unreadable(message);}
#else
    (void)path;(void)output;message="Settings persistence is unavailable on this platform.";
    return AdventurePreferencesStatus::Unsupported;
#endif
}
AdventurePreferencesStatus saveNativeAdventurePreferences(const std::filesystem::path& path,
    const AdventurePreferences& preferences,std::string& message) {
#if defined(__linux__)
    try {
        message="The settings path is invalid. Your current controls still work.";
        std::string text;
        if(!validPath(path)||!encodeAdventurePreferences(preferences,text,message))return AdventurePreferencesStatus::Invalid;
        if(text.empty()||text.size()>kMaximumAdventurePreferencesBytes) {
            message="The settings file is too large. Your current controls still work.";return AdventurePreferencesStatus::Invalid;
        }
        std::error_code error;std::filesystem::create_directories(path.parent_path(),error);
        if(error)return unavailable(message);
        Descriptor directory(::open(path.parent_path().c_str(),O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW));
        if(directory.value<0)return unavailable(message);
        const auto name=path.filename().string();struct stat old{};
        if(::fstatat(directory.value,name.c_str(),&old,AT_SYMLINK_NOFOLLOW)==0) {
            if(!S_ISREG(old.st_mode))return unavailable(message);
        }else if(errno!=ENOENT)return unavailable(message);
        static std::atomic<uint64_t> serial{0};
        const auto temporary=".adventure-prefs-"+std::to_string(::getpid())+"-"+std::to_string(++serial)+".tmp";
        Descriptor fd(::openat(directory.value,temporary.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC|O_NOFOLLOW,0600));
        if(fd.value<0)return unavailable(message);
        const auto removeTemporary=[&]{(void)::unlinkat(directory.value,temporary.c_str(),0);};
        size_t offset=0;
        while(offset<text.size()) {
            const auto count=::write(fd.value,text.data()+offset,text.size()-offset);
            if(count<0&&errno==EINTR)continue;
            if(count<=0){removeTemporary();return unavailable(message);}
            offset+=static_cast<size_t>(count);
        }
        if(::fsync(fd.value)!=0){removeTemporary();return unavailable(message);}
        const int raw=fd.value;fd.value=-1;
        if(::close(raw)!=0){removeTemporary();return unavailable(message);}
        if(::renameat(directory.value,temporary.c_str(),directory.value,name.c_str())!=0){removeTemporary();return unavailable(message);}
        if(::fsync(directory.value)!=0) {
            message="Settings applied; disk confirmation failed. Try saving settings again.";
            return AdventurePreferencesStatus::Uncertain;
        }
        message="Settings saved.";return AdventurePreferencesStatus::Saved;
    }catch(const std::exception&){return unavailable(message);}
#else
    (void)path;(void)preferences;message="Settings persistence is unavailable on this platform.";
    return AdventurePreferencesStatus::Unsupported;
#endif
}
} // namespace voxy::platform
