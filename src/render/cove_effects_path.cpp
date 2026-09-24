#include "gpu/pipeline.hpp"
#include "render/cove_effects_path.hpp"
#include "gpu/resources.hpp"
#include <algorithm>
#include <cmath>

namespace voxy::render {
namespace {
bool finite(glm::vec4 value) noexcept {
    for(int i=0;i<4;++i)if(!std::isfinite(value[i]))return false;
    return true;
}
bool valid(const CoveEffectsPath::Instance& value) noexcept {
    return finite(value.positionKind)&&finite(value.velocityAge)&&finite(value.sizeLife)&&finite(value.color)
        &&value.positionKind.w>=0&&value.positionKind.w<=4&&std::floor(value.positionKind.w)==value.positionKind.w
        &&value.velocityAge.w>=0&&value.sizeLife.x>0&&value.sizeLife.x<=32
        &&value.sizeLife.y>0&&value.sizeLife.y<=32&&value.sizeLife.z>0&&value.sizeLife.z<=10
        &&value.velocityAge.w<=value.sizeLife.z
        &&glm::all(glm::greaterThanEqual(value.color,glm::vec4(0)))
        &&glm::all(glm::lessThanEqual(value.color,glm::vec4(1)));
}
}

bool CoveEffectsPath::init(WGPUDevice device,WGPUQueue queue,WGPUTextureFormat format,const std::filesystem::path& shader) {
    shutdown();if(!device||!queue)return false;
    device_=device;queue_=queue;
    instances_=gpu::createBuffer(device,gpu::BufferDesc::storage(maximumInstances*sizeof(Instance),true,"cove_effect_instances"));
    uniforms_=gpu::createBuffer(device,gpu::BufferDesc::uniform(sizeof(Uniforms),"cove_effect_uniforms"));
    shader_=gpu::loadShaderModule(device,shader,"cove_effects");
    const std::array entries{
        gpu::BindGroupLayoutEntry(0).vertexVisible().fragmentVisible().uniformBuffer(false,sizeof(Uniforms)),
        gpu::BindGroupLayoutEntry(1).vertexVisible().storageBuffer(true,false,sizeof(Instance)),
        gpu::BindGroupLayoutEntry(2).fragmentVisible().texture(WGPUTextureSampleType_UnfilterableFloat),
        gpu::BindGroupLayoutEntry(3).vertexVisible().fragmentVisible().texture(WGPUTextureSampleType_Float,WGPUTextureViewDimension_2DArray),
        gpu::BindGroupLayoutEntry(4).vertexVisible().fragmentVisible().sampler(WGPUSamplerBindingType_Filtering)};
    layout_=gpu::createBindGroupLayout(device,entries,"cove_effect_bindings");
    if(!instances_||!uniforms_||!shader_||!layout_){shutdown();return false;}
    pipelineLayout_=gpu::createPipelineLayout(device,std::array{layout_},"cove_effect_pipeline_layout");
    WGPUBlendState blend{};
    blend.color={WGPUBlendOperation_Add,WGPUBlendFactor_One,WGPUBlendFactor_OneMinusSrcAlpha};
    blend.alpha={WGPUBlendOperation_Add,WGPUBlendFactor_One,WGPUBlendFactor_OneMinusSrcAlpha};
    WGPUColorTargetState color{};color.format=format;color.writeMask=WGPUColorWriteMask_All;color.blend=&blend;
    WGPUFragmentState fragment{};fragment.module=shader_;WGPU_SET_ENTRY_POINT(fragment,"fs");
    fragment.targetCount=1;fragment.targets=&color;
    WGPURenderPipelineDescriptor descriptor{};WGPU_SET_LABEL(descriptor,"cove_effect_pipeline");
    descriptor.layout=pipelineLayout_;descriptor.vertex.module=shader_;WGPU_SET_ENTRY_POINT(descriptor.vertex,"vs");
    descriptor.primitive.topology=WGPUPrimitiveTopology_TriangleList;
    descriptor.primitive.frontFace=WGPUFrontFace_CCW;descriptor.primitive.cullMode=WGPUCullMode_None;
    descriptor.multisample.count=1;descriptor.multisample.mask=0xffffffff;
    descriptor.fragment=&fragment;
    if(pipelineLayout_)pipeline_=::voxy::gpu::createRenderPipeline(device,&descriptor);
    if(!pipeline_){shutdown();return false;}
    return true;
}

void CoveEffectsPath::shutdown() noexcept {
    if(bindings_)wgpuBindGroupRelease(bindings_);
    if(pipeline_)wgpuRenderPipelineRelease(pipeline_);
    if(pipelineLayout_)wgpuPipelineLayoutRelease(pipelineLayout_);
    if(layout_)wgpuBindGroupLayoutRelease(layout_);
    if(shader_)wgpuShaderModuleRelease(shader_);
    if(instances_)wgpuBufferRelease(instances_);
    if(uniforms_)wgpuBufferRelease(uniforms_);
    bindings_=nullptr;pipeline_=nullptr;pipelineLayout_=nullptr;layout_=nullptr;shader_=nullptr;
    instances_=uniforms_=nullptr;boundDepth_=boundWater_=nullptr;boundSampler_=nullptr;
    device_=nullptr;queue_=nullptr;encoded_=0;bindingChanges_=uploads_=0;
}

bool CoveEffectsPath::render(WGPUCommandEncoder encoder,WGPUTextureView color,WGPUTextureView depth,
    const CoveEffectsFrame& frame,std::span<const Instance> instances) {
    encoded_=0;
    if(!pipeline_||!encoder||!color||instances.size()>maximumInstances)return false;
    if(instances.empty())return true;
    if(!depth||!frame.camera||!frame.camera->isValid()||!frame.water.valid())return false;
    for(int i=0;i<3;++i)if(!std::isfinite(frame.sceneOrigin[i])||std::abs(frame.sceneOrigin[i])>1e6)return false;
    for(const auto& instance:instances)if(!valid(instance))return false;
    if(!bindings_||boundDepth_!=depth||boundWater_!=frame.water.displacementTexture||boundSampler_!=frame.water.displacementSampler) {
        const std::array entries{
            gpu::BindGroupEntry(0).buffer(uniforms_,0,sizeof(Uniforms)),
            gpu::BindGroupEntry(1).buffer(instances_,0,maximumInstances*sizeof(Instance)),
            gpu::BindGroupEntry(2).textureView(depth),
            gpu::BindGroupEntry(3).textureView(frame.water.displacementTexture),
            gpu::BindGroupEntry(4).sampler(frame.water.displacementSampler)};
        auto next=gpu::createBindGroup(device_,layout_,entries,"cove_effect_bound_frame");
        if(!next)return false;
        if(bindings_)wgpuBindGroupRelease(bindings_);
        bindings_=next;boundDepth_=depth;boundWater_=frame.water.displacementTexture;
        boundSampler_=frame.water.displacementSampler;++bindingChanges_;
    }
    std::copy(instances.begin(),instances.end(),sorted_.begin());
    const auto eye=glm::vec3(frame.camera->cameraPos)-glm::vec3(frame.sceneOrigin);
    const auto distance=[&](const Instance& value){const auto d=glm::vec3(value.positionKind)-eye;return glm::dot(d,d);};
    // Fixed storage and stack-only introsort. One premultiplied back-to-front
    // draw avoids one-pass-per-particle allocation or attachment feedback.
    std::sort(sorted_.begin(),sorted_.begin()+static_cast<std::ptrdiff_t>(instances.size()),
        [&](const auto& a,const auto& b){return distance(a)>distance(b);});
    const Uniforms uniforms{*frame.camera,glm::vec4(glm::vec3(frame.sceneOrigin),0)};
    if(!gpu::writeBuffer(queue_,uniforms_,0,uniforms)
        ||!gpu::writeBuffer(queue_,instances_,0,std::span<const Instance>(sorted_.data(),instances.size())))return false;
    ++uploads_;
    WGPURenderPassColorAttachment target{};target.view=color;
    target.depthSlice=WGPU_DEPTH_SLICE_UNDEFINED;target.loadOp=WGPULoadOp_Load;target.storeOp=WGPUStoreOp_Store;
    WGPURenderPassDescriptor descriptor{};WGPU_SET_LABEL(descriptor,"cove_effects_after_water");
    descriptor.colorAttachmentCount=1;descriptor.colorAttachments=&target;
    auto pass=wgpuCommandEncoderBeginRenderPass(encoder,&descriptor);if(!pass)return false;
    wgpuRenderPassEncoderSetPipeline(pass,pipeline_);
    wgpuRenderPassEncoderSetBindGroup(pass,0,bindings_,0,nullptr);
    wgpuRenderPassEncoderDraw(pass,6,static_cast<uint32_t>(instances.size()),0,0);
    wgpuRenderPassEncoderEnd(pass);wgpuRenderPassEncoderRelease(pass);
    encoded_=static_cast<uint32_t>(instances.size());return true;
}

} // namespace voxy::render
