#pragma once

#include "overlayscene.h"

#include <QColor>
#include <QMatrix4x4>
#include <QSize>
#include <rhi/qrhi.h>

#include <memory>
#include <unordered_map>
#include <vector>

namespace encom {

// Mirrors the uniform block in shaders/hex.*, overlay.* and smoke.* (std140).
struct GlobeUniforms {
    float mvp[16];
    float modelView[16];
    float currentTime;
    float introDuration;
    float introAltitude;
    float fogNear;
    float fogFar;
    float highlightLat;
    float highlightLon;
    float highlightActive;
    float pulse;
    float pointScale;
    float pad[2];
    float smokeColor[4];
};
static_assert(sizeof(GlobeUniforms) == 192, "GlobeUniforms must match the std140 block");

QShader loadShader(const QString &name);
// Interleaved float attributes at consecutive locations, e.g. { 3, 2, 4 }.
QRhiVertexInputLayout makeLayout(std::initializer_list<int> components);

struct PipelineOptions {
    QRhiGraphicsPipeline::Topology topology = QRhiGraphicsPipeline::Triangles;
    bool depthTest = true;
    bool depthWrite = true;
    bool additive = false;  // THREE.AdditiveBlending instead of NormalBlending
    bool cullBack = false;  // default is DoubleSide
};
std::unique_ptr<QRhiGraphicsPipeline> createPipeline(QRhi *rhi, QRhiRenderTarget *target, const QString &shader,
                                                     const QRhiVertexInputLayout &layout,
                                                     QRhiShaderResourceBindings *layoutSrb,
                                                     const PipelineOptions &options);

// One draw in the transparent pass. The buffer is held by pointer-to-owner because
// dynamic buffers can be reallocated after the draw was recorded.
struct Draw {
    QRhiGraphicsPipeline *pipeline;
    QRhiShaderResourceBindings *srb;
    std::unique_ptr<QRhiBuffer> *buffer;
    quint32 first;
    quint32 count;
    float depth;   // view-space distance used for sorting
    quint64 order; // creation order for ties
};
// Back to front, ties in creation order (three.js reversePainterSortStable).
void drawSorted(QRhiCommandBuffer *cb, const QRhiViewport &viewport, std::vector<Draw> &draws);

// Creation-order slots for objects at the globe's origin; overlay objects sort after
// these by id (OverlayScene ids start at 1, scaled by 4 and offset past them).
enum OriginOrder : quint64 {
    OrderHostFirst = 0,  // ParticleGlobe land particles
    OrderHostSecond = 1, // ParticleGlobe atmosphere particles
    OrderIntroLines = 2,
    OrderSmoke = 3,
    OrderGrid = 4,
};

// Everything one frame of overlays needs from the hosting renderer.
struct OverlayFrame {
    QMatrix4x4 projection; // includes the clip-space correction
    QMatrix4x4 modelView;  // globe frame -> view space
    QSize pixelSize;
    double timeMs = 0.0;
    double cameraAngle = 0.0; // hex-globe convention, for the satellites' tilt
    float fogNear = 0.f;
    float fogFar = 0.f;
    float devicePixelRatio = 1.f;
    QColor baseColor;         // the IP pin pulses toward it
    // Intro lines are only drawn by the hex globe.
    bool introLines = false;
    int introDuration = 0;
    double introAltitude = 1.1;
    QColor introLinesColor;
    int introLinesCount = 0;
};

// Draws OverlayScene (pin lines/tops/labels/smoke, marker arcs/rings/labels,
// satellites, intro lines) for either globe. Geometry is built on the CPU each frame
// in the globe's own frame; the model-view matrix places it.
class OverlayRenderer
{
public:
    void reset();               // the QRhi changed: drop everything
    void invalidatePipelines(); // sample count or color format changed

    // Uploads uniforms, textures and geometry through `u` and appends transparent
    // draws. `uniforms` arrives with the host's fields set (grid intro/highlight);
    // the matrices, fog, point scale and smoke color are filled in here.
    void prepare(QRhi *rhi, QRhiRenderTarget *target, QRhiResourceUpdateBatch *u, const OverlayScene &scene,
                 const OverlayFrame &frame, GlobeUniforms uniforms, std::vector<Draw> &transparent);
    // Pin lines are opaque in three.js: draw them before the transparent pass.
    void drawOpaque(QRhiCommandBuffer *cb, const QRhiViewport &viewport);

    // The uniform buffer above, for the hex grid's pipeline and draws.
    QRhiShaderResourceBindings *uniformSrb() const { return m_uniformSrb.get(); }

private:
    struct TextureEntry {
        std::unique_ptr<QRhiTexture> texture;
        std::unique_ptr<QRhiShaderResourceBindings> srb;
    };

    void ensureResources(QRhiRenderTarget *target, QRhiResourceUpdateBatch *u);
    TextureEntry createTexture(const QImage &image, QRhiResourceUpdateBatch *u);
    void updateTextureCache(const OverlayScene &scene, QRhiResourceUpdateBatch *u);
    QRhiShaderResourceBindings *texturedSrb(const QImage &image) const;

    QRhi *m_rhi = nullptr;
    int m_sampleCount = 0;
    std::unique_ptr<QRhiBuffer> m_uniformBuffer;
    std::unique_ptr<QRhiSampler> m_sampler;
    std::unique_ptr<QRhiShaderResourceBindings> m_uniformSrb; // smoke (and the host's grid)
    TextureEntry m_whiteTexture;                              // lines
    TextureEntry m_dotTexture;                                // pin tops
    // Labels, marker sprites and satellite sheets, keyed by QImage::cacheKey().
    std::unordered_map<qint64, TextureEntry> m_imageTextures;

    std::unique_ptr<QRhiBuffer> m_overlayBuffer;
    std::unique_ptr<QRhiBuffer> m_smokeBuffer;
    quint32 m_lineVertexCount = 0;

    std::unique_ptr<QRhiGraphicsPipeline> m_linePipeline;
    std::unique_ptr<QRhiGraphicsPipeline> m_spritePipeline;
    std::unique_ptr<QRhiGraphicsPipeline> m_satellitePipeline;
    std::unique_ptr<QRhiGraphicsPipeline> m_smokePipeline;

    // Globe.js createIntroLines: generated once, shown until introDuration.
    std::vector<std::vector<QVector3D>> m_introLines;
};

} // namespace encom
