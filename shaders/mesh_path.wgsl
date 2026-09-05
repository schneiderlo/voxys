struct MeshUniforms {
    viewProj : mat4x4<f32>,
    cameraPosition : vec4<f32>,
    lightDirectionFogDensity : vec4<f32>,
    sunColorIntensity : vec4<f32>,
    ambientColorIntensity : vec4<f32>,
    fogColorExposure : vec4<f32>,
};

struct GpuDrawInstance {
    modelMatrix : mat4x4<f32>,
    tintColor : vec4<f32>,
    emissiveBoost : f32,
    materialIndex : u32,
    padding : vec2<u32>,
};

struct GpuMaterial {
    baseColorFactor : vec4<f32>,
    emissiveFactorAlphaCutoff : vec4<f32>,
    metallicRoughnessNormalOcclusion : vec4<f32>,
    flags : vec4<u32>,
};

@group(0) @binding(0) var<uniform> uniforms : MeshUniforms;
@group(0) @binding(1) var<storage, read> instances : array<GpuDrawInstance>;
@group(0) @binding(2) var<storage, read> materials : array<GpuMaterial>;
@group(0) @binding(3) var environmentTexture : texture_2d<f32>;
@group(0) @binding(4) var environmentSampler : sampler;
@group(0) @binding(5) var rayDepth : texture_2d<f32>;
@group(0) @binding(6) var baseColorTexture : texture_2d<f32>;
@group(0) @binding(7) var normalTexture : texture_2d<f32>;
@group(0) @binding(8) var metallicRoughnessTexture : texture_2d<f32>;
@group(0) @binding(9) var emissiveTexture : texture_2d<f32>;
@group(0) @binding(10) var materialSampler : sampler;

fn safeNormalize(value : vec3<f32>, fallback : vec3<f32>) -> vec3<f32> {
    let lengthSquared = dot(value, value);
    let normalized = value * inverseSqrt(max(lengthSquared, 1.0e-12));
    return select(fallback, normalized, lengthSquared > 1.0e-12);
}

struct VertexInput {
    @location(0) position : vec3<f32>,
    @location(1) normal : vec3<f32>,
    @location(2) tangent : vec4<f32>,
    @location(3) texCoord : vec2<f32>,
    @location(4) joints : vec4<u32>,
    @location(5) weights : vec4<f32>,
    @builtin(instance_index) instanceIndex : u32,
};

struct VertexOutput {
    @builtin(position) position : vec4<f32>,
    @location(0) worldPosition : vec3<f32>,
    @location(1) worldNormal : vec3<f32>,
    @location(2) tintColor : vec4<f32>,
    @location(3) @interpolate(flat) materialIndex : u32,
    @location(4) @interpolate(flat) emissiveBoost : f32,
    @location(5) texCoord : vec2<f32>,
    @location(6) worldTangent : vec4<f32>,
};

@vertex
fn vs(input : VertexInput) -> VertexOutput {
    let instance = instances[input.instanceIndex];
    let worldPosition = instance.modelMatrix * vec4<f32>(input.position, 1.0);

    let modelX = instance.modelMatrix[0].xyz;
    let modelY = instance.modelMatrix[1].xyz;
    let modelZ = instance.modelMatrix[2].xyz;
    let cofactorX = cross(modelY, modelZ);
    let cofactorY = cross(modelZ, modelX);
    let cofactorZ = cross(modelX, modelY);
    let determinantSign = select(-1.0, 1.0, dot(modelX, cofactorX) >= 0.0);
    let normalMatrix = mat3x3<f32>(cofactorX, cofactorY, cofactorZ);

    var output : VertexOutput;
    output.position = uniforms.viewProj * worldPosition;
    output.worldPosition = worldPosition.xyz;
    output.worldNormal = safeNormalize(
        normalMatrix * input.normal * determinantSign, vec3<f32>(0.0, 1.0, 0.0));
    output.tintColor = instance.tintColor;
    output.materialIndex = instance.materialIndex;
    output.emissiveBoost = instance.emissiveBoost;
    output.texCoord = input.texCoord;
    let tangentDirection = safeNormalize(
        (instance.modelMatrix * vec4<f32>(input.tangent.xyz, 0.0)).xyz,
        vec3<f32>(1.0, 0.0, 0.0));
    output.worldTangent = vec4<f32>(
        tangentDirection, input.tangent.w * determinantSign);
    return output;
}

fn distributionGGX(normal : vec3<f32>, halfVector : vec3<f32>,
                   roughness : f32) -> f32 {
    let alpha = roughness * roughness;
    let alphaSquared = alpha * alpha;
    let normalDotHalf = max(dot(normal, halfVector), 0.0);
    let denominatorTerm = normalDotHalf * normalDotHalf
        * (alphaSquared - 1.0) + 1.0;
    return alphaSquared
        / max(3.141592653589793 * denominatorTerm * denominatorTerm, 1.0e-6);
}

fn geometrySchlickGGX(normalDotDirection : f32, roughness : f32) -> f32 {
    let shifted = roughness + 1.0;
    let k = shifted * shifted * 0.125;
    return normalDotDirection
        / max(normalDotDirection * (1.0 - k) + k, 1.0e-6);
}

