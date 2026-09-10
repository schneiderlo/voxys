#include "physics/gpu/gpu_authored_shapes.hpp"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <new>

namespace voxy::physics {
namespace {
constexpr uint32_t kMaximumOperations=8, kMaximumSlots=512;
#if defined(VOXY_WASM)
constexpr uint32_t kScopeCount=3;
#else
// The pinned native implementation aborts on the declared Internal filter.
constexpr uint32_t kScopeCount=2;
#endif
constexpr uint32_t kCallbacksPerOperation=kScopeCount+1;

// No GPU objects live in this shared state. Each armed cell temporarily owns
// its parent so an in-flight callback cannot outlive its userdata. The callback
// clears that self-reference BEFORE its final release publication; a subsequent
// acquire/rearm can therefore reuse the same cell without allocating or racing.
struct Callbacks {
    struct Cell {
        std::shared_ptr<Callbacks> keepAlive{};
        std::array<char,256> message{};
        std::atomic<uint32_t> status{0}; // pending 0, success 1, failure 2
    };
    std::array<std::array<Cell,kCallbacksPerOperation>,kMaximumOperations> cells{};
};
void finishCallback(void* userdata,bool success,std::string_view message) noexcept {
    auto& cell=*static_cast<Callbacks::Cell*>(userdata);
    auto keepAlive=std::move(cell.keepAlive);
    const auto count=std::min(message.size(),cell.message.size()-1);
    if(count!=0) std::memcpy(cell.message.data(),message.data(),count);
    cell.message[count]='\0';
    cell.status.store(success ? 1u : 2u,std::memory_order_release);
}
#if defined(VOXY_WASM)
void retainDevice(WGPUDevice device) noexcept { wgpuDeviceAddRef(device); }
std::string_view messageView(WGPUStringView message) noexcept {
    return message.data ? std::string_view{message.data,message.length==WGPU_STRLEN ? std::strlen(message.data) : message.length} : std::string_view{};
}
void popScope(WGPUDevice device,Callbacks::Cell* cell) noexcept {
    WGPUPopErrorScopeCallbackInfo info=WGPU_POP_ERROR_SCOPE_CALLBACK_INFO_INIT;
    info.mode=WGPUCallbackMode_AllowSpontaneous; info.userdata1=cell;
    info.callback=[](WGPUPopErrorScopeStatus status,WGPUErrorType type,WGPUStringView message,void* data,void*) {
        finishCallback(data,status==WGPUPopErrorScopeStatus_Success && type==WGPUErrorType_NoError,messageView(message));
    };
    static_cast<void>(wgpuDevicePopErrorScope(device,info));
}
void queueFence(WGPUQueue queue,Callbacks::Cell* cell) noexcept {
    WGPUQueueWorkDoneCallbackInfo info=WGPU_QUEUE_WORK_DONE_CALLBACK_INFO_INIT;
    info.mode=WGPUCallbackMode_AllowSpontaneous; info.userdata1=cell;
    info.callback=[](WGPUQueueWorkDoneStatus status,WGPUStringView message,void* data,void*) {
        finishCallback(data,status==WGPUQueueWorkDoneStatus_Success,messageView(message));
    };
    static_cast<void>(wgpuQueueOnSubmittedWorkDone(queue,info));
}
#else
void retainDevice(WGPUDevice device) noexcept { wgpuDeviceReference(device); }
void popScope(WGPUDevice device,Callbacks::Cell* cell) noexcept {
    wgpuDevicePopErrorScope(device,[](WGPUErrorType type,const char* message,void* data) {
        finishCallback(data,type==WGPUErrorType_NoError,message ? message : "");
    },cell);
}
void queueFence(WGPUQueue queue,Callbacks::Cell* cell) noexcept {
    wgpuQueueOnSubmittedWorkDone(queue,[](WGPUQueueWorkDoneStatus status,void* data) {
        finishCallback(data,status==WGPUQueueWorkDoneStatus_Success,
            status==WGPUQueueWorkDoneStatus_Success ? "" : "GPU queue completion failed");
    },cell);
}
#endif

struct Range { uint32_t begin=0,count=0; };
class Ranges {
public:
    explicit Ranges(uint32_t capacity=0) { free_[0]={0,capacity}; }
    [[nodiscard]] std::optional<Range> take(uint32_t count) noexcept {
        if(count==0) return std::nullopt;
        for(size_t i=0;i<size_;++i) if(free_[i].count>=count) {
            const Range result{free_[i].begin,count}; free_[i].begin+=count; free_[i].count-=count;
            if(free_[i].count==0) erase(i);
            return result;
        }
        return std::nullopt;
    }
    // At most 512 allocated spans leave at most 513 free intervals. Inputs are
    // exact previously allocated spans; no public arbitrary-free API exists.
    void give(Range range) noexcept {
        size_t i=0; while(i<size_ && free_[i].begin<range.begin) ++i;
        if(i>0 && free_[i-1].begin+free_[i-1].count==range.begin) {
            free_[i-1].count+=range.count;
            if(i<size_ && free_[i-1].begin+free_[i-1].count==free_[i].begin) { free_[i-1].count+=free_[i].count; erase(i); }
        } else if(i<size_ && range.begin+range.count==free_[i].begin) {
            free_[i].begin=range.begin; free_[i].count+=range.count;
        } else {
            for(size_t j=size_;j>i;--j) free_[j]=free_[j-1];
            free_[i]=range; ++size_;
        }
    }
private:
    void erase(size_t i) noexcept { for(size_t j=i+1;j<size_;++j) free_[j-1]=free_[j]; --size_; }
    std::array<Range,kMaximumSlots+1> free_{};
    size_t size_=1;
};
GpuShapeError poolError(ShapePoolError error) noexcept {
    switch(error) {
    case ShapePoolError::None: return GpuShapeError::None;
    case ShapePoolError::Allocation: return GpuShapeError::Allocation;
    case ShapePoolError::Capacity: case ShapePoolError::GenerationExhausted: case ShapePoolError::ReferenceOverflow: return GpuShapeError::Capacity;
    case ShapePoolError::InvalidProfile: return GpuShapeError::InvalidLimits;
    case ShapePoolError::Retiring: return GpuShapeError::NotReady;
    default: return GpuShapeError::InvalidHandle;
    }
}
} // namespace

class GpuAuthoredShapeStore::Impl {
public:
    struct Record {
        ShapeHandle handle{};
        Range cells{},faces{},nodes{};
        GpuShapeState state=GpuShapeState::Missing;
        uint64_t uploadSerial=0;
    };
    struct Operation { uint64_t serial=0; bool sealed=false; };
    Impl(WGPUDevice device,AuthoredShapePool pool,GpuShapeStoreLimits limits)
        : device_(device),pool_(std::move(pool)),limits_(limits),
          callbacks_(std::make_shared<Callbacks>()),cells_(limits.cpu.cells),faces_(limits.cpu.faces),nodes_(limits.cpu.nodes) {
        retainDevice(device_);
        // Derive the queue from its device; accepting an unrelated queue could
        // put validation failures on a different device's error-scope stack.
        queue_=wgpuDeviceGetQueue(device_);
        header_.layout={kAuthoredShapeFormatVersion,limits.cpu.slots,2+7*(limits.cpu.slots+1),0};
        header_.layout[3]=header_.layout[2]+3*limits.cpu.cells;
        header_.capacities={header_.layout[3]+3*limits.cpu.faces,0,limits.cpu.cells,limits.cpu.faces};
        header_.capacities[1]=header_.capacities[0]+2*limits.cpu.nodes;
    }
    ~Impl() {
        // Normal users close/drain first. An abandoned owner still balances
        // open scopes and leaves independent callback userdata alive. Release
        // references rather than destroying a buffer held by encoded commands.
        if(unresolved_.valid()) { seal({}); unresolved_={}; }
        releaseGpu();
    }
    void releaseGpu() noexcept {
        if(buffer_) { wgpuBufferRelease(buffer_); buffer_=nullptr; }
        if(queue_) { wgpuQueueRelease(queue_); queue_=nullptr; }
        if(device_) { wgpuDeviceRelease(device_); device_=nullptr; }
    }
    [[nodiscard]] uint64_t bytes() const noexcept { return uint64_t{header_.capacities[1]}*16; }
    [[nodiscard]] GpuShapeError admission() const noexcept {
        if(phase_==GpuShapePhase::Failed) return GpuShapeError::GpuFailure;
        if(phase_==GpuShapePhase::Closing || phase_==GpuShapePhase::Closed) return GpuShapeError::Closed;
        if(phase_!=GpuShapePhase::Ready) return GpuShapeError::NotReady;
        if(unresolved_.valid() || operationCount_==kMaximumOperations) return GpuShapeError::Busy;
        if(pool_.stats().submitted==UINT64_MAX) return GpuShapeError::Capacity;
        return GpuShapeError::None;
    }
    [[nodiscard]] const Record* record(ShapeHandle handle) const noexcept {
        if(!handle.valid() || handle.index>limits_.cpu.slots || records_[handle.index].handle!=handle) return nullptr;
        return &records_[handle.index];
    }
    [[nodiscard]] Record* record(ShapeHandle handle) noexcept {
        if(!handle.valid() || handle.index>limits_.cpu.slots || records_[handle.index].handle!=handle) return nullptr;
        return &records_[handle.index];
    }
    void fail(std::string_view message) noexcept {
        if(phase_==GpuShapePhase::Failed) return;
        const auto n=std::min(message.size(),failure_.size()-1);
        if(n!=0) std::memcpy(failure_.data(),message.data(),n);
        failure_[n]='\0'; phase_=GpuShapePhase::Failed;
    }
    // Prevalidated bounded operation registration. Queue serials originate only
    // here, including initialization, uploads and descriptor invalidations.
    [[nodiscard]] bool start(std::span<const ShapeHandle> handles) noexcept {
        const auto serial=pool_.stats().submitted;
        if(operationCount_==kMaximumOperations || serial==UINT64_MAX || unresolved_.valid()) return false;
        if(pool_.submitted(serial+1,handles)!=ShapePoolError::None) return false;
        const uint32_t index=(operationHead_+operationCount_)%kMaximumOperations;
        operations_[index]={serial+1,false}; ++operationCount_;
        for(auto& cell:callbacks_->cells[index]) { cell.status.store(0,std::memory_order_relaxed); cell.message[0]='\0'; }
        wgpuDevicePushErrorScope(device_,WGPUErrorFilter_Validation);
        wgpuDevicePushErrorScope(device_,WGPUErrorFilter_OutOfMemory);
#if defined(VOXY_WASM)
        wgpuDevicePushErrorScope(device_,WGPUErrorFilter_Internal);
#endif
        openOperation_=index;
        return true;
    }
    void seal(std::span<const WGPUCommandBuffer> commands) noexcept {
        wgpuQueueSubmit(queue_,commands.size(),commands.data());
        auto& cells=callbacks_->cells[openOperation_];
        for(uint32_t i=0;i<kScopeCount;++i) { cells[i].keepAlive=callbacks_; popScope(device_,&cells[i]); }
        cells[kScopeCount].keepAlive=callbacks_; queueFence(queue_,&cells[kScopeCount]);
        operations_[openOperation_].sealed=true;
    }
    void write(uint64_t offset,const void* data,size_t size) noexcept {
        wgpuQueueWriteBuffer(queue_,buffer_,offset,data,size); uploadBytes_+=size;
    }
    [[nodiscard]] bool initialize() noexcept {
        WGPULimits deviceLimits{};
        if(!queue_ || !gpu::getDeviceLimits(device_,deviceLimits) || bytes()>limits_.gpuBytes
            || bytes()>deviceLimits.maxBufferSize || bytes()>deviceLimits.maxStorageBufferBindingSize) return false;
        if(!start({})) return false;
        WGPUBufferDescriptor desc{}; WGPU_SET_LABEL(desc,"authored_shape_heap");
        desc.size=bytes(); desc.usage=WGPUBufferUsage_Storage|WGPUBufferUsage_CopyDst|WGPUBufferUsage_CopySrc;
        buffer_=wgpuDeviceCreateBuffer(device_,&desc);
        if(buffer_) write(0,&header_,sizeof(header_));
        seal({});
        if(!buffer_) fail("Authored shape buffer allocation failed");
        return buffer_!=nullptr;
    }
    void cleanup() noexcept {
        if(phase_==GpuShapePhase::Failed || phase_==GpuShapePhase::Closed || unresolved_.valid()
            || operationCount_==kMaximumOperations) return;
        bool any=false;
        for(uint32_t i=1;i<=limits_.cpu.slots;++i) if(records_[i].handle.valid() && !pool_.get(records_[i].handle)) { any=true; break; }
        if(!any) return;
        if(!start({})) { fail("Authored shape invalidation serial exhausted"); return; }
        constexpr std::array<uint32_t,4> zero{};
        for(uint32_t i=1;i<=limits_.cpu.slots;++i) {
            auto& r=records_[i];
            if(!r.handle.valid() || pool_.get(r.handle)) continue;
            // Old readers have completed. Invalidate the old generation before
            // any later queue use; a new upload may safely follow this write.
            write(sizeof(GpuShapeHeapHeader)+uint64_t{i}*sizeof(GpuShapeDescriptor),zero.data(),sizeof(zero));
            cells_.give(r.cells); faces_.give(r.faces); nodes_.give(r.nodes); r={}; --rangeCount_;
        }
        seal({});
    }
    [[nodiscard]] ShapeHandle upload(AuthoredShape&& shape,GpuShapeError& error) noexcept {
        cleanup(); error=admission(); if(error!=GpuShapeError::None) return {};
        const auto cost=shape.cost();
        if(cost.cells==0 || cost.faces==0 || cost.nodes==0) { error=GpuShapeError::InvalidHandle; return {}; }
        auto nextCells=cells_,nextFaces=faces_,nextNodes=nodes_;
        const auto c=nextCells.take(cost.cells),f=nextFaces.take(cost.faces),n=nextNodes.take(cost.nodes);
        if(!c || !f || !n) { error=GpuShapeError::Capacity; return {}; }
        ShapePoolError cpuError;
        const auto handle=pool_.insert(std::move(shape),cpuError);
        if(!handle.valid()) { error=poolError(cpuError); return {}; }
        cells_=nextCells; faces_=nextFaces; nodes_=nextNodes;
        auto& r=records_[handle.index]; r={handle,*c,*f,*n,GpuShapeState::Uploading,0}; ++rangeCount_;
        const std::array handles{handle};
        if(!start(handles)) { fail("Authored shape upload reservation invariant failed"); error=GpuShapeError::GpuFailure; return handle; }
        r.uploadSerial=pool_.stats().submitted;
        const auto& value=*pool_.get(handle);
        write(uint64_t{header_.layout[2]}*16+uint64_t{c->begin}*sizeof(PackedShapeCell),value.cells().data(),value.cells().size_bytes());
        write(uint64_t{header_.layout[3]}*16+uint64_t{f->begin}*sizeof(PackedShapeFace),value.faces().data(),value.faces().size_bytes());
        write(uint64_t{header_.capacities[0]}*16+uint64_t{n->begin}*sizeof(PackedShapeNode),value.nodes().data(),value.nodes().size_bytes());
        const auto bounds=value.rootBounds();
        const GpuShapeDescriptor descriptor{{handle.generation,kAuthoredShapeFormatVersion,c->begin,c->count},
            {f->begin,f->count,n->begin,n->count},value.packedMass(),
            {bounds.minimum.x,bounds.minimum.y,bounds.minimum.z,0},{bounds.maximum.x,bounds.maximum.y,bounds.maximum.z,0}};
        write(sizeof(GpuShapeHeapHeader)+uint64_t{handle.index}*sizeof(GpuShapeDescriptor),&descriptor,sizeof(descriptor));
        seal({}); error=GpuShapeError::None; return handle;
    }
    void poll() noexcept {
        if(phase_==GpuShapePhase::Closed) return;
        while(operationCount_!=0) {
            const auto& operation=operations_[operationHead_]; if(!operation.sealed) break;
            bool pending=false;
            for(const auto& cell:callbacks_->cells[operationHead_]) {
                const auto status=cell.status.load(std::memory_order_acquire);
                if(status==0) pending=true;
                else if(status==2) fail(cell.message[0]!='\0' ? std::string_view{cell.message.data()} : "GPU shape operation failed");
            }
            if(pending) break;
            if(phase_!=GpuShapePhase::Failed) {
                if(pool_.completed(operation.serial)!=ShapePoolError::None) { fail("Authored shape completion frontier invariant failed"); break; }
                if(phase_==GpuShapePhase::Initializing) phase_=GpuShapePhase::Ready;
                for(auto& r:records_) if(r.state==GpuShapeState::Uploading && r.uploadSerial<=operation.serial) r.state=GpuShapeState::Ready;
            }
            operations_[operationHead_]={}; operationHead_=(operationHead_+1)%kMaximumOperations; --operationCount_;
        }
        cleanup();
        if(phase_==GpuShapePhase::Closing && operationCount_==0 && rangeCount_==0 && !unresolved_.valid()) {
            releaseGpu(); phase_=GpuShapePhase::Closed;
        }
    }
    WGPUDevice device_=nullptr;
    WGPUQueue queue_=nullptr;
    WGPUBuffer buffer_=nullptr;
    AuthoredShapePool pool_;
    GpuShapeStoreLimits limits_{};
    std::shared_ptr<Callbacks> callbacks_;
    GpuShapeHeapHeader header_{};
    Ranges cells_,faces_,nodes_;
    std::array<Record,kMaximumSlots+1> records_{};
    std::array<Operation,kMaximumOperations> operations_{};
    uint32_t operationHead_=0,operationCount_=0,openOperation_=0,rangeCount_=0;
    uint64_t uploadBytes_=0;
    GpuShapeSubmission unresolved_{};
    GpuShapePhase phase_=GpuShapePhase::Initializing;
    std::array<char,256> failure_{};
};

GpuAuthoredShapeStore::GpuAuthoredShapeStore(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
GpuAuthoredShapeStore::~GpuAuthoredShapeStore()=default;
std::unique_ptr<GpuAuthoredShapeStore> GpuAuthoredShapeStore::create(WGPUDevice device,
    uint64_t identity,GpuShapeError& error,GpuShapeStoreLimits limits) {
    if(!device) { error=GpuShapeError::InvalidContext; return {}; }
    if(limits.gpuBytes==0 || limits.gpuBytes>32*1024*1024) { error=GpuShapeError::InvalidLimits; return {}; }
    ShapePoolError cpuError; auto pool=AuthoredShapePool::create(identity,cpuError,limits.cpu);
    if(!pool) { error=poolError(cpuError); return {}; }
    try {
        auto result=std::unique_ptr<GpuAuthoredShapeStore>{new GpuAuthoredShapeStore(std::make_unique<Impl>(device,std::move(*pool),limits))};
        if(!result->impl_->initialize()) {
            error=result->impl_->phase_==GpuShapePhase::Failed ? GpuShapeError::GpuFailure : GpuShapeError::InvalidLimits;
            return {};
        }
        error=GpuShapeError::None; return result;
    } catch(const std::bad_alloc&) { error=GpuShapeError::Allocation; return {}; }
}
ShapeHandle GpuAuthoredShapeStore::upload(AuthoredShape&& value,GpuShapeError& error) noexcept { return impl_->upload(std::move(value),error); }
GpuShapeState GpuAuthoredShapeStore::state(ShapeHandle handle) const noexcept {
    if(impl_->phase_==GpuShapePhase::Failed || impl_->phase_==GpuShapePhase::Closed) return GpuShapeState::Missing;
    const auto* record=impl_->record(handle); return record && impl_->pool_.get(handle) ? record->state : GpuShapeState::Missing;
}
const AuthoredShape* GpuAuthoredShapeStore::get(ShapeHandle handle) const noexcept { return impl_->pool_.get(handle); }
GpuShapeError GpuAuthoredShapeStore::retain(ShapeHandle handle) noexcept {
    if(impl_->phase_==GpuShapePhase::Failed) return GpuShapeError::GpuFailure;
    if(impl_->phase_==GpuShapePhase::Closing || impl_->phase_==GpuShapePhase::Closed) return GpuShapeError::Closed;
    return poolError(impl_->pool_.retain(handle));
}
GpuShapeError GpuAuthoredShapeStore::release(ShapeHandle handle) noexcept {
    const auto result=poolError(impl_->pool_.release(handle)); impl_->cleanup(); return result;
}
GpuShapeError GpuAuthoredShapeStore::retire(ShapeHandle handle) noexcept {
    auto* record=impl_->record(handle); if(!record) return GpuShapeError::InvalidHandle;
    const auto result=poolError(impl_->pool_.retire(handle));
    if(result==GpuShapeError::None) record->state=GpuShapeState::Retiring;
    impl_->cleanup(); return result;
}
GpuShapeSubmission GpuAuthoredShapeStore::prepareSubmission(std::span<const ShapeHandle> handles,GpuShapeError& error) noexcept {
    impl_->cleanup(); error=impl_->admission(); if(error!=GpuShapeError::None) return {};
    if(handles.size()>kMaximumSlots) { error=GpuShapeError::Capacity; return {}; }
    for(auto handle:handles) if(state(handle)!=GpuShapeState::Ready) { error=GpuShapeError::InvalidHandle; return {}; }
    if(!impl_->start(handles)) { error=GpuShapeError::Capacity; return {}; }
    impl_->unresolved_={impl_->pool_.identity(),impl_->pool_.stats().submitted}; return impl_->unresolved_;
}
GpuShapeError GpuAuthoredShapeStore::submit(GpuShapeSubmission ticket,std::span<const WGPUCommandBuffer> commands) noexcept {
    if(!ticket.valid() || ticket!=impl_->unresolved_) return GpuShapeError::InvalidTicket;
    if(impl_->phase_==GpuShapePhase::Failed) return GpuShapeError::GpuFailure;
    if(commands.empty() || commands.size()>16 || std::ranges::any_of(commands,[](auto command){return command==nullptr;})) return GpuShapeError::InvalidCommands;
    impl_->seal(commands); impl_->unresolved_={}; return GpuShapeError::None;
}
GpuShapeError GpuAuthoredShapeStore::discard(GpuShapeSubmission ticket) noexcept {
    if(!ticket.valid() || ticket!=impl_->unresolved_) return GpuShapeError::InvalidTicket;
    impl_->seal({}); impl_->unresolved_={}; return GpuShapeError::None;
}
void GpuAuthoredShapeStore::poll() noexcept { impl_->poll(); }
void GpuAuthoredShapeStore::close() noexcept {
    if(impl_->phase_==GpuShapePhase::Closed || impl_->phase_==GpuShapePhase::Failed) return;
    impl_->phase_=GpuShapePhase::Closing;
    for(auto& r:impl_->records_) if(r.handle.valid() && impl_->pool_.get(r.handle)) {
        static_cast<void>(impl_->pool_.retire(r.handle)); r.state=GpuShapeState::Retiring;
    }
    impl_->cleanup(); impl_->poll();
}
GpuShapeStoreStats GpuAuthoredShapeStore::stats() const noexcept {
    uint32_t pending=0;
    for(uint32_t i=0;i<impl_->operationCount_;++i) {
        const auto index=(impl_->operationHead_+i)%kMaximumOperations;
        if(!impl_->operations_[index].sealed) continue;
        for(const auto& cell:impl_->callbacks_->cells[index]) if(cell.status.load(std::memory_order_acquire)==0) ++pending;
    }
    return {impl_->phase_,impl_->pool_.stats(),impl_->buffer_ ? impl_->bytes() : 0,impl_->uploadBytes_,impl_->operationCount_,pending,
        impl_->rangeCount_,impl_->unresolved_.valid()};
}
std::string_view GpuAuthoredShapeStore::failure() const noexcept { return impl_->failure_.data(); }
WGPUBuffer GpuAuthoredShapeStore::buffer() const noexcept { return impl_->buffer_; }
const GpuShapeHeapHeader& GpuAuthoredShapeStore::layout() const noexcept { return impl_->header_; }
} // namespace voxy::physics
