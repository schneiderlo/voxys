#pragma once

#include "gpu/webgpu_compat.hpp"
#include <glm/vec4.hpp>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <optional>
#include <vector>

namespace voxy::render {

enum class CoveHudTone { Neutral, Ready, Blocked, Waiting };
struct CoveHudMenuRow {
    std::string label;
    bool enabled=true;
    bool operator==(const CoveHudMenuRow&) const = default;
};
struct CoveHudMenu {
    std::string title,subtitle,status;
    std::vector<CoveHudMenuRow> rows;
    size_t selected=0;
    bool naming=false;
    std::string name;
    size_t key=0;
    bool keyboardFocus=true;
    bool operator==(const CoveHudMenu&) const = default;
};
inline constexpr std::string_view kCoveNameKeys="ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 -_.";
struct CoveHudMenuHit {
    glm::vec4 bounds{};
    int row=-1,key=-1;
};
struct CoveHudContent {
    std::string title, selected, economy, status;
    std::array<std::string,3> hints;
    CoveHudTone tone=CoveHudTone::Neutral;
    std::optional<CoveHudMenu> menu{};
    std::string objective{}; // Read-only formatter step; never gameplay state.
    bool rightAligned=false; // Play view; workshop keeps its reserved left column.
    bool operator==(const CoveHudContent&) const = default;
};
// Normalized camera rectangle, leaving the left HUD and bottom 3D palette free.
[[nodiscard]] glm::dvec4 coveHudWorkshopRectangle(uint32_t width,uint32_t height) noexcept;

struct CoveHudQuad {
    glm::vec4 bounds; // Pixel x,y,width,height, converted to NDC before upload.
    glm::vec4 uv;
    glm::vec4 color;
};
struct CoveHudLayout {
    static constexpr size_t maximumQuads=768;
    std::array<CoveHudQuad,maximumQuads> quads{};
    size_t count=0;
    glm::vec4 panel{};
    float bodyPixels=20;
    bool truncated=false;
    std::vector<CoveHudMenuHit> menuHits;
};
[[nodiscard]] CoveHudLayout layoutCoveHud(const CoveHudContent&,uint32_t width,uint32_t height);
[[nodiscard]] std::vector<uint8_t> decodeCoveHudAtlas();

// One fixed atlas and vertex buffer; no game state, input handling or world resources.
class CoveHudPath {
public:
    ~CoveHudPath(){shutdown();}
    CoveHudPath()=default;
    CoveHudPath(const CoveHudPath&)=delete;
    CoveHudPath& operator=(const CoveHudPath&)=delete;
    [[nodiscard]] bool init(WGPUDevice,WGPUQueue,WGPUTextureFormat,const std::filesystem::path& shader);
    void shutdown() noexcept;
    [[nodiscard]] bool needsContentUpdate() const noexcept;
    void setContent(CoveHudContent);
    [[nodiscard]] bool render(WGPUCommandEncoder,WGPUTextureView,uint32_t width,uint32_t height);
    void clearEncodedObservation() noexcept { lastEncodedQuads_=0; }
    [[nodiscard]] uint32_t lastEncodedQuads() const noexcept {return lastEncodedQuads_;}
    [[nodiscard]] const CoveHudLayout& layout() const noexcept {return layout_;}
    [[nodiscard]] const CoveHudContent& content() const noexcept {return content_;}
    [[nodiscard]] uint64_t uploadCount() const noexcept {return uploadCount_;}
    [[nodiscard]] bool initialized() const noexcept {return pipeline_!=nullptr;}
    static constexpr uint64_t residentBytes=512u*256u+CoveHudLayout::maximumQuads*sizeof(CoveHudQuad);
private:
    WGPUDevice device_=nullptr;
    WGPUQueue queue_=nullptr;
    WGPUTexture atlas_=nullptr;
    WGPUTextureView atlasView_=nullptr;
    WGPUSampler sampler_=nullptr;
    WGPUBuffer quads_=nullptr;
    WGPUShaderModule shader_=nullptr;
    WGPUBindGroupLayout bindingsLayout_=nullptr;
    WGPUBindGroup bindings_=nullptr;
    WGPUPipelineLayout pipelineLayout_=nullptr;
    WGPURenderPipeline pipeline_=nullptr;
    CoveHudContent content_;
    CoveHudLayout layout_;
    std::chrono::steady_clock::time_point contentUpdated_{};
    uint32_t width_=0,height_=0,lastEncodedQuads_=0;
    uint64_t uploadCount_=0;
    bool dirty_=true;
};
}