fn geometrySmith(normal : vec3<f32>, viewDirection : vec3<f32>,
                 lightDirection : vec3<f32>, roughness : f32) -> f32 {
    return geometrySchlickGGX(max(dot(normal, viewDirection), 0.0), roughness)
        * geometrySchlickGGX(max(dot(normal, lightDirection), 0.0), roughness);
}

fn fresnelSchlick(cosine : f32, f0 : vec3<f32>) -> vec3<f32> {
    return f0 + (vec3<f32>(1.0) - f0)
        * pow(clamp(1.0 - cosine, 0.0, 1.0), 5.0);
}

fn fresnelSchlickRoughness(cosine : f32, f0 : vec3<f32>,
                          roughness : f32) -> vec3<f32> {
    return f0 + (max(vec3<f32>(1.0 - roughness), f0) - f0)
        * pow(clamp(1.0 - cosine, 0.0, 1.0), 5.0);
}

fn directionToEquirectangular(directionInput : vec3<f32>) -> vec2<f32> {
    let direction = safeNormalize(directionInput, vec3<f32>(0.0, 1.0, 0.0));
    let longitude = atan2(direction.z, direction.x);
    let latitude = atan2(clamp(direction.y, -1.0, 1.0),
                         max(length(direction.xz), 1.0e-6));
    return vec2<f32>(longitude * 0.15915494309189535 + 0.5,
                     0.5 - latitude * 0.3183098861837907);
}

fn sampleEnvironment(direction : vec3<f32>, lod : f32) -> vec3<f32> {
    return textureSampleLevel(environmentTexture, environmentSampler,
                              directionToEquirectangular(direction), lod).rgb;
}

fn acesFilmic(inputColor : vec3<f32>) -> vec3<f32> {
    let inputMatrix = mat3x3<f32>(
        vec3<f32>(0.59719, 0.07600, 0.02840),
        vec3<f32>(0.35458, 0.90834, 0.13383),
        vec3<f32>(0.04823, 0.01566, 0.83777));
    let outputMatrix = mat3x3<f32>(
        vec3<f32>(1.60475, -0.10208, -0.00327),
        vec3<f32>(-0.53108, 1.10813, -0.07276),
        vec3<f32>(-0.07367, -0.00605, 1.07602));
    let color = inputMatrix * (inputColor / 0.6);
    let numerator = color * (color + vec3<f32>(0.0245786))
        - vec3<f32>(0.000090537);
    let denominator = color * ((color + vec3<f32>(0.432951)) * 0.983729)
        + vec3<f32>(0.238081);
    return clamp(outputMatrix * (numerator / denominator),
                 vec3<f32>(0.0), vec3<f32>(1.0));
}

fn linearToSrgb(linear : vec3<f32>) -> vec3<f32> {
    let low = linear * 12.92;
    let high = 1.055 * pow(max(linear, vec3<f32>(0.0)),
                           vec3<f32>(1.0 / 2.4)) - vec3<f32>(0.055);
    return select(high, low, linear <= vec3<f32>(0.0031308));
}

