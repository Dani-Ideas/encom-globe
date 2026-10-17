#pragma once

#include "overlayrenderer.h"

#include <QColor>
#include <QMatrix4x4>
#include <QVector2D>
#include <QtQuick/QQuickRhiItem>
#include <rhi/qrhi.h>

#include <memory>
#include <vector>

// Render-thread side of ParticleGlobeItem. Land particles and atmosphere particles
// share one shader (told apart by `layer`) and one pipeline layout, each with its own
// uniform buffer; the atmosphere shell is an indexed sphere drawn last, additively.
// Pins, markers and satellites go through the shared OverlayRenderer with the
// particles' rotation as their model matrix.
class ParticleRenderer : public QQuickRhiItemRenderer
{
public:
    void initialize(QRhiCommandBuffer *cb) override;
    void synchronize(QQuickRhiItem *item) override;
    void render(QRhiCommandBuffer *cb) override;

private:
    struct State {
        QColor baseColor;
        QColor atmosphereColor;
        double elapsedMs = 0.0;
        double globeScale = 1.0;
        float pointSize = 2.f;
        float turbulence = 4.f;
        float hoverRadius = 200.f;
        float hoverPull = 0.6f;
        float hoverTurbulence = 60.f;
        bool atmosphere = true;
        float atmosphereScale = 1.12f;
        float atmosphereOpacity = 0.f;
        float atmosphereRimPower = 3.f;
        int atmosphereParticles = 2500;
        float atmospherePointSize = 1.2f;
        float atmosphereParticleOpacity = 0.5f;
        float globe = 0.f;
        float hover = 0.f;
        QVector2D mouse;
        QMatrix4x4 rotation;
        double yaw = 0.0;
        int introDuration = 2000;
        float introAltitude = 1.1f;
        QColor introLinesColor;
        int introLinesCount = 60;
        float devicePixelRatio = 1.f;
        OverlayScene scene;
    };

    void releaseResources();
    void buildLand(QRhiResourceUpdateBatch *u);
    void buildAtmosphere(QRhiResourceUpdateBatch *u);
    void buildShell(QRhiResourceUpdateBatch *u);

    State m_state;

    QRhi *m_rhi = nullptr;
    int m_sampleCount = 0;
    QRhiTexture::Format m_textureFormat = QRhiTexture::RGBA8;

    // Tile centres and real (sign-corrected) lat/lon from grid-particles.bin.
    std::vector<float> m_tiles; // x y z lat lon per tile

    QColor m_builtColor;
    int m_landCount = 0;
    int m_builtAtmosphereCount = -1;
    float m_builtAtmosphereScale = 0.f;
    int m_atmosphereCount = 0;
    int m_shellIndexCount = 0;

    std::unique_ptr<QRhiBuffer> m_landUniforms;
    std::unique_ptr<QRhiBuffer> m_atmosphereUniforms;
    std::unique_ptr<QRhiShaderResourceBindings> m_landSrb;
    std::unique_ptr<QRhiShaderResourceBindings> m_atmosphereSrb;
    std::unique_ptr<QRhiBuffer> m_landBuffer;
    std::unique_ptr<QRhiBuffer> m_atmosphereBuffer;
    std::unique_ptr<QRhiBuffer> m_shellVertices;
    std::unique_ptr<QRhiBuffer> m_shellIndices;

    std::unique_ptr<QRhiGraphicsPipeline> m_landPipeline;
    std::unique_ptr<QRhiGraphicsPipeline> m_atmospherePipeline;
    std::unique_ptr<QRhiGraphicsPipeline> m_shellPipeline;

    encom::OverlayRenderer m_overlays;
};
