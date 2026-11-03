#include "backdrop.hpp"

#include <d3dcompiler.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

namespace backdrop {
namespace {

const char* kShaderSource = R"HLSL(
cbuffer FrameParams : register(b0)
{
    float4 p0;       // x=width y=height z=time w=intensity
    float4 p1;       // x=mouseX y=mouseY z=caustic w=grain
    float4 cBg0;
    float4 cBg1;
    float4 cBg2;
    float4 cBlob0;
    float4 cBlob1;
    float4 cBlob2;
    float4 cPrimary;
    float4 pRipple;  // xy = uv, z = age, w = active
};

struct VSOut
{
    float4 pos : SV_POSITION;
    float2 uv  : TEXCOORD0;
};

VSOut VSMain(uint id : SV_VertexID)
{
    float2 uv = float2((float)((id << 1) & 2), (float)(id & 2));
    VSOut output;
    output.uv = uv;
    output.pos = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    return output;
}

float Hash21(float2 p)
{
    p = frac(p * float2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return frac(p.x * p.y);
}

float ValueNoise(float2 p)
{
    float2 i = floor(p);
    float2 f = frac(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = Hash21(i);
    float b = Hash21(i + float2(1.0, 0.0));
    float c = Hash21(i + float2(0.0, 1.0));
    float d = Hash21(i + float2(1.0, 1.0));
    return lerp(lerp(a, b, f.x), lerp(c, d, f.x), f.y);
}

float Fbm(float2 p)
{
    float value = 0.0;
    float amplitude = 0.5;
    for (int i = 0; i < 4; ++i)
    {
        value += amplitude * ValueNoise(p);
        p = p * 2.03 + 11.7;
        amplitude *= 0.5;
    }
    return value;
}

float Blob(float2 uv, float2 center, float radius)
{
    float d = length((uv - center) * float2(1.0, 1.22));
    return saturate(1.0 - d / radius);
}

float4 PSMain(VSOut input) : SV_TARGET
{
    float2 resolution = max(p0.xy, float2(1.0, 1.0));
    float time = p0.z;
    float intensity = saturate(p0.w);

    float2 uv = input.uv;
    float2 aspect = float2(resolution.x / resolution.y, 1.0);
    float2 p = (uv - 0.5) * aspect;

    // Domain-warped noise gives the slow liquid vein structure.
    float2 q = p * 1.6;
    q += 0.22 * float2(sin(q.y * 1.7 + time * 0.21), cos(q.x * 1.4 - time * 0.17));
    float n1 = Fbm(q + float2(time * 0.035, -time * 0.028));
    float n2 = Fbm(q * 1.9 + float2(-time * 0.026, time * 0.031) + n1 * 1.2);
    float veins = smoothstep(0.35, 0.95, n2) * 0.9 + smoothstep(0.62, 1.0, n1) * 0.35;

    float3 color = lerp(cBg0.rgb, cBg1.rgb, saturate(0.30 + uv.y * 0.80));
    color = lerp(color, cBg2.rgb, saturate(0.30 + 0.55 * n2) * 0.60);

    // Three drifting light blobs.
    float2 b0 = float2(0.28 + 0.17 * sin(time * 0.13), 0.34 + 0.15 * cos(time * 0.11));
    float2 b1 = float2(0.76 + 0.14 * cos(time * 0.09 + 1.3), 0.64 + 0.12 * sin(time * 0.12 + 0.7));
    float2 b2 = float2(0.52 + 0.21 * sin(time * 0.07 + 2.1), 0.18 + 0.11 * cos(time * 0.10 + 2.6));
    float f0 = Blob(uv, b0, 0.56) * (0.75 + 0.25 * sin(time * 0.50));
    float f1 = Blob(uv, b1, 0.64) * (0.70 + 0.30 * cos(time * 0.37 + 0.4));
    float f2 = Blob(uv, b2, 0.46) * (0.65 + 0.35 * sin(time * 0.29 + 1.7));
    color += cBlob0.rgb * f0 * 0.30 * intensity;
    color += cBlob1.rgb * f1 * 0.24 * intensity;
    color += cBlob2.rgb * f2 * 0.18 * intensity;

    // Soft cursor light.
    float mouseDistance = length((uv - p1.xy) * aspect);
    float mouseLight = saturate(1.0 - mouseDistance / 0.48);
    color += cPrimary.rgb * mouseLight * mouseLight * 0.055 * intensity;

    // Liquid veins.
    color += cPrimary.rgb * veins * 0.060 * intensity;

    // Caustic shimmer.
    float2 cp = uv * float2(9.0, 6.0) + n1 * 2.4;
    float caustic = pow(saturate(sin(cp.x + time * 0.55) * 0.5 + 0.5), 10.0);
    caustic += pow(saturate(sin(cp.y * 1.3 - time * 0.42 + 1.7) * 0.5 + 0.5), 12.0);
    color += cPrimary.rgb * caustic * 0.035 * p1.z * intensity;

    // Click ripple.
    if (pRipple.w > 0.5)
    {
        float age = pRipple.z;
        float d = length((uv - pRipple.xy) * aspect);
        float ring = saturate(1.0 - abs(d - age * 0.55) / 0.10);
        ring = ring * ring * saturate(1.0 - age / 1.20);
        color += cPrimary.rgb * ring * 0.090;
    }

    // Vignette + film grain.
    float vignette = smoothstep(1.30, 0.25, length(p));
    color *= 0.72 + 0.40 * vignette;

    float grain = Hash21(uv * resolution + frac(time * 13.7));
    color += (grain - 0.5) * p1.w;

    return float4(color, 1.0);
}
)HLSL";

const char* kBlurSource = R"HLSL(
cbuffer BlurParams : register(b0)
{
    float4 p0; // x = texel width, y = texel height, z = dirX, w = dirY
};

Texture2D sourceTexture : register(t0);
SamplerState sourceSampler : register(s0);

struct VSOut
{
    float4 pos : SV_POSITION;
    float2 uv  : TEXCOORD0;
};

VSOut VSMain(uint id : SV_VertexID)
{
    float2 uv = float2((float)((id << 1) & 2), (float)(id & 2));
    VSOut output;
    output.uv = uv;
    output.pos = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    return output;
}

float4 PSMain(VSOut input) : SV_TARGET
{
    float2 step = float2(p0.z * p0.x, p0.w * p0.y);
    float3 color = sourceTexture.Sample(sourceSampler, input.uv).rgb * 0.227027;

    color += sourceTexture.Sample(sourceSampler, input.uv + step * 1.384615).rgb * 0.316216;
    color += sourceTexture.Sample(sourceSampler, input.uv - step * 1.384615).rgb * 0.316216;
    color += sourceTexture.Sample(sourceSampler, input.uv + step * 3.230769).rgb * 0.070270;
    color += sourceTexture.Sample(sourceSampler, input.uv - step * 3.230769).rgb * 0.070270;

    return float4(color, 1.0);
}
)HLSL";

struct BgParams {
    float p0[4];
    float p1[4];
    float cBg0[4];
    float cBg1[4];
    float cBg2[4];
    float cBlob0[4];
    float cBlob1[4];
    float cBlob2[4];
    float cPrimary[4];
    float pRipple[4];
};

struct BlurParams {
    float p0[4];
};

struct Target {
    ID3D11Texture2D* texture = nullptr;
    ID3D11RenderTargetView* rtv = nullptr;
    ID3D11ShaderResourceView* srv = nullptr;
    int width = 0;
    int height = 0;
};

ID3D11Device* g_device = nullptr;
ID3D11DeviceContext* g_context = nullptr;

ID3D11VertexShader* g_vertexShader = nullptr;
ID3D11PixelShader* g_backgroundShader = nullptr;
ID3D11PixelShader* g_blurShader = nullptr;
ID3D11InputLayout* g_dummyLayout = nullptr;

ID3D11Buffer* g_backgroundBuffer = nullptr;
ID3D11Buffer* g_blurBuffer = nullptr;
ID3D11SamplerState* g_sampler = nullptr;
ID3D11RasterizerState* g_rasterizer = nullptr;

Target g_background;
Target g_blurA;
Target g_blurB;

bool g_shadersReady = false;

typedef HRESULT(WINAPI* D3DCompileFn)(LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO*,
                                      ID3DInclude*, LPCSTR, LPCSTR, UINT, UINT,
                                      ID3DBlob**, ID3DBlob**);

HMODULE g_compilerModule = nullptr;

void LogFailure(const char* stage, const char* message) {
    std::string text = "[VcDex] backdrop ";
    text += stage;
    if (message && *message) {
        text += ": ";
        text += message;
    }
    text += "\n";
    OutputDebugStringA(text.c_str());
}

bool Compile(const char* source, const char* entry, const char* target,
             ID3DBlob** blob) {
    if (!g_compilerModule || !blob) return false;

    auto compile = reinterpret_cast<D3DCompileFn>(
        GetProcAddress(g_compilerModule, "D3DCompile"));
    if (!compile) return false;

    ID3DBlob* errors = nullptr;
    const HRESULT hr = compile(source, std::strlen(source), nullptr, nullptr, nullptr,
                               entry, target, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0,
                               blob, &errors);
    if (FAILED(hr)) {
        if (errors) {
            LogFailure(entry,
                       static_cast<const char*>(errors->GetBufferPointer()));
            errors->Release();
        }
        if (*blob) {
            (*blob)->Release();
            *blob = nullptr;
        }
        return false;
    }

    if (errors) errors->Release();
    return true;
}

void ConvertColor(ImU32 color, float out[4]) {
    out[0] = static_cast<float>((color >> IM_COL32_R_SHIFT) & 0xFFu) / 255.0f;
    out[1] = static_cast<float>((color >> IM_COL32_G_SHIFT) & 0xFFu) / 255.0f;
    out[2] = static_cast<float>((color >> IM_COL32_B_SHIFT) & 0xFFu) / 255.0f;
    out[3] = static_cast<float>((color >> IM_COL32_A_SHIFT) & 0xFFu) / 255.0f;
}

void ReleaseTarget(Target& target) {
    if (target.srv) { target.srv->Release(); target.srv = nullptr; }
    if (target.rtv) { target.rtv->Release(); target.rtv = nullptr; }
    if (target.texture) { target.texture->Release(); target.texture = nullptr; }
    target.width = 0;
    target.height = 0;
}

bool CreateTarget(Target& target, int width, int height) {
    ReleaseTarget(target);

    width = std::max(1, width);
    height = std::max(1, height);

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = static_cast<UINT>(width);
    desc.Height = static_cast<UINT>(height);
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    desc.CPUAccessFlags = 0;

    if (FAILED(g_device->CreateTexture2D(&desc, nullptr, &target.texture)) ||
        !target.texture) {
        target.texture = nullptr;
        return false;
    }

    if (FAILED(g_device->CreateRenderTargetView(target.texture, nullptr,
                                                &target.rtv)) ||
        !target.rtv) {
        ReleaseTarget(target);
        return false;
    }

    if (FAILED(g_device->CreateShaderResourceView(target.texture, nullptr,
                                                  &target.srv)) ||
        !target.srv) {
        ReleaseTarget(target);
        return false;
    }

    target.width = width;
    target.height = height;
    return true;
}

bool CreateShaders() {
    g_compilerModule = LoadLibraryW(L"d3dcompiler_47.dll");
    if (!g_compilerModule) {
        LogFailure("compiler", "d3dcompiler_47.dll not available");
        return false;
    }

    ID3DBlob* vsBlob = nullptr;
    ID3DBlob* backgroundBlob = nullptr;
    ID3DBlob* blurBlob = nullptr;

    const bool compiled =
        Compile(kShaderSource, "VSMain", "vs_4_0", &vsBlob) &&
        Compile(kShaderSource, "PSMain", "ps_4_0", &backgroundBlob) &&
        Compile(kBlurSource, "PSMain", "ps_4_0", &blurBlob);

    if (!compiled) {
        if (vsBlob) vsBlob->Release();
        if (backgroundBlob) backgroundBlob->Release();
        if (blurBlob) blurBlob->Release();
        return false;
    }

    const HRESULT vsHr = g_device->CreateVertexShader(
        vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &g_vertexShader);
    const HRESULT bgHr = g_device->CreatePixelShader(
        backgroundBlob->GetBufferPointer(), backgroundBlob->GetBufferSize(), nullptr,
        &g_backgroundShader);
    const HRESULT blurHr = g_device->CreatePixelShader(
        blurBlob->GetBufferPointer(), blurBlob->GetBufferSize(), nullptr, &g_blurShader);

    vsBlob->Release();
    backgroundBlob->Release();
    blurBlob->Release();

    if (FAILED(vsHr) || FAILED(bgHr) || FAILED(blurHr)) {
        LogFailure("pipeline", "shader creation failed");
        return false;
    }
    return true;
}

bool CreateStates() {
    D3D11_BUFFER_DESC bufferDesc{};
    bufferDesc.Usage = D3D11_USAGE_DYNAMIC;
    bufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    bufferDesc.ByteWidth = sizeof(BgParams);

    if (FAILED(g_device->CreateBuffer(&bufferDesc, nullptr, &g_backgroundBuffer)))
        return false;

    bufferDesc.ByteWidth = sizeof(BlurParams);
    if (FAILED(g_device->CreateBuffer(&bufferDesc, nullptr, &g_blurBuffer)))
        return false;

    D3D11_SAMPLER_DESC samplerDesc{};
    samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;

    if (FAILED(g_device->CreateSamplerState(&samplerDesc, &g_sampler)))
        return false;

    D3D11_RASTERIZER_DESC rasterDesc{};
    rasterDesc.FillMode = D3D11_FILL_SOLID;
    rasterDesc.CullMode = D3D11_CULL_NONE;
    rasterDesc.DepthClipEnable = TRUE;

    if (FAILED(g_device->CreateRasterizerState(&rasterDesc, &g_rasterizer)))
        return false;

    return true;
}

void DrawFullscreen() {
    g_context->Draw(3, 0);
}

bool RenderPass(Target& destination, ID3D11PixelShader* shader,
                ID3D11ShaderResourceView* source, const void* constants,
                std::size_t constantSize, ID3D11Buffer* buffer,
                float dirX, float dirY, int texelWidth, int texelHeight) {
    if (!destination.rtv) return false;

    ID3D11RenderTargetView* rtv = destination.rtv;
    g_context->OMSetRenderTargets(1, &rtv, nullptr);

    D3D11_VIEWPORT viewport{};
    viewport.Width = static_cast<float>(destination.width);
    viewport.Height = static_cast<float>(destination.height);
    viewport.MaxDepth = 1.0f;
    g_context->RSSetViewports(1, &viewport);
    g_context->RSSetState(g_rasterizer);

    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(g_context->Map(buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
        return false;
    if (constants && constantSize > 0)
        std::memcpy(mapped.pData, constants, constantSize);
    g_context->Unmap(buffer, 0);

    g_context->VSSetShader(g_vertexShader, nullptr, 0);
    g_context->PSSetShader(shader, nullptr, 0);
    g_context->PSSetConstantBuffers(0, 1, &buffer);
    g_context->IASetInputLayout(nullptr);
    g_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    g_context->GSSetShader(nullptr, nullptr, 0);
    g_context->OMSetBlendState(nullptr, nullptr, 0xffffffffu);
    g_context->OMSetDepthStencilState(nullptr, 0);

    if (source) {
        g_context->PSSetShaderResources(0, 1, &source);
        g_context->PSSetSamplers(0, 1, &g_sampler);
    }

    (void)dirX;
    (void)dirY;
    (void)texelWidth;
    (void)texelHeight;

    DrawFullscreen();
    return true;
}

} // namespace

bool Init(ID3D11Device* device, ID3D11DeviceContext* context) {
    g_device = device;
    g_context = context;
    if (!g_device || !g_context) return false;

#ifdef VCDEX_DISABLE_BACKDROP
    return false;
#endif

    g_shadersReady = CreateShaders() && CreateStates();
    return g_shadersReady;
}

void Shutdown() {
    ReleaseTarget(g_background);
    ReleaseTarget(g_blurA);
    ReleaseTarget(g_blurB);

    if (g_rasterizer) { g_rasterizer->Release(); g_rasterizer = nullptr; }
    if (g_sampler) { g_sampler->Release(); g_sampler = nullptr; }
    if (g_blurBuffer) { g_blurBuffer->Release(); g_blurBuffer = nullptr; }
    if (g_backgroundBuffer) { g_backgroundBuffer->Release(); g_backgroundBuffer = nullptr; }
    if (g_dummyLayout) { g_dummyLayout->Release(); g_dummyLayout = nullptr; }
    if (g_blurShader) { g_blurShader->Release(); g_blurShader = nullptr; }
    if (g_backgroundShader) { g_backgroundShader->Release(); g_backgroundShader = nullptr; }
    if (g_vertexShader) { g_vertexShader->Release(); g_vertexShader = nullptr; }

    if (g_compilerModule) {
        FreeLibrary(g_compilerModule);
        g_compilerModule = nullptr;
    }

    g_shadersReady = false;
    g_device = nullptr;
    g_context = nullptr;
}

void Resize(int width, int height) {
    if (!g_shadersReady || !g_device) return;

    const int backgroundW = std::max(1, width / 2);
    const int backgroundH = std::max(1, height / 2);
    const int blurW = std::max(1, width / 4);
    const int blurH = std::max(1, height / 4);

    if (g_background.width != backgroundW || g_background.height != backgroundH)
        CreateTarget(g_background, backgroundW, backgroundH);

    if (g_blurA.width != blurW || g_blurA.height != blurH)
        CreateTarget(g_blurA, blurW, blurH);

    if (g_blurB.width != blurW || g_blurB.height != blurH)
        CreateTarget(g_blurB, blurW, blurH);
}

bool Available() {
    return g_shadersReady && g_background.srv && g_background.rtv && g_blurA.srv &&
           g_blurB.srv;
}

void Render(const Params& params, const theme::Colors& colors) {
    if (!Available()) return;

    const float time = params.reduceMotion ? params.time * 0.18f : params.time;

    BgParams background{};
    background.p0[0] = static_cast<float>(g_background.width);
    background.p0[1] = static_cast<float>(g_background.height);
    background.p0[2] = time;
    background.p0[3] = std::clamp(params.intensity, 0.0f, 1.0f);
    background.p1[0] = std::clamp(params.mouse.x, 0.0f, 1.0f);
    background.p1[1] = std::clamp(params.mouse.y, 0.0f, 1.0f);
    background.p1[2] = std::clamp(params.caustic, 0.0f, 1.0f);
    background.p1[3] = std::clamp(params.grain, 0.0f, 0.12f);

    ConvertColor(colors.bg0, background.cBg0);
    ConvertColor(colors.bg1, background.cBg1);
    ConvertColor(colors.bg2, background.cBg2);
    ConvertColor(colors.blob0, background.cBlob0);
    ConvertColor(colors.blob1, background.cBlob1);
    ConvertColor(colors.blob2, background.cBlob2);
    ConvertColor(colors.primary, background.cPrimary);

    background.pRipple[0] = params.ripple.x;
    background.pRipple[1] = params.ripple.y;
    background.pRipple[2] = std::max(0.0f, params.rippleAge);
    background.pRipple[3] = params.rippleAge >= 0.0f && params.rippleAge < 1.2f ? 1.0f : 0.0f;

    // Background pass.
    RenderPass(g_background, g_backgroundShader, nullptr, &background,
               sizeof(background), g_backgroundBuffer, 0.0f, 0.0f, 0, 0);

    // Separable blur passes. Two iterations when the user prefers high quality.
    const int iterations = params.highQuality ? 2 : 1;
    ID3D11ShaderResourceView* input = g_background.srv;

    for (int i = 0; i < iterations; ++i) {
        BlurParams horizontal{};
        horizontal.p0[0] = 1.0f / static_cast<float>(g_blurA.width);
        horizontal.p0[1] = 1.0f / static_cast<float>(g_blurA.height);
        horizontal.p0[2] = 1.0f;
        horizontal.p0[3] = 0.0f;

        RenderPass(g_blurA, g_blurShader, input, &horizontal, sizeof(horizontal),
                   g_blurBuffer, 1.0f, 0.0f, g_blurA.width, g_blurA.height);

        BlurParams vertical{};
        vertical.p0[0] = 1.0f / static_cast<float>(g_blurB.width);
        vertical.p0[1] = 1.0f / static_cast<float>(g_blurB.height);
        vertical.p0[2] = 0.0f;
        vertical.p0[3] = 1.0f;

        RenderPass(g_blurB, g_blurShader, g_blurA.srv, &vertical, sizeof(vertical),
                   g_blurBuffer, 0.0f, 1.0f, g_blurB.width, g_blurB.height);

        input = g_blurB.srv;
    }

    // Unbind everything so ImGui's own pipeline starts from a clean slate.
    ID3D11RenderTargetView* nullTarget = nullptr;
    g_context->OMSetRenderTargets(1, &nullTarget, nullptr);
    ID3D11ShaderResourceView* nullView = nullptr;
    g_context->PSSetShaderResources(0, 1, &nullView);
    ID3D11SamplerState* nullSampler = nullptr;
    g_context->PSSetSamplers(0, 1, &nullSampler);
}

ID3D11ShaderResourceView* BackgroundSrv() {
    return g_background.srv;
}

ID3D11ShaderResourceView* BlurSrv() {
    return g_blurB.srv;
}

} // namespace backdrop