@fragment
fn fs(input : VertexOutput,
      @builtin(front_facing) frontFacing : bool) -> @location(0) vec4<f32> {
    // Material selection is flat per primitive but non-uniform across a quad.
    // Compute derivatives before any material-dependent branch, then use the
    // explicit-gradient sampling operations inside those branches. Plain
    // textureSample is invalid in non-uniform control flow on browser WebGPU.
    let texCoordDx = dpdx(input.texCoord);
    let texCoordDy = dpdy(input.texCoord);
    let material = materials[input.materialIndex];
    if (!frontFacing && material.flags.y == 0u) {
        discard;
    }
    let textureMask = material.flags.w;
    var baseColorSample = vec4<f32>(1.0);
    if ((textureMask & 1u) != 0u) {
        baseColorSample = textureSampleGrad(
            baseColorTexture, materialSampler, input.texCoord,
            texCoordDx, texCoordDy);
    }
    let baseColor = material.baseColorFactor * input.tintColor
        * baseColorSample;
    let alpha = clamp(baseColor.a, 0.0, 1.0);
    if (material.flags.x == 1u
        && alpha < material.emissiveFactorAlphaCutoff.w) {
        discard;
    }

    let cameraDelta = uniforms.cameraPosition.xyz - input.worldPosition;
    let distanceToCamera = length(cameraDelta);
    if (uniforms.cameraPosition.w > 0.5) {
        let dimensions = textureDimensions(rayDepth, 0);
        let pixel = clamp(vec2<i32>(input.position.xy), vec2<i32>(0),
                          vec2<i32>(dimensions) - vec2<i32>(1));
        let terrainDistance = textureLoad(rayDepth, pixel, 0).x;
        if (terrainDistance >= 0.0
            && distanceToCamera > terrainDistance + 0.02) {
            discard;
        }
    }

    var normal = safeNormalize(input.worldNormal, vec3<f32>(0.0, 1.0, 0.0));
    if (!frontFacing && material.flags.y != 0u) {
        normal = -normal;
    }
    if ((textureMask & 2u) != 0u) {
        let tangent = safeNormalize(
            input.worldTangent.xyz - normal * dot(normal, input.worldTangent.xyz),
            vec3<f32>(1.0, 0.0, 0.0));
        let bitangent = safeNormalize(
            cross(normal, tangent) * input.worldTangent.w,
            vec3<f32>(0.0, 0.0, 1.0));
        let encodedNormal = textureSampleGrad(
            normalTexture, materialSampler, input.texCoord,
            texCoordDx, texCoordDy).xyz;
        let tangentNormal = safeNormalize(
            vec3<f32>((encodedNormal.xy * 2.0 - vec2<f32>(1.0))
                      * material.metallicRoughnessNormalOcclusion.z,
                      encodedNormal.z * 2.0 - 1.0),
            vec3<f32>(0.0, 0.0, 1.0));
        normal = safeNormalize(
            mat3x3<f32>(tangent, bitangent, normal) * tangentNormal,
            normal);
    }
    let viewDirection = safeNormalize(
        cameraDelta, vec3<f32>(0.0, 0.0, 1.0));
    let lightDirection = safeNormalize(
        uniforms.lightDirectionFogDensity.xyz, vec3<f32>(0.0, 1.0, 0.0));
    let halfVector = safeNormalize(
        viewDirection + lightDirection, normal);
    let normalDotLight = max(dot(normal, lightDirection), 0.0);
    let normalDotView = max(dot(normal, viewDirection), 0.0);

    let albedo = max(baseColor.rgb, vec3<f32>(0.0));
    var metallicRoughnessSample = vec4<f32>(1.0);
    if ((textureMask & 4u) != 0u) {
        metallicRoughnessSample = textureSampleGrad(
            metallicRoughnessTexture, materialSampler, input.texCoord,
            texCoordDx, texCoordDy);
    }
    let metallic = clamp(material.metallicRoughnessNormalOcclusion.x
                         * metallicRoughnessSample.b, 0.0, 1.0);
    let roughness = clamp(material.metallicRoughnessNormalOcclusion.y
                          * metallicRoughnessSample.g, 0.045, 1.0);
    let f0 = mix(vec3<f32>(0.04), albedo, metallic);
    let fresnel = fresnelSchlick(max(dot(halfVector, viewDirection), 0.0), f0);
    let distribution = distributionGGX(normal, halfVector, roughness);
    let geometry = geometrySmith(normal, viewDirection, lightDirection, roughness);
    let specular = distribution * geometry * fresnel
        / max(4.0 * normalDotView * normalDotLight, 1.0e-5);
    let diffuseWeight = (vec3<f32>(1.0) - fresnel) * (1.0 - metallic);
    let sunRadiance = uniforms.sunColorIntensity.rgb
        * max(uniforms.sunColorIntensity.w, 0.0);
    let direct = (diffuseWeight * albedo * 0.3183098861837907 + specular)
        * sunRadiance * normalDotLight;

    let mipCount = textureNumLevels(environmentTexture);
    let maximumLod = f32(max(mipCount, 1u) - 1u);
    let diffuseEnvironment = sampleEnvironment(normal, maximumLod);
    let reflectionDirection = reflect(-viewDirection, normal);
    let specularEnvironment = sampleEnvironment(
        reflectionDirection, roughness * maximumLod);
    let environmentFresnel = fresnelSchlickRoughness(
        normalDotView, f0, roughness);
    let environmentDiffuseWeight = (vec3<f32>(1.0) - environmentFresnel)
        * (1.0 - metallic);
    let ambientScale = uniforms.ambientColorIntensity.rgb
        * max(uniforms.ambientColorIntensity.w, 0.0);
    let ambient = (environmentDiffuseWeight * albedo * diffuseEnvironment
        + environmentFresnel * specularEnvironment) * ambientScale;

    var emissiveSample = vec3<f32>(1.0);
    if ((textureMask & 8u) != 0u) {
        emissiveSample = textureSampleGrad(
            emissiveTexture, materialSampler, input.texCoord,
            texCoordDx, texCoordDy).rgb;
    }
    let emissive = max(material.emissiveFactorAlphaCutoff.rgb, vec3<f32>(0.0))
        * max(input.tintColor.rgb, vec3<f32>(0.0)) * emissiveSample
        + vec3<f32>(max(input.emissiveBoost, 0.0));
    let unlit = albedo + emissive;
    let pbr = direct + ambient + emissive;
    var color = select(pbr, unlit, material.flags.z != 0u);

    let fogAmount = clamp(
        1.0 - exp(-max(uniforms.lightDirectionFogDensity.w, 0.0)
                  * distanceToCamera), 0.0, 1.0);
    color = mix(color, max(uniforms.fogColorExposure.rgb, vec3<f32>(0.0)),
                fogAmount);
    let presented = linearToSrgb(acesFilmic(
        color * max(uniforms.fogColorExposure.w, 0.0)));
    let outputAlpha = select(1.0, alpha, material.flags.x == 2u);
    return vec4<f32>(presented, outputAlpha);
}
