#pragma once

#include "render/cove_hud.hpp"

namespace voxy::render {

enum class AdventureHudMode : uint8_t { Explore, Build, Catalog, Dialogue, Workbench, Chest, Journal, Pause, Bag, Guide };
enum class AdventurePaletteCategory : uint8_t { Structure, Bricks, Furniture };
struct AdventureHudRow {
    std::string label, detail;
    bool enabled=true;
    int action=10, value=0;
    uint32_t intent=0; // Opaque displayed intent; never recomputed from row position.
    uint8_t pieceKind=0;
    bool operator==(const AdventureHudRow&) const = default;
};
struct AdventureHudContent {
    AdventureHudMode mode=AdventureHudMode::Explore;
    std::string title, objective, context, status, selected, cost, compass;
    std::string menuText, menuStatus;
    std::vector<AdventureHudRow> rows, quickActions, buildControls;
    // Category actions are supplied by the runtime, preserving its input authority.
    std::array<AdventureHudRow,3> categories{};
    size_t selectedRow=0;
    uint8_t pieceKind=0;
    AdventurePaletteCategory paletteCategory=AdventurePaletteCategory::Structure;
    bool pickerOpen=false;
    float textScale=1;
    bool highContrast=false;
    CoveHudTone tone=CoveHudTone::Neutral;
    bool operator==(const AdventureHudContent&) const = default;
};
struct AdventureHudHit {
    glm::vec4 bounds{};
    int action=0, value=0;
    uint32_t intent=0;
    size_t row=SIZE_MAX;
    bool enabled=true;
};
struct AdventureHudTriangle {
    std::array<float,6> xy{}; // Pixel-space triangle; converted to NDC at upload.
    uint32_t rgba=0;
};
static_assert(sizeof(AdventureHudTriangle)==28);
struct AdventureHudLayout {
    static constexpr uint32_t maximumVisibleThumbnails=7,maximumTriangles=5684;
    CoveHudLayout canvas;
    std::vector<AdventureHudTriangle> triangles;
    std::vector<AdventureHudHit> hits;
    std::vector<glm::vec4> panels;
    uint32_t thumbnailCount=0;
    bool selectedVisible=false,trianglesTruncated=false;
    // The guide's actual emitted glyph range lets CPU checks verify complete
    // cards, including their tail beyond the font helper's per-call limit.
    glm::vec4 guideBodyBounds{};
    size_t guideBodyFirstQuad=0,guideBodyQuadCount=0,guideBodyLines=0;
    bool guideBodyComplete=false;
};
[[nodiscard]] AdventureHudLayout layoutAdventureHud(const AdventureHudContent&,uint32_t width,uint32_t height);

// The established font pass retains its own bound. Actual-mesh thumbnails use
// one separate bounded colored-triangle buffer, uploaded only on layout change.
class AdventureHudPath {
public:
    ~AdventureHudPath(){shutdown();}
    bool init(WGPUDevice,WGPUQueue,WGPUTextureFormat,const std::filesystem::path&);
    void shutdown() noexcept;
    bool needsContentUpdate() const noexcept {return std::chrono::steady_clock::now()-updated_>=std::chrono::milliseconds(100);}
    void setContent(AdventureHudContent);
    bool render(WGPUCommandEncoder,WGPUTextureView,uint32_t,uint32_t);
    void clearEncodedObservation() noexcept {backend_.clearEncodedObservation();lastEncodedTriangles_=0;}
    uint32_t lastEncodedQuads() const noexcept {return backend_.lastEncodedQuads();}
    uint64_t uploadCount() const noexcept {return backend_.uploadCount()+triangleUploadCount_;}
    uint32_t lastEncodedTriangles() const noexcept {return lastEncodedTriangles_;}
    uint64_t triangleUploadCount() const noexcept {return triangleUploadCount_;}
    bool initialized() const noexcept {return backend_.initialized()&&pipeline_;}
    const AdventureHudContent& content() const noexcept {return content_;}
    const AdventureHudLayout& layout() const noexcept {return layout_;}
    static constexpr uint32_t maximumTriangleCount=AdventureHudLayout::maximumTriangles;
    static constexpr uint32_t maximumVertexCount=maximumTriangleCount*3;
    static constexpr uint64_t maximumBufferBytes=uint64_t{maximumTriangleCount}*sizeof(AdventureHudTriangle);
    static constexpr uint64_t residentBytes=CoveHudPath::residentBytes+maximumBufferBytes;
private:
    CoveHudPath backend_;
    AdventureHudContent content_;
    AdventureHudLayout layout_;
    bool contentSet_=false;
    bool trianglesDirty_=true;
    WGPUQueue queue_=nullptr;
    WGPUBuffer triangles_=nullptr;
    WGPUShaderModule shader_=nullptr;
    WGPUPipelineLayout pipelineLayout_=nullptr;
    WGPURenderPipeline pipeline_=nullptr;
    uint32_t lastEncodedTriangles_=0;
    uint64_t triangleUploadCount_=0;
    std::chrono::steady_clock::time_point updated_{};
};
}
