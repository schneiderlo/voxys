#include "game/assets/rigid_animation.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

namespace voxy::game::assets {
namespace {
constexpr uint64_t kDecodedLimit = 2u * 1024u * 1024u;
constexpr uint64_t kGpuLimit = 1024u * 1024u;

bool fail(std::string& error, const char* message) {
    error = message;
    return false;
}

float scalar(const moto::VmeshData& data, uint64_t offset) {
    float result;
    std::memcpy(&result, data.channelData.data() + offset, sizeof(result));
    return result;
}

std::string_view name(const moto::VmeshData& data, uint32_t offset) {
    if (offset == 0 || offset >= data.stringBlob.size()) return {};
    const size_t end = data.stringBlob.find('\0', offset);
    if (end == std::string::npos) return {};
    return {data.stringBlob.data() + offset, end - offset};
}

bool rigidMatrix(const glm::dmat4& matrix) {
    for (int c = 0; c < 4; ++c) for (int r = 0; r < 4; ++r)
        if (!std::isfinite(matrix[c][r]) || std::abs(matrix[c][r]) > 100'000.0) return false;
    if (glm::length(glm::dvec3(matrix[3])) > 100'000.0
        || std::abs(matrix[3][3] - 1.0) > 1e-8) return false;
    for (int c = 0; c < 3; ++c) {
        if (std::abs(matrix[c][3]) > 1e-8) return false;
        for (int d = 0; d < 3; ++d)
            if (std::abs(glm::dot(glm::dvec3(matrix[c]), glm::dvec3(matrix[d]))
                - (c == d ? 1.0 : 0.0)) > 1e-6) return false;
    }
    return glm::determinant(glm::dmat3(matrix)) > 0.999999;
}

struct LocalPose { glm::dvec3 translation{0}; glm::dquat rotation{1,0,0,0}; };
using Locals = std::array<LocalPose, kRigidAnimationMaximumNodes>;

glm::dquat canonical(glm::dquat value) {
    value = glm::normalize(value);
    int pivot = 0;
    for (int i = 1; i < 4; ++i) if (std::abs(value[i]) > std::abs(value[pivot])) pivot = i;
    return value[pivot] < 0 ? -value : value;
}

glm::dquat shortest(glm::dquat a, glm::dquat b, double t) {
    // Equivalent signed key encodings must choose the same arc even when their
    // exact 180-degree separation gives dot==0 and neither arc is shorter.
    a = canonical(a); b = canonical(b);
    if (glm::dot(a,b) < 0) b = -b;
    return glm::normalize(glm::slerp(a,b,t));
}

void sampleLocal(const RigidAnimationAsset& asset, uint32_t clipIndex, double seconds,
                 bool loop, Locals& pose) {
    const auto& data = asset.mesh;
    for (size_t i = 0; i < data.nodes.size(); ++i) {
        const auto& n = data.nodes[i];
        pose[i].translation = {n.translation[0],n.translation[1],n.translation[2]};
        pose[i].rotation = glm::normalize(glm::dquat(n.rotation[3],n.rotation[0],n.rotation[1],n.rotation[2]));
    }
    const auto& clip = data.anims[clipIndex];
    double time = std::clamp(seconds,0.0,double(clip.duration));
    if (loop) {
        time = std::fmod(seconds,double(clip.duration));
        if (time < 0) time += double(clip.duration);
    }
    const uint32_t first = static_cast<uint32_t>(clip.channelsOffset / sizeof(moto::VmeshAnimChannel));
    for (uint32_t i = first; i < first + clip.channelCount; ++i) {
        const auto& channel = data.animChannels[i];
        uint32_t left = 0, right = channel.keyCount - 1;
        while (left + 1 < right) {
            const uint32_t mid = left + (right-left)/2;
            if (double(scalar(data,channel.keysOffset + uint64_t{mid}*4u)) <= time) left = mid;
            else right = mid;
        }
        const double firstTime = scalar(data,channel.keysOffset + uint64_t{left}*4u);
        const double lastTime = scalar(data,channel.keysOffset + uint64_t{right}*4u);
        double t = std::clamp((time-firstTime)/(lastTime-firstTime),0.0,1.0);
        if (channel.interpolation == moto::VmeshAnimInterpolationStep) t = time >= lastTime ? 1.0 : 0.0;
        const uint64_t width = channel.path == moto::VmeshAnimPathRotation ? 4u : 3u;
        const uint64_t values = channel.keysOffset + uint64_t{channel.keyCount}*4u;
        std::array<double,4> a{},b{};
        for (uint64_t v = 0; v < width; ++v) {
            a[static_cast<size_t>(v)] = scalar(data,values+(uint64_t{left}*width+v)*4u);
            b[static_cast<size_t>(v)] = scalar(data,values+(uint64_t{right}*width+v)*4u);
        }
        if (channel.path == moto::VmeshAnimPathRotation)
            pose[channel.nodeIndex].rotation = shortest({a[3],a[0],a[1],a[2]},{b[3],b[0],b[1],b[2]},t);
        else pose[channel.nodeIndex].translation = glm::mix(glm::dvec3(a[0],a[1],a[2]),glm::dvec3(b[0],b[1],b[2]),t);
    }
}
} // namespace

bool prepareRigidAnimation(moto::VmeshData data, RigidAnimationAsset& output, std::string& error) {
    error.clear();
    const auto& h = data.header;
    if (h.flags != moto::kVmeshHasAnimations || h.skinCount || h.jointCount
        || !data.skins.empty() || !data.joints.empty()
        || h.nodeCount == 0 || h.nodeCount > kRigidAnimationMaximumNodes || h.nodeCount != data.nodes.size()
        || h.animCount != kRobotClips.size() || h.animCount != data.anims.size()
        || h.animChannelCount != data.animChannels.size() || h.animChannelCount > 768
        || data.channelData.size() > 256u*1024u)
        return fail(error,"animation: unsupported counts/skin/layout");
    RigidAnimationAsset pending;
    uint32_t root = UINT32_MAX;
    for (size_t i = 0; i < data.nodes.size(); ++i) {
        const auto& node = data.nodes[i];
        const auto nodeName = name(data,node.nameOffset);
        if (nodeName.empty() || nodeName.size() > 64) return fail(error,"animation: missing/long node name");
        for (size_t j = 0; j < i; ++j)
            if (name(data,data.nodes[j].nameOffset) == nodeName) return fail(error,"animation: duplicate node name");
        if (node.parent < 0) {
            if (node.parent != -1 || root != UINT32_MAX || nodeName != "robot_root"
                || node.meshIndex != UINT32_MAX) return fail(error,"animation: one meshless robot_root required");
            root = static_cast<uint32_t>(i);
            for (float v : node.translation) if (v != 0) return fail(error,"animation: root motion forbidden");
            if (node.rotation[0] != 0 || node.rotation[1] != 0 || node.rotation[2] != 0
                || node.rotation[3] != 1) return fail(error,"animation: root rotation must be identity");
        }
        for (float scale : node.scale) if (std::abs(scale - 1.0f) > 1e-6f)
            return fail(error,"animation: unit node scale required");
        for (float translation : node.translation) if (!std::isfinite(translation) || std::abs(translation) > 3)
            return fail(error,"animation: translation bound");
    }
    if (root == UINT32_MAX) return fail(error,"animation: missing root");
    for (size_t anchor = 0; anchor < kRobotAnchors.size(); ++anchor) {
        auto it = std::find_if(data.nodes.begin(),data.nodes.end(),[&](const auto& node) {
            return name(data,node.nameOffset) == kRobotAnchors[anchor]; });
        if (it == data.nodes.end() || it->meshIndex != UINT32_MAX || it->parent < 0)
            return fail(error,"animation: missing/nonempty anchor");
        pending.anchors[anchor] = static_cast<uint32_t>(it-data.nodes.begin());
    }
    std::array<bool,768> ownedChannels{};
    for (size_t expected = 0; expected < kRobotClips.size(); ++expected) {
        auto it = std::find_if(data.anims.begin(),data.anims.end(),[&](const auto& clip) {
            return name(data,clip.nameOffset) == kRobotClips[expected]; });
        if (it == data.anims.end()) return fail(error,"animation: required clip missing");
        pending.clips[expected] = static_cast<uint32_t>(it-data.anims.begin());
        if (!std::isfinite(it->duration) || it->duration <= 0 || it->duration > 10
            || it->channelCount == 0 || it->channelsOffset % sizeof(moto::VmeshAnimChannel))
            return fail(error,"animation: clip duration/channels");
        const uint64_t first = it->channelsOffset / sizeof(moto::VmeshAnimChannel);
        if (first > data.animChannels.size() || it->channelCount > data.animChannels.size()-first)
            return fail(error,"animation: channel range");
        std::array<uint8_t,kRigidAnimationMaximumNodes> paths{};
        for (uint64_t index = first; index < first + it->channelCount; ++index) {
            if (ownedChannels[static_cast<size_t>(index)]) return fail(error,"animation: shared clip channel");
            ownedChannels[static_cast<size_t>(index)] = true;
            const auto& c = data.animChannels[static_cast<size_t>(index)];
            if (c.nodeIndex >= data.nodes.size() || c.nodeIndex == root || c.path > moto::VmeshAnimPathRotation
                || c.interpolation > moto::VmeshAnimInterpolationStep || c.keyCount < 2 || c.keyCount > 121
                || c.keysOffset % 4u) return fail(error,"animation: channel target/keys/interpolation");
            const uint8_t bit = static_cast<uint8_t>(1u << c.path);
            if (paths[c.nodeIndex] & bit) return fail(error,"animation: duplicate node/path");
            paths[c.nodeIndex] |= bit;
            const uint64_t width = c.path == moto::VmeshAnimPathRotation ? 4u : 3u;
            const uint64_t bytes = uint64_t{c.keyCount} * (width+1u)*4u;
            if (c.keysOffset > data.channelData.size() || bytes > data.channelData.size()-c.keysOffset)
                return fail(error,"animation: key byte range");
            double previous = -1;
            for (uint32_t key = 0; key < c.keyCount; ++key) {
                const double t = scalar(data,c.keysOffset+uint64_t{key}*4u);
                if (!std::isfinite(t) || t <= previous || t < 0 || t > double(it->duration)+1e-6
                    || (key == 0 && t != 0)) return fail(error,"animation: strictly increasing nonnegative keys required");
                previous = t;
                double norm = 0;
                for (uint64_t v = 0; v < width; ++v) {
                    const double value = scalar(data,c.keysOffset+(uint64_t{c.keyCount}+uint64_t{key}*width+v)*4u);
                    if (!std::isfinite(value) || (width == 3 && std::abs(value) > 3))
                        return fail(error,"animation: finite bounded key values required");
                    norm += value*value;
                }
                if (width == 4 && std::abs(norm-1.0) > 1e-4) return fail(error,"animation: unit quaternion keys required");
            }
        }
    }
    for (size_t i = 0; i < data.animChannels.size(); ++i)
        if (!ownedChannels[i]) return fail(error,"animation: orphan channel");

    // Animation semantics are fully validated above. Reuse the independent
    // rigid geometry/material/normal/hierarchy checks on its explicit rest view.
    // The original owned clip data remains intact in the admitted asset.
    auto rest = data;
    rest.header.flags = 0;
    rest.header.animCount = rest.header.animChannelCount = 0;
    rest.anims.clear(); rest.animChannels.clear(); rest.channelData.clear();
    RigidPrefabLimits limits;
    limits.maximumNodes = 32; limits.maximumMeshes = 24; limits.maximumSubmeshes = 48;
    limits.maximumMaterials = 8; limits.maximumMeshInstances = 24; limits.maximumExpandedDraws = 48;
    limits.maximumGpuBytes = kGpuLimit; limits.maximumDecodedBytes = kDecodedLimit;
    limits.maximumAbsoluteCoordinate = 3;
    if (!prepareRigidPrefab(rest,construction::CubeRotation{12},limits,pending.prefab,error)) return false;
    const uint64_t animationBytes = data.anims.size()*sizeof(moto::VmeshAnim)
        + data.animChannels.size()*sizeof(moto::VmeshAnimChannel) + data.channelData.size();
    if (animationBytes > kDecodedLimit-pending.prefab.counts.decodedBytes)
        return fail(error,"animation: decoded byte budget");
    pending.prefab.counts.decodedBytes += animationBytes;
    std::array<bool,kRigidAnimationMaximumNodes> evaluated{};
    size_t count = 0;
    while (count < data.nodes.size()) {
        const size_t before = count;
        for (size_t i = 0; i < data.nodes.size(); ++i) {
            const auto parent = data.nodes[i].parent;
            if (!evaluated[i] && (parent < 0 || evaluated[static_cast<size_t>(parent)])) {
                pending.evaluationOrder[count++] = static_cast<uint32_t>(i);
                evaluated[i] = true;
            }
        }
        if (count == before) return fail(error,"animation: hierarchy cycle");
    }
    pending.mesh = std::move(data);
    output = std::move(pending);
    return true;
}

bool sampleRigidAnimation(const RigidAnimationAsset& asset, uint32_t clipIndex,
    double timeSeconds, bool loop, std::optional<RigidAnimationBlend> blend,
    const glm::dmat4& cameraRelativeRoot, RigidAnimationPose& output, std::string& error) {
    error.clear();
    if (clipIndex >= asset.mesh.anims.size() || !std::isfinite(timeSeconds)
        || asset.mesh.nodes.size() > kRigidAnimationMaximumNodes
        || asset.prefab.meshNodes.size() > kRigidAnimationMaximumDraws || !rigidMatrix(cameraRelativeRoot)
        || (blend && (blend->clipIndex >= asset.mesh.anims.size() || !std::isfinite(blend->timeSeconds)
            || !std::isfinite(blend->weight) || blend->weight < 0 || blend->weight > 1)))
        return fail(error,"animation: invalid sample/root/blend");
    Locals locals;
    sampleLocal(asset,clipIndex,timeSeconds,loop,locals);
    if (blend && blend->weight > 0) {
        Locals other;
        sampleLocal(asset,blend->clipIndex,blend->timeSeconds,blend->loop.value_or(loop),other);
        for (size_t i = 0; i < asset.mesh.nodes.size(); ++i) {
            locals[i].translation = glm::mix(locals[i].translation,other[i].translation,blend->weight);
            locals[i].rotation = shortest(locals[i].rotation,other[i].rotation,blend->weight);
        }
    }
    RigidAnimationPose pending;
    for (size_t order = 0; order < asset.mesh.nodes.size(); ++order) {
        const uint32_t i = asset.evaluationOrder[order];
        const auto local = glm::translate(glm::dmat4(1),locals[i].translation)*glm::mat4_cast(locals[i].rotation);
        const auto parent = asset.mesh.nodes[i].parent;
        pending.nodeToAsset[i] = parent < 0 ? local : pending.nodeToAsset[static_cast<size_t>(parent)]*local;
    }
    // Basis12 is explicit: export X/Z axes are reversed relative to canonical.
    const glm::dmat4 basis(glm::dvec4(-1,0,0,0),glm::dvec4(0,1,0,0),
                           glm::dvec4(0,0,-1,0),glm::dvec4(0,0,0,1));
    const glm::dmat4 root = cameraRelativeRoot*basis;
    for (const auto& node : asset.prefab.meshNodes) {
        const auto transform = root*pending.nodeToAsset[node.nodeIndex];
        pending.draws[pending.drawCount++] = {node.nodeIndex,node.meshIndex,glm::mat4(transform)};
        const auto& bound = asset.prefab.meshBounds[node.meshIndex];
        for (int corner = 0; corner < 8; ++corner) {
            const glm::dvec3 p = glm::dvec3(transform*glm::dvec4(
                corner&1 ? bound.maximum.x : bound.minimum.x,
                corner&2 ? bound.maximum.y : bound.minimum.y,
                corner&4 ? bound.maximum.z : bound.minimum.z,1));
            if (!pending.bounds.valid) pending.bounds = {p,p,true};
            else { pending.bounds.minimum = glm::min(pending.bounds.minimum,p);
                   pending.bounds.maximum = glm::max(pending.bounds.maximum,p); }
        }
    }
    for (size_t i = 0; i < asset.anchors.size(); ++i)
        pending.anchors[i] = root*pending.nodeToAsset[asset.anchors[i]];
    output = pending;
    return true;
}
} // namespace voxy::game::assets
