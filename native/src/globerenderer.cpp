#include "globerenderer.h"

#include "globeitem.h"

#include <QFile>
#include <QMatrix4x4>
#include <QQuickWindow>
#include <QRandomGenerator>
#include <QVector4D>

#include <algorithm>
#include <cmath>
#include <cstring>

using namespace encom;

namespace {

constexpr int GridFloats = 8; // position(3) color(3) lat lng

float hueToRgb(float p, float q, float t)
{
    if (t < 0.f) t += 1.f;
    if (t > 1.f) t -= 1.f;
    if (t < 1.f / 6.f) return p + (q - p) * 6.f * t;
    if (t < 1.f / 2.f) return q;
    if (t < 2.f / 3.f) return p + (q - p) * 6.f * (2.f / 3.f - t);
    return p;
}

// Same as THREE.Color.setHSL with color management on: HSL is in sRGB, the stored
// value is linear. The web version's RawShaderMaterial outputs that linear value
// as-is, so we do the same to get identical colors.
QVector3D hslToLinear(float h, float s, float l)
{
    if (s <= 0.f)
        return QVector3D(srgbToLinear(l), srgbToLinear(l), srgbToLinear(l));
    const float q = l <= 0.5f ? l * (1.f + s) : l + s - l * s;
    const float p = 2.f * l - q;
    return QVector3D(srgbToLinear(hueToRgb(p, q, h + 1.f / 3.f)),
                     srgbToLinear(hueToRgb(p, q, h)),
                     srgbToLinear(hueToRgb(p, q, h - 1.f / 3.f)));
}

} // namespace

void GlobeRenderer::initialize(QRhiCommandBuffer *)
{
    if (m_rhi != rhi()) {
        m_gridPipeline.reset();
        m_gridBuffer.reset();
        m_overlays.reset();
        m_rhi = rhi();
    }
    if (m_sampleCount != renderTarget()->sampleCount()) {
        m_sampleCount = renderTarget()->sampleCount();
        m_gridPipeline.reset();
    }
    QRhiTexture *finalTex = m_sampleCount > 1 ? resolveTexture() : colorTexture();
    if (m_textureFormat != finalTex->format()) {
        m_textureFormat = finalTex->format();
        m_gridPipeline.reset();
        m_overlays.invalidatePipelines();
    }
    if (m_tiles.empty())
        m_tiles = loadTiles();
}

void GlobeRenderer::synchronize(QQuickRhiItem *rhiItem)
{
    auto *item = static_cast<GlobeItem *>(rhiItem);
    m_state.baseColor = item->m_baseColor;
    m_state.elapsedMs = item->m_elapsedMs;
    m_state.globeScale = item->m_globeScale > 0 ? item->m_globeScale : 1.0;
    m_state.viewAngle = item->m_viewAngle;
    m_state.dayLength = std::max(1, item->m_dayLength);
    m_state.introDuration = item->m_introDuration;
    m_state.introAltitude = item->m_introAltitude;
    m_state.introLinesColor = item->m_introLinesColor;
    m_state.introLinesCount = item->m_introLinesCount;
    m_state.highlightLat = item->m_highlightLat;
    m_state.highlightLon = item->m_highlightLon;
    m_state.highlightActive = item->m_highlightActive;
    m_state.devicePixelRatio = item->window() ? float(item->window()->effectiveDevicePixelRatio()) : 1.0f;
    m_state.scene = item->m_scene;
}

std::vector<GlobeRenderer::Tile> GlobeRenderer::loadTiles()
{
    std::vector<Tile> tiles;
    QFile f(QStringLiteral(":/encomglobe/grid.bin"));
    if (!f.open(QIODevice::ReadOnly)) {
        qWarning("EncomGlobe: grid.bin not found in resources");
        return tiles;
    }
    const QByteArray data = f.readAll();
    constexpr qsizetype stride = 4 * (2 + 1 + 18);
    quint32 count = 0;
    if (data.size() >= 4)
        std::memcpy(&count, data.constData(), 4);
    if (data.size() < 4 + qsizetype(count) * stride) {
        qWarning("EncomGlobe: grid.bin is truncated");
        return tiles;
    }

    tiles.resize(count);
    const char *p = data.constData() + 4;
    for (auto &t : tiles) {
        quint32 n;
        std::memcpy(&t.lat, p, 4);
        std::memcpy(&t.lon, p + 4, 4);
        std::memcpy(&n, p + 8, 4);
        std::memcpy(t.b, p + 12, sizeof(t.b));
        t.count = int(n);
        p += stride;
    }
    return tiles;
}

