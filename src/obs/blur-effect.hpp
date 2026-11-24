#pragma once
// Two separable Gaussian passes. Input/output targets are always distinct.
constexpr const char *QrBlurEffect = R"OBS(
uniform float4x4 ViewProj;
uniform texture2d image;
uniform float2 uv_offset;
uniform float2 uv_scale;
uniform float2 uv_min;
uniform float2 uv_max;
uniform float2 sample_step;
uniform float blur_sigma;
sampler_state blur_sampler { Filter = Linear; AddressU = Clamp; AddressV = Clamp; };
struct VertData { float4 pos : POSITION; float2 uv : TEXCOORD0; };
VertData VSBlur(VertData v) {
    VertData o;
    o.pos = mul(float4(v.pos.xyz, 1.0), ViewProj);
    o.uv = v.uv;
    return o;
}
float4 PSBlur(VertData v) : TARGET {
    float2 center = uv_offset + v.uv * uv_scale;
    if (blur_sigma < 0.5) return float4(image.Sample(blur_sampler, clamp(center, uv_min, uv_max)).rgb, 1.0);
    float3 rgb = float3(0.0, 0.0, 0.0);
    float total = 0.0;
    for (int i = -48; i <= 48; ++i) {
        float distance = float(i) / max(blur_sigma, 0.5);
        float weight = exp(-0.5 * distance * distance);
        float2 at = clamp(center + float(i) * sample_step, uv_min, uv_max);
        rgb += image.Sample(blur_sampler, at).rgb * weight;
        total += weight;
    }
    return float4(rgb / total, 1.0);
}
technique Draw { pass { vertex_shader = VSBlur(v); pixel_shader = PSBlur(v); } }
)OBS";
