#include "engine/platform/native/save_store.hpp"
#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <limits>
#include <new>
#include <utility>
#if defined(__linux__)
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace voxy::platform {
using namespace game::expedition;
namespace {
#if defined(__linux__)
bool fail(StoreIssue& issue,int code,bool uncertain=false) {
    auto error=StoreError::Io;
    if(code==ENOSPC||code==EDQUOT)error=StoreError::NoSpace;
    if(code==EACCES||code==EPERM||code==EROFS)error=StoreError::Permission;
    issue={error,code,uncertain};return false;
}
struct Descriptor {
    int value=-1;
    explicit Descriptor(int fd=-1):value(fd){}
    ~Descriptor(){if(value>=0)::close(value);}
    Descriptor(const Descriptor&)=delete;
    Descriptor& operator=(const Descriptor&)=delete;
    int release(){return std::exchange(value,-1);}
};
bool flush(int fd,StoreIssue& issue) {
    while(::fsync(fd)!=0){if(errno!=EINTR)return fail(issue,errno);}
    return true;
}
int directory(const std::filesystem::path& path,StoreIssue& issue) {
    Descriptor current(::open("/",O_RDONLY|O_DIRECTORY|O_CLOEXEC));
    if(current.value<0){fail(issue,errno);return -1;}
    for(const auto& component:path.relative_path()) {
        const auto name=component.string();if(name.empty()||name==".")continue;
        if(name==".."){issue.error=StoreError::InvalidPath;return -1;}
        if(::mkdirat(current.value,name.c_str(),0700)!=0&&errno!=EEXIST){fail(issue,errno);return -1;}
        Descriptor child(::openat(current.value,name.c_str(),O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW));
        if(child.value<0){fail(issue,errno);return -1;}
        // Also covers directories left by a previous interrupted creator.
        if(!flush(current.value,issue))return -1;
        ::close(current.value);current.value=child.release();
    }
    return current.release();
}
enum class ReadState { Missing,Valid,Damaged,Fatal };
ReadState readCopy(int directoryFd,const char* name,game::construction::WorldNamespace world,StoredGeneration& output,StoreIssue& issue) {
    Descriptor file(::openat(directoryFd,name,O_RDONLY|O_NONBLOCK|O_CLOEXEC|O_NOFOLLOW));
    if(file.value<0) {
        if(errno==ENOENT)return ReadState::Missing;
        if(errno==ELOOP){issue.error=StoreError::InvalidData;return ReadState::Damaged;}
        fail(issue,errno);return ReadState::Fatal;
    }
    struct stat state{};
    if(::fstat(file.value,&state)!=0){fail(issue,errno);return ReadState::Fatal;}
    if(!S_ISREG(state.st_mode)||state.st_nlink!=1||state.st_size<=0
        ||static_cast<uint64_t>(state.st_size)>kMaximumStoredPayloadBytes+kStoredGenerationEnvelopeBytes){issue.error=StoreError::InvalidData;return ReadState::Damaged;}
    std::vector<std::byte> bytes(static_cast<size_t>(state.st_size));size_t offset=0;
    while(offset<bytes.size()) {
        const auto count=::read(file.value,bytes.data()+offset,bytes.size()-offset);
        if(count<0){if(errno==EINTR)continue;fail(issue,errno);return ReadState::Fatal;}
        if(count==0){issue.error=StoreError::InvalidData;return ReadState::Damaged;}
        offset+=static_cast<size_t>(count);
    }
    if(decodeStoredGeneration(bytes,world,output,issue))return ReadState::Valid;
    return issue.error==StoreError::InvalidData?ReadState::Damaged:ReadState::Fatal;
}
#endif
}
struct NativeSaveStore::Impl {
    game::construction::WorldNamespace world{};
    SaveIoObserver* observer=nullptr;
    bool poisoned=false;
#if defined(__linux__)
    Descriptor directoryFd,lock;
    Impl(int directoryValue,int lockValue):directoryFd(directoryValue),lock(lockValue){}
    bool before(SaveIo operation,StoreIssue& issue) {
        const auto code=observer?observer->before(operation):0;
        return code==0||fail(issue,code);
    }
    void after(SaveIo operation){if(observer)observer->after(operation);}
    bool writeCopy(const char* name,std::span<const std::byte> bytes,bool& renamed,StoreIssue& issue) {
        if(!before(SaveIo::OpenStage,issue))return false;
        // Check type/link count before truncation; never follow a stale symlink.
        Descriptor file(::openat(directoryFd.value,"next",O_WRONLY|O_CREAT|O_NONBLOCK|O_CLOEXEC|O_NOFOLLOW,0600));
        if(file.value<0)return fail(issue,errno);
        struct stat state{};
        if(::fstat(file.value,&state)!=0)return fail(issue,errno);
        if(!S_ISREG(state.st_mode)||state.st_nlink!=1){issue.error=StoreError::InvalidData;return false;}
        if(::ftruncate(file.value,0)!=0)return fail(issue,errno);
        after(SaveIo::OpenStage);
        size_t offset=0;
        while(offset<bytes.size()) {
            if(!before(SaveIo::WriteChunk,issue))return false;
            const auto count=::write(file.value,bytes.data()+offset,std::min(size_t{65536},bytes.size()-offset));
            if(count<0){if(errno==EINTR)continue;return fail(issue,errno);}
            if(count==0)return fail(issue,EIO);
            offset+=static_cast<size_t>(count);after(SaveIo::WriteChunk);
        }
        if(!before(SaveIo::FlushFile,issue)||!flush(file.value,issue))return false;
        after(SaveIo::FlushFile);
        if(!before(SaveIo::CloseFile,issue))return false;
        // Linux close releases the descriptor even on error; never retry it.
        if(::close(file.release())!=0)return fail(issue,errno);
        after(SaveIo::CloseFile);
        if(!before(SaveIo::Rename,issue))return false;
        renamed=true; // Conservatively uncertain if the filesystem reports failure.
        if(::renameat(directoryFd.value,"next",directoryFd.value,name)!=0)return fail(issue,errno);
        after(SaveIo::Rename);
        if(!before(SaveIo::FlushDirectory,issue)||!flush(directoryFd.value,issue))return false;
        after(SaveIo::FlushDirectory);return true;
    }
#endif
};
NativeSaveStore::NativeSaveStore(std::unique_ptr<Impl> impl):impl_(std::move(impl)){}
NativeSaveStore::~NativeSaveStore()=default;
std::filesystem::path NativeSaveStore::defaultRoot(StoreIssue& issue) {
    issue={};
#if defined(__linux__)
    const auto* xdg=std::getenv("XDG_DATA_HOME");
    if(xdg&&*xdg&&std::filesystem::path(xdg).is_absolute())return std::filesystem::path(xdg)/"voxys"/"saves";
    const auto* homePath=std::getenv("HOME");
    if(homePath&&*homePath&&std::filesystem::path(homePath).is_absolute())return std::filesystem::path(homePath)/".local"/"share"/"voxys"/"saves";
    issue.error=StoreError::InvalidPath;
#else
    issue.error=StoreError::UnsupportedPlatform;
#endif
    return {};
}
std::unique_ptr<NativeSaveStore> NativeSaveStore::open(const std::filesystem::path& path,
    game::construction::WorldNamespace world,StoreIssue& issue,SaveIoObserver* observer) {
    issue={};
    if(!path.is_absolute()||path==path.root_path()||!game::construction::isValid(world)) {issue.error=StoreError::InvalidPath;return {};}
    for(const auto& part:path)if(part==".."||part.native().find(typename std::filesystem::path::value_type{})!=std::filesystem::path::string_type::npos){issue.error=StoreError::InvalidPath;return {};}
#if defined(__linux__)
    try {
        Descriptor dir(directory(path,issue));if(dir.value<0)return {};
        Descriptor lock(::openat(dir.value,"writer.lock",O_RDWR|O_CREAT|O_CLOEXEC|O_NOFOLLOW,0600));
        if(lock.value<0){fail(issue,errno);return {};}
        struct stat state{};
        if(::fstat(lock.value,&state)!=0){fail(issue,errno);return {};}
        if(!S_ISREG(state.st_mode)||state.st_nlink!=1){issue.error=StoreError::InvalidData;return {};}
        if(::flock(lock.value,LOCK_EX|LOCK_NB)!=0){
            if(errno==EWOULDBLOCK)issue.error=StoreError::Busy;else fail(issue,errno);
            return {};
        }
        if(!flush(dir.value,issue))return {};
        auto impl=std::make_unique<Impl>(dir.value,lock.value);(void)dir.release();(void)lock.release();
        impl->world=world;impl->observer=observer;
        return std::unique_ptr<NativeSaveStore>(new NativeSaveStore(std::move(impl)));
    }catch(const std::bad_alloc&){issue.error=StoreError::Capacity;return {};}
#else
    (void)observer;issue.error=StoreError::UnsupportedPlatform;return {};
#endif
}
bool NativeSaveStore::load(StoredGeneration& output,StoreIssue& issue) {
    issue={};
#if defined(__linux__)
    try {
        StoredGeneration primary,mirror;StoreIssue primaryIssue,mirrorIssue;
        const auto a=readCopy(impl_->directoryFd.value,"current",impl_->world,primary,primaryIssue);
        const auto b=readCopy(impl_->directoryFd.value,"mirror",impl_->world,mirror,mirrorIssue);
        if(a==ReadState::Fatal||b==ReadState::Fatal){issue=a==ReadState::Fatal?primaryIssue:mirrorIssue;return false;}
        if(a==ReadState::Missing&&b==ReadState::Missing){output={};return true;}
        if(a!=ReadState::Valid&&b!=ReadState::Valid){issue.error=StoreError::InvalidData;return false;}
        if(a==ReadState::Valid&&b==ReadState::Valid&&primary.generation==mirror.generation&&primary.payload!=mirror.payload){issue.error=StoreError::Conflict;return false;}
        const bool both=a==ReadState::Valid&&b==ReadState::Valid;
        const bool repair=!both||primary.generation!=mirror.generation;
        auto& latest=(a!=ReadState::Valid||(b==ReadState::Valid&&mirror.generation>primary.generation))?mirror:primary;
        latest.needsRepair=repair;output=std::move(latest);return true;
    }catch(const std::bad_alloc&){issue.error=StoreError::Capacity;return false;}
#else
    (void)output;issue.error=StoreError::UnsupportedPlatform;return false;
#endif
}
bool NativeSaveStore::publish(uint64_t expected,std::span<const std::byte> payload,StoreIssue& issue) {
    issue={};
    if(impl_->poisoned){issue.error=StoreError::RecoveryRequired;issue.publicationMayHaveHappened=true;return false;}
#if defined(__linux__)
    try {
        StoredGeneration previous;if(!load(previous,issue))return false;
        if(expected!=previous.generation){issue.error=StoreError::Conflict;return false;}
        if(expected==std::numeric_limits<uint64_t>::max()){issue.error=StoreError::Capacity;return false;}
        std::vector<std::byte> bytes;if(!encodeStoredGeneration(impl_->world,expected+1,payload,bytes,issue))return false;
        bool renamed=false;
        if(!impl_->writeCopy("current",bytes,renamed,issue)||!impl_->writeCopy("mirror",bytes,renamed,issue)) {
            issue.publicationMayHaveHappened=renamed;impl_->poisoned=renamed;return false;
        }
        return true;
    }catch(const std::bad_alloc&){issue.error=StoreError::Capacity;return false;}
#else
    (void)expected;(void)payload;issue.error=StoreError::UnsupportedPlatform;return false;
#endif
}
} // namespace voxy::platform
