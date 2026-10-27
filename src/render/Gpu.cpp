#include "render/Gpu.hpp"

#include "rlgl.h"

namespace pm::gpu {

RenderTexture2D createTarget(int width, int height, bool hdr, bool linearFilter) {
    RenderTexture2D rt{};
    if (hdr) {
        rt.id = rlLoadFramebuffer();
        if (rt.id > 0) {
            rlEnableFramebuffer(rt.id);
            rt.texture.id = rlLoadTexture(nullptr, width, height, PIXELFORMAT_UNCOMPRESSED_R16G16B16A16, 1);
            rt.texture.width = width;
            rt.texture.height = height;
            rt.texture.format = PIXELFORMAT_UNCOMPRESSED_R16G16B16A16;
            rt.texture.mipmaps = 1;
            rlFramebufferAttach(rt.id, rt.texture.id, RL_ATTACHMENT_COLOR_CHANNEL0, RL_ATTACHMENT_TEXTURE2D, 0);
            bool ok = rt.texture.id > 0 && rlFramebufferComplete(rt.id);
            rlDisableFramebuffer();
            if (!ok) {
                UnloadRenderTexture(rt);
                rt = RenderTexture2D{};
            }
        }
    }
    if (rt.id == 0) rt = LoadRenderTexture(width, height);
    SetTextureFilter(rt.texture, linearFilter ? TEXTURE_FILTER_BILINEAR : TEXTURE_FILTER_POINT);
    SetTextureWrap(rt.texture, TEXTURE_WRAP_CLAMP);
    return rt;
}

void destroyTarget(RenderTexture2D& target) {
    if (target.id > 0) UnloadRenderTexture(target);
    target = RenderTexture2D{};
}

void drawQuad(const Texture2D& tex, float x, float y, float w, float h, float u0, float v0, float u1, float v1) {
    rlSetTexture(tex.id);
    rlBegin(RL_QUADS);
    rlColor4ub(255, 255, 255, 255);
    rlNormal3f(0.0f, 0.0f, 1.0f);
    rlTexCoord2f(u0, v0);
    rlVertex2f(x, y);
    rlTexCoord2f(u0, v1);
    rlVertex2f(x, y + h);
    rlTexCoord2f(u1, v1);
    rlVertex2f(x + w, y + h);
    rlTexCoord2f(u1, v0);
    rlVertex2f(x + w, y);
    rlEnd();
    rlSetTexture(0);
}

void drawFullTarget(const Texture2D& src, int w, int h) {
    // Render textures store row 0 at the bottom; drawing with v flipped keeps every layer
    // in the same "y down" orientation.
    drawQuad(src, 0.0f, 0.0f, static_cast<float>(w), static_cast<float>(h), 0.0f, 1.0f, 1.0f, 0.0f);
}

BlendScope::BlendScope(Blend mode) {
    switch (mode) {
    case Blend::Alpha: BeginBlendMode(BLEND_ALPHA); break;
    case Blend::Additive: BeginBlendMode(BLEND_ADDITIVE); break;
    case Blend::Replace:
        rlSetBlendFactors(RL_ONE, RL_ZERO, RL_FUNC_ADD);
        BeginBlendMode(BLEND_CUSTOM);
        break;
    case Blend::AddColorKeepAlpha:
        rlSetBlendFactorsSeparate(RL_SRC_ALPHA, RL_ONE, RL_ZERO, RL_ONE, RL_FUNC_ADD, RL_FUNC_ADD);
        BeginBlendMode(BLEND_CUSTOM_SEPARATE);
        break;
    case Blend::Foreground:
        rlSetBlendFactorsSeparate(RL_SRC_ALPHA, RL_ONE_MINUS_SRC_ALPHA, RL_ZERO, RL_ONE_MINUS_SRC_ALPHA, RL_FUNC_ADD, RL_FUNC_ADD);
        BeginBlendMode(BLEND_CUSTOM_SEPARATE);
        break;
    }
}

BlendScope::~BlendScope() { EndBlendMode(); }

ShaderProgram::~ShaderProgram() { unload(); }

bool ShaderProgram::load(const char* fragmentSource, const char* debugName) {
    unload();
    shader_ = LoadShaderFromMemory(nullptr, fragmentSource);
    if (!IsShaderValid(shader_) || shader_.id == rlGetShaderIdDefault()) {
        TraceLog(LOG_ERROR, "Shader '%s' failed to compile", debugName);
        // A link failure hands back raylib's default program, which must stay loaded, with a
        // location table allocated just for this copy: free only the table.
        if (shader_.id == rlGetShaderIdDefault()) MemFree(shader_.locs);
        shader_ = Shader{};
        return false;
    }
    return true;
}

void ShaderProgram::unload() {
    if (shader_.id != 0 && IsWindowReady()) UnloadShader(shader_);
    shader_ = Shader{};
    locations_.clear();
}

int ShaderProgram::location(const char* name) {
    auto it = locations_.find(name);
    if (it != locations_.end()) return it->second;
    int loc = GetShaderLocation(shader_, name);
    locations_.emplace(name, loc);
    return loc;
}

void ShaderProgram::set(const char* name, float v) {
    SetShaderValue(shader_, location(name), &v, SHADER_UNIFORM_FLOAT);
}

void ShaderProgram::set(const char* name, int v) { SetShaderValue(shader_, location(name), &v, SHADER_UNIFORM_INT); }

void ShaderProgram::set(const char* name, Vec2 v) {
    float d[2] = {v.x, v.y};
    SetShaderValue(shader_, location(name), d, SHADER_UNIFORM_VEC2);
}

void ShaderProgram::set(const char* name, float x, float y, float z) {
    float d[3] = {x, y, z};
    SetShaderValue(shader_, location(name), d, SHADER_UNIFORM_VEC3);
}

void ShaderProgram::setVec4Array(const char* name, const float* data, int count) {
    if (count <= 0) return;
    SetShaderValueV(shader_, location(name), data, SHADER_UNIFORM_VEC4, count);
}

void ShaderProgram::setTexture(const char* name, const Texture2D& tex) {
    SetShaderValueTexture(shader_, location(name), tex);
}

} // namespace pm::gpu
