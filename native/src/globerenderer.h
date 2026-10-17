#pragma once

#include "overlayrenderer.h"

#include <QColor>
#include <QtQuick/QQuickRhiItem>
#include <rhi/qrhi.h>

#include <memory>
#include <vector>

// Render-thread side of GlobeItem: the hex grid, plus the shared OverlayRenderer for
// pins, markers, satellites and the intro lines. Scene state is copied from the item
// in synchronize() while the GUI thread is blocked.
class GlobeRenderer : public QQuickRhiItemRenderer
{
public:
    void initialize(QRhiCommandBuffer *cb) override;
    void synchronize(QQuickRhiItem *item) override;
    void render(QRhiCommandBuffer *cb) override;

private:
    struct Tile {
        float lat;
        float lon;
        int count;
        float b[6][3];
    };

    struct State {
        QColor baseColor;
        double elapsedMs = 0.0;
        double globeScale = 1.0;
        double viewAngle = 0.0;
        int dayLength = 28000;
        int introDuration = 2000;
        double introAltitude = 1.1;
        QColor introLinesColor;
        int introLinesCount = 60;
        double highlightLat = 0.0;
        double highlightLon = 0.0;
        bool highlightActive = false;
        float devicePixelRatio = 1.0f;
        OverlayScene scene;
    };

    static std::vector<Tile> loadTiles();

    State m_state;

    QRhi *m_rhi = nullptr;
    int m_sampleCount = 0;
    QRhiTexture::Format m_textureFormat = QRhiTexture::RGBA8;

    std::vector<Tile> m_tiles;
    QColor m_builtColor;
    int m_gridVertexCount = 0;
    std::unique_ptr<QRhiBuffer> m_gridBuffer;
    std::unique_ptr<QRhiGraphicsPipeline> m_gridPipeline;

    encom::OverlayRenderer m_overlays;
};
