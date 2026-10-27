#pragma once
// Thin helpers over raylib/rlgl for the post-processing pipeline: HDR render targets,
// explicit-UV quads, blend-mode scopes and a shader wrapper with cached uniform locations.

#include <string>
#include <unordered_map>

#include "core/Math.hpp"
#include "raylib.h"

namespace pm::gpu {

/// Creates a render target. `hdr` requests RGBA16F (falls back to RGBA8 if unsupported).
RenderTexture2D createTarget(int width, int height, bool hdr, bool linearFilter);
void destroyTarget(RenderTexture2D& target);

/// Draws `tex` into the rectangle (x, y, w, h) with explicit texture coordinates
/// (u0, v0) at the top-left corner and (u1, v1) at the bottom-right corner.
void drawQuad(const Texture2D& tex, float x, float y, float w, float h, float u0, float v0, float u1, float v1);

/// Full-target copy/pass helper: draws `src` stretched over a `w` x `h` render target with
/// render-texture orientation preserved.
void drawFullTarget(const Texture2D& src, int w, int h);

/// Foreground blends colour like Alpha but drives the target's alpha towards 0 under opaque
/// pixels, marking them for the composite as figures in front of the back wall.
enum class Blend { Alpha, Replace, Additive, AddColorKeepAlpha, Foreground };

/// RAII scope that switches the blend mode and restores alpha blending afterwards.
class BlendScope {
public:
    explicit BlendScope(Blend mode);
    ~BlendScope();
    BlendScope(const BlendScope&) = delete;
    BlendScope& operator=(const BlendScope&) = delete;
};

class ShaderProgram {
public:
    ShaderProgram() = default;
    ~ShaderProgram();
    ShaderProgram(const ShaderProgram&) = delete;
    ShaderProgram& operator=(const ShaderProgram&) = delete;

    /// Compiles a fragment shader against raylib's default vertex shader.
    bool load(const char* fragmentSource, const char* debugName);
    void unload();
    bool valid() const { return shader_.id != 0; }
    const Shader& raw() const { return shader_; }

    int location(const char* name);
    void set(const char* name, float v);
    void set(const char* name, int v);
    void set(const char* name, Vec2 v);
    void set(const char* name, float x, float y, float z);
    void setVec4Array(const char* name, const float* data, int count);
    void setTexture(const char* name, const Texture2D& tex);

private:
    Shader shader_{};
    std::unordered_map<std::string, int> locations_;
};

} // namespace pm::gpu