void GlobeRenderer::render(QRhiCommandBuffer *cb)
{
    QRhiResourceUpdateBatch *u = m_rhi->nextResourceUpdateBatch();
    const double t = m_state.elapsedMs;

    // --- Camera orbit, as in Globe.prototype.tick (cameraAngle starts at PI) ---
    const QSize size = renderTarget()->pixelSize();
    const float distance = float(1700.0 / m_state.globeScale);
    const double cameraAngle = M_PI + 2.0 * M_PI * std::fmod(t / m_state.dayLength, 1.0);
    const double va = m_state.viewAngle;
    const QVector3D eye(float(distance * std::cos(cameraAngle) * std::cos(va)),
                        float(distance * std::sin(va)),
                        float(distance * std::sin(cameraAngle) * std::cos(va)));

    encom::OverlayFrame frame;
    frame.modelView.lookAt(eye, QVector3D(0, 0, 0), QVector3D(0, 1, 0));
    frame.projection = m_rhi->clipSpaceCorrMatrix();
    frame.projection.perspective(50.f, size.width() / float(std::max(1, size.height())), 1.f, distance + 300.f);
    frame.pixelSize = size;
    frame.timeMs = t;
    frame.cameraAngle = cameraAngle;
    frame.fogNear = distance;
    frame.fogFar = distance + 300.f;
    frame.devicePixelRatio = m_state.devicePixelRatio;
    frame.baseColor = m_state.baseColor;
    frame.introLines = true;
    frame.introDuration = m_state.introDuration;
    frame.introAltitude = m_state.introAltitude;
    frame.introLinesColor = m_state.introLinesColor;
    frame.introLinesCount = m_state.introLinesCount;

    // The hex grid's own uniforms; the overlay renderer adds the rest and uploads.
    encom::GlobeUniforms uniforms = {};
    // The intro only needs the first few seconds; clamping keeps float precision
    // from mattering, and the pulse phase is computed here in double for the same reason.
    uniforms.currentTime = float(std::min(t, 1.0e7));
    uniforms.introDuration = float(m_state.introDuration);
    uniforms.introAltitude = float(m_state.introAltitude);
    uniforms.highlightLat = float(m_state.highlightLat);
    uniforms.highlightLon = float(m_state.highlightLon);
    uniforms.highlightActive = m_state.highlightActive ? 1.f : 0.f;
    uniforms.pulse = float((1.0 + std::sin(t * 0.0015)) * 0.5);

    std::vector<encom::Draw> transparent;
    m_overlays.prepare(m_rhi, renderTarget(), u, m_state.scene, frame, uniforms, transparent);

    // --- Hex grid (rebuilt when the base color changes, like Globe.setBaseColor) ---
    if (!m_gridPipeline) {
        m_gridPipeline = encom::createPipeline(m_rhi, renderTarget(), QStringLiteral("hex"),
                                               encom::makeLayout({ 3, 3, 2 }), m_overlays.uniformSrb(), {});
    }
    if (!m_tiles.empty() && (!m_gridBuffer || m_builtColor != m_state.baseColor)) {
        float h, s, l;
        m_state.baseColor.toHsl().getHslF(&h, &s, &l);
        h = std::max(h, 0.f); // achromatic colors report -1

        auto *rng = QRandomGenerator::global();
        QVector3D palette[10];
        for (int i = 0; i < 10; ++i) {
            palette[i] = hslToLinear(std::fmod(h + (i - 5) * 0.02f + 1.f, 1.f), s,
                                     std::min(1.f, l + float(rng->generateDouble()) / 3.f));
        }

        std::vector<float> v;
        v.reserve(m_tiles.size() * 4 * 3 * GridFloats);
        for (const Tile &tile : m_tiles) {
            if (tile.count < 5)
                continue;
            const QVector3D &c = palette[rng->bounded(10)];
            auto vertex = [&](int i) {
                v.insert(v.end(), { tile.b[i][0], tile.b[i][1], tile.b[i][2],
                                    c.x(), c.y(), c.z(), tile.lat, tile.lon });
            };
            const int fan[][3] = { { 0, 1, 2 }, { 0, 2, 3 }, { 0, 3, 4 }, { 0, 5, 4 } };
            for (int k = 0; k < (tile.count > 5 ? 4 : 3); ++k) {
                vertex(fan[k][0]);
                vertex(fan[k][1]);
                vertex(fan[k][2]);
            }
        }

        m_gridBuffer.reset(m_rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer,
                                            quint32(v.size() * sizeof(float))));
        m_gridBuffer->create();
        u->uploadStaticBuffer(m_gridBuffer.get(), v.data());
        m_gridVertexCount = int(v.size() / GridFloats);
        m_builtColor = m_state.baseColor;
    }
    // In three.js the grid sits at the origin, created after the intro lines and smoke.
    if (m_gridVertexCount > 0) {
        transparent.push_back({ m_gridPipeline.get(), m_overlays.uniformSrb(), &m_gridBuffer, 0,
                                quint32(m_gridVertexCount), distance, encom::OrderGrid });
    }

    const QRhiViewport viewport(0, 0, size.width(), size.height());
    cb->beginPass(renderTarget(), Qt::black, { 1.0f, 0 }, u);
    m_overlays.drawOpaque(cb, viewport);
    encom::drawSorted(cb, viewport, transparent);
    cb->endPass();
}
