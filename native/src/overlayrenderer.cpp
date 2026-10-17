#include "overlayrenderer.h"

#include <QFile>
#include <QRandomGenerator>
#include <QVector4D>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace encom {

namespace {

constexpr int OverlayFloats = 9; // position(3) uv(2) color(4)
constexpr int SmokeFloats = 5;   // position(3) alpha size

// SmokeProvider.js
constexpr int SmokePerPin = 30;
constexpr double SmokePerSecond = 20.0;
constexpr double SmokeLifeMs = 1500.0;

void pushOverlayVertex(std::vector<float> &v, const QVector3D &p, float u, float tv, const QVector4D &c)
{
    v.insert(v.end(), { p.x(), p.y(), p.z(), u, tv, c.x(), c.y(), c.z(), c.w() });
}

// A quad centred on `center`, spanning ±halfX and ±halfY. UVs run top-down (v0 at +halfY).
void pushQuad(std::vector<float> &v, const QVector3D &center, const QVector3D &halfX,
              const QVector3D &halfY, float u0, float v0, float u1, float v1, const QVector4D &color)
{
    const QVector3D bl = center - halfX - halfY, br = center + halfX - halfY;
    const QVector3D tr = center + halfX + halfY, tl = center - halfX + halfY;
    pushOverlayVertex(v, bl, u0, v1, color);
    pushOverlayVertex(v, br, u1, v1, color);
    pushOverlayVertex(v, tr, u1, v0, color);
    pushOverlayVertex(v, bl, u0, v1, color);
    pushOverlayVertex(v, tr, u1, v0, color);
    pushOverlayVertex(v, tl, u0, v0, color);
}

void ensureBufferSize(QRhi *rhi, std::unique_ptr<QRhiBuffer> &buffer, quint32 size)
{
    if (buffer && buffer->size() >= size)
        return;
    buffer.reset(rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer, std::max(size * 2, 4096u)));
    buffer->create();
}

} // namespace

QShader loadShader(const QString &name)
{
    QFile f(name);
    if (!f.open(QIODevice::ReadOnly)) {
        qWarning("EncomGlobe: missing shader %s", qPrintable(name));
        return QShader();
    }
    return QShader::fromSerialized(f.readAll());
}

QRhiVertexInputLayout makeLayout(std::initializer_list<int> components)
{
    static const QRhiVertexInputAttribute::Format formats[] = {
        QRhiVertexInputAttribute::Float, QRhiVertexInputAttribute::Float2,
        QRhiVertexInputAttribute::Float3, QRhiVertexInputAttribute::Float4,
    };
    std::vector<QRhiVertexInputAttribute> attributes;
    quint32 offset = 0;
    int location = 0;
    for (int c : components) {
        attributes.emplace_back(0, location++, formats[c - 1], offset);
        offset += c * sizeof(float);
    }
    QRhiVertexInputLayout layout;
    layout.setBindings({ { offset } });
    layout.setAttributes(attributes.begin(), attributes.end());
    return layout;
}

std::unique_ptr<QRhiGraphicsPipeline> createPipeline(QRhi *rhi, QRhiRenderTarget *target, const QString &shader,
                                                     const QRhiVertexInputLayout &layout,
                                                     QRhiShaderResourceBindings *layoutSrb,
                                                     const PipelineOptions &options)
{
    std::unique_ptr<QRhiGraphicsPipeline> ps(rhi->newGraphicsPipeline());
    ps->setShaderStages({
        { QRhiShaderStage::Vertex, loadShader(QStringLiteral(":/encomglobe/%1.vert.qsb").arg(shader)) },
        { QRhiShaderStage::Fragment, loadShader(QStringLiteral(":/encomglobe/%1.frag.qsb").arg(shader)) },
    });
    ps->setVertexInputLayout(layout);
    ps->setTopology(options.topology);

    QRhiGraphicsPipeline::TargetBlend blend;
    blend.enable = true;
    blend.srcColor = QRhiGraphicsPipeline::SrcAlpha;
    blend.dstColor = options.additive ? QRhiGraphicsPipeline::One : QRhiGraphicsPipeline::OneMinusSrcAlpha;
    blend.srcAlpha = QRhiGraphicsPipeline::One;
    blend.dstAlpha = options.additive ? QRhiGraphicsPipeline::One : QRhiGraphicsPipeline::OneMinusSrcAlpha;
    ps->setTargetBlends({ blend });
    ps->setCullMode(options.cullBack ? QRhiGraphicsPipeline::Back : QRhiGraphicsPipeline::None);
    ps->setDepthTest(options.depthTest);
    ps->setDepthWrite(options.depthWrite);
    ps->setSampleCount(target->sampleCount());
    ps->setShaderResourceBindings(layoutSrb);
    ps->setRenderPassDescriptor(target->renderPassDescriptor());
    if (!ps->create())
        qWarning("EncomGlobe: failed to create the %s pipeline", qPrintable(shader));
    return ps;
}

void drawSorted(QRhiCommandBuffer *cb, const QRhiViewport &viewport, std::vector<Draw> &draws)
{
    std::stable_sort(draws.begin(), draws.end(), [](const Draw &a, const Draw &b) {
        if (a.depth != b.depth)
            return a.depth > b.depth;
        return a.order < b.order;
    });
    for (const Draw &d : draws) {
        if (!d.pipeline || !d.srb || !d.buffer || !*d.buffer || d.count == 0)
            continue;
        cb->setGraphicsPipeline(d.pipeline);
        cb->setViewport(viewport);
        cb->setShaderResources(d.srb);
        const QRhiCommandBuffer::VertexInput input(d.buffer->get(), 0);
        cb->setVertexInput(0, 1, &input);
        cb->draw(d.count, 1, d.first);
    }
}

void OverlayRenderer::reset()
{
    invalidatePipelines();
    m_imageTextures.clear();
    m_whiteTexture = {};
    m_dotTexture = {};
    m_uniformSrb.reset();
    m_sampler.reset();
    m_uniformBuffer.reset();
    m_overlayBuffer.reset();
    m_smokeBuffer.reset();
    m_rhi = nullptr;
}

void OverlayRenderer::invalidatePipelines()
{
    m_linePipeline.reset();
    m_spritePipeline.reset();
    m_satellitePipeline.reset();
    m_smokePipeline.reset();
}

OverlayRenderer::TextureEntry OverlayRenderer::createTexture(const QImage &image, QRhiResourceUpdateBatch *u)
{
    TextureEntry entry;
    entry.texture.reset(m_rhi->newTexture(QRhiTexture::RGBA8, image.size(), 1,
                                          QRhiTexture::MipMapped | QRhiTexture::UsedWithGenerateMips));
    entry.texture->create();
    u->uploadTexture(entry.texture.get(), image);
    u->generateMips(entry.texture.get());

    entry.srb.reset(m_rhi->newShaderResourceBindings());
    entry.srb->setBindings({
        QRhiShaderResourceBinding::uniformBuffer(
            0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage,
            m_uniformBuffer.get()),
        QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage,
                                                  entry.texture.get(), m_sampler.get()),
    });
    entry.srb->create();
    return entry;
}

void OverlayRenderer::ensureResources(QRhiRenderTarget *target, QRhiResourceUpdateBatch *u)
{
    if (!m_uniformBuffer) {
        m_uniformBuffer.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, sizeof(GlobeUniforms)));
        m_uniformBuffer->create();
        m_sampler.reset(m_rhi->newSampler(QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::Linear,
                                          QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge));
        m_sampler->create();

        m_uniformSrb.reset(m_rhi->newShaderResourceBindings());
        m_uniformSrb->setBindings({
            QRhiShaderResourceBinding::uniformBuffer(
                0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage,
                m_uniformBuffer.get()),
        });
        m_uniformSrb->create();

        QImage white(1, 1, QImage::Format_RGBA8888);
        white.fill(Qt::white);
        m_whiteTexture = createTexture(white, u);
        m_dotTexture = createTexture(createDotImage(), u);
    }

    if (!m_linePipeline) {
        const QRhiVertexInputLayout overlayLayout = makeLayout({ 3, 2, 4 });
        QRhiShaderResourceBindings *textured = m_whiteTexture.srb.get();
        PipelineOptions points;
        points.topology = QRhiGraphicsPipeline::Points;
        m_smokePipeline = createPipeline(m_rhi, target, QStringLiteral("smoke"), makeLayout({ 3, 2 }),
                                         m_uniformSrb.get(), points);
        PipelineOptions lines;
        lines.topology = QRhiGraphicsPipeline::Lines;
        m_linePipeline = createPipeline(m_rhi, target, QStringLiteral("overlay"), overlayLayout, textured, lines);
        m_spritePipeline = createPipeline(m_rhi, target, QStringLiteral("overlay"), overlayLayout, textured, {});
        // Satellites use depthTest: false in Satellite.js.
        PipelineOptions noDepth;
        noDepth.depthTest = false;
        noDepth.depthWrite = false;
        m_satellitePipeline = createPipeline(m_rhi, target, QStringLiteral("overlay"), overlayLayout, textured,
                                             noDepth);
    }
}

void OverlayRenderer::updateTextureCache(const OverlayScene &scene, QRhiResourceUpdateBatch *u)
{
    std::unordered_map<qint64, TextureEntry> next;
    auto keep = [&](const QImage &image) {
        if (image.isNull() || next.count(image.cacheKey()))
            return;
        auto it = m_imageTextures.find(image.cacheKey());
        next.emplace(image.cacheKey(),
                     it != m_imageTextures.end() ? std::move(it->second) : createTexture(image, u));
    };
    for (const PinState &pin : scene.pins)
        keep(pin.label);
    for (const MarkerState &marker : scene.markers) {
        keep(marker.label);
        keep(marker.sprite);
    }
    for (const SatelliteGroup &group : scene.satelliteGroups)
        keep(group.sheet);
    m_imageTextures = std::move(next);
}

QRhiShaderResourceBindings *OverlayRenderer::texturedSrb(const QImage &image) const
{
    if (image.isNull())
        return nullptr;
    auto it = m_imageTextures.find(image.cacheKey());
    return it != m_imageTextures.end() ? it->second.srb.get() : nullptr;
}

void OverlayRenderer::prepare(QRhi *rhi, QRhiRenderTarget *target, QRhiResourceUpdateBatch *u,
                              const OverlayScene &scene, const OverlayFrame &frame, GlobeUniforms uniforms,
                              std::vector<Draw> &transparent)
{
    if (m_rhi != rhi) {
        reset();
        m_rhi = rhi;
    }
    if (m_sampleCount != target->sampleCount()) {
        m_sampleCount = target->sampleCount();
        invalidatePipelines();
    }
    ensureResources(target, u);
    updateTextureCache(scene, u);

    const double t = frame.timeMs;
    const QMatrix4x4 &modelView = frame.modelView;
    const QMatrix4x4 mvp = frame.projection * modelView;
    // Billboards face the camera: its right/up axes expressed in the globe's frame.
    const QVector3D cameraRight(modelView(0, 0), modelView(0, 1), modelView(0, 2));
    const QVector3D cameraUp(modelView(1, 0), modelView(1, 1), modelView(1, 2));
    const QVector3D eye = modelView.inverted().map(QVector3D(0, 0, 0));
    auto viewDepth = [&](const QVector3D &p) { return -modelView.map(p).z(); };
    const float originDepth = viewDepth(QVector3D(0, 0, 0));

    std::memcpy(uniforms.mvp, mvp.constData(), sizeof(uniforms.mvp));
    std::memcpy(uniforms.modelView, modelView.constData(), sizeof(uniforms.modelView));
    uniforms.fogNear = frame.fogNear;
    uniforms.fogFar = frame.fogFar;
    // The web canvas renders at CSS resolution, so point sizes are CSS pixels.
    uniforms.pointScale = frame.devicePixelRatio;
    const float smokeGray = srgbToLinear(0xaa / 255.f);
    uniforms.smokeColor[0] = uniforms.smokeColor[1] = uniforms.smokeColor[2] = smokeGray;
    uniforms.smokeColor[3] = 1.f;
    u->updateDynamicBuffer(m_uniformBuffer.get(), 0, sizeof(GlobeUniforms), &uniforms);

    std::vector<float> overlay;
    std::vector<float> smoke;

    // The IP pin pulses from white to the palette color (Globe.tick).
    const float ipMix = float((1.0 + std::sin(t * 0.0008)) * 0.5);
    const QVector3D ipColor = QVector3D(1, 1, 1) + (linearRgb(frame.baseColor) - QVector3D(1, 1, 1)) * ipMix;

    std::vector<const PinState *> pins;
    pins.reserve(scene.pins.size() + 1);
    for (const PinState &pin : scene.pins)
        pins.push_back(&pin);
    if (scene.ipPin)
        pins.push_back(&*scene.ipPin);

    // Pin lines first, in one opaque draw (see drawOpaque).
    for (const PinState *pin : pins) {
        const QVector3D base = mapPoint(pin->lat, pin->lon);
        const float k = elasticOut(float(std::clamp((t - pin->lineStarted) / 1500.0, 0.0, 1.0)));
        const float altitude = pin->lineFrom + (pin->lineTo - pin->lineFrom) * k;
        const QVector4D color(pin->ip ? ipColor : pin->lineColor, 1.f);
        pushOverlayVertex(overlay, base, 0.5f, 0.5f, color);
        pushOverlayVertex(overlay, base * altitude, 0.5f, 0.5f, color);
    }
    m_lineVertexCount = quint32(overlay.size() / OverlayFloats);

    auto addSprite = [&](QRhiGraphicsPipeline *pipeline, QRhiShaderResourceBindings *srb,
                         const QVector3D &center, const QVector3D &halfX, const QVector3D &halfY,
                         float u0, float v0, float u1, float v1, const QVector4D &color, quint64 order) {
        const quint32 first = quint32(overlay.size() / OverlayFloats);
        pushQuad(overlay, center, halfX, halfY, u0, v0, u1, v1, color);
        transparent.push_back({ pipeline, srb, &m_overlayBuffer, first, 6, viewDepth(center), order });
    };

    // Transparent line strips (intro lines, marker arcs) as line-list pairs.
    auto addLineStrip = [&](const std::vector<QVector3D> &points, const QVector4D &color, quint64 order) {
        if (points.size() < 2)
            return;
        const quint32 first = quint32(overlay.size() / OverlayFloats);
        for (size_t i = 0; i + 1 < points.size(); ++i) {
            pushOverlayVertex(overlay, points[i], 0.5f, 0.5f, color);
            pushOverlayVertex(overlay, points[i + 1], 0.5f, 0.5f, color);
        }
        transparent.push_back({ m_linePipeline.get(), m_whiteTexture.srb.get(), &m_overlayBuffer, first,
                                quint32((points.size() - 1) * 2), originDepth, order });
    };

    for (const PinState *pin : pins) {
        // Pin.js fades the top and label in over 500 ms after a 1 s delay.
        const float fade = float(std::clamp((t - pin->created - 1000.0) / 500.0, 0.0, 1.0));
        if (fade <= 0.f)
            continue;
        const QVector3D point = mapPoint(pin->lat, pin->lon);
        const float alt = pin->altitude;

        if (pin->labelVisible) {
            if (QRhiShaderResourceBindings *srb = texturedSrb(pin->label)) {
                const QVector3D center(point.x() * alt * 1.1f,
                                       point.y() * alt + (point.y() < 0 ? -15.f : 30.f),
                                       point.z() * alt * 1.1f);
                addSprite(m_spritePipeline.get(), srb, center,
                          cameraRight * (pin->label.width() / 2.f), cameraUp * (pin->label.height() / 2.f),
                          0, 0, 1, 1, QVector4D(1, 1, 1, fade), pin->id * 4 + 8);
            }
        }
        if (pin->topVisible) {
            const QVector3D tint = pin->ip ? ipColor : pin->topTint;
            addSprite(m_spritePipeline.get(), m_dotTexture.srb.get(), point * alt,
                      cameraRight * (pin->topScale / 2.f), cameraUp * (pin->topScale / 2.f),
                      0, 0, 1, 1, QVector4D(tint, fade), pin->id * 4 + 10);
        }

        if (pin->smokeVisible) {
            for (int i = 0; i < SmokePerPin; ++i) {
                const double start = pin->smokeStarted + 1000.0 * i / SmokePerSecond + 1500.0;
                double dt = t - start;
                if (dt <= 0.0)
                    continue;
                dt = std::fmod(dt, SmokeLifeMs);
                if (dt == 0.0)
                    continue;
                const QVector3D p = mapPoint(pin->lat, pin->lon - dt / 50.0, GlobeRadius * alt);
                smoke.insert(smoke.end(), { p.x(), p.y(), p.z(), float(1.0 - dt / SmokeLifeMs),
                                            float(2.5 - dt / SmokeLifeMs) });
            }
        }
    }

    // --- Intro lines (Globe.js createIntroLines + the fade/spin in tick) ---
    if (frame.introLines && t < frame.introDuration) {
        if (m_introLines.empty()) {
            auto *rng = QRandomGenerator::global();
            for (int i = 0; i < frame.introLinesCount; ++i) {
                const double lat = rng->generateDouble() * 180 + 90;
                double lon = rng->generateDouble() * 5;
                int length = 4 + int(rng->generateDouble() * 5);
                if (rng->generateDouble() < 0.3) {
                    lon = rng->generateDouble() * 30 - 50;
                    length = 3 + int(rng->generateDouble() * 3);
                }
                std::vector<QVector3D> line;
                for (int j = 0; j < length; ++j)
                    line.push_back(mapPoint(lat, lon - j * 5) * float(frame.introAltitude));
                m_introLines.push_back(std::move(line));
            }
        }
        // tick() ends up at opacity 1 until the last 20%, then fades out.
        const double progress = t / frame.introDuration;
        const float opacity = progress > 0.8 ? float(std::max(1.0 - progress, 0.0) * 5) : 1.f;
        const double spin = 2 * M_PI * progress; // rotateY a full turn over the intro
        const float c = float(std::cos(spin)), sn = float(std::sin(spin));
        const QVector4D color(linearRgb(frame.introLinesColor), opacity);
        std::vector<QVector3D> rotated;
        for (const auto &line : m_introLines) {
            rotated.clear();
            for (const QVector3D &p : line)
                rotated.emplace_back(p.x() * c + p.z() * sn, p.y(), -p.x() * sn + p.z() * c);
            addLineStrip(rotated, color, OrderIntroLines);
        }
    }

    // --- Markers (Marker.js) ---
    std::vector<QVector3D> arcNow;
    auto arcAt = [&](const std::vector<QVector3D> &points, const MarkerState &marker) -> bool {
        const int n = int(points.size());
        // The arc is drawn one point per step: later vertices wait on the newest point.
        const int drawn = std::clamp(int((t - marker.created) / MarkerStepMs), 0, n - 1);
        arcNow.resize(n);
        for (int x = 0; x < n; ++x)
            arcNow[x] = points[std::min(x, drawn)];
        if (marker.retractStarted >= 0) {
            // Marker.remove shifts the arc toward its end once per step, then drops it.
            const int steps = int((t - marker.retractStarted) / MarkerStepMs) + 1;
            if (steps >= n)
                return false;
            for (int x = 0; x < steps; ++x)
                for (int i = 0; i < x; ++i)
                    arcNow[i] = arcNow[i + 1];
        }
        return true;
    };

    for (const MarkerState &marker : scene.markers) {
        if (!marker.arc.empty()) {
            const QVector4D color(marker.lineColor, 0.5f);
            if (arcAt(marker.arc, marker))
                addLineStrip(arcNow, color, marker.id * 4 + 8);
            if (arcAt(marker.arcDotted, marker))
                addLineStrip(arcNow, color, marker.id * 4 + 9);
        }

        const QVector3D point = mapPoint(marker.lat, marker.lon);
        const float alt = marker.altitude;

        // The ring pops in with an elastic scale 0 -> 50, after the arc has been drawn.
        const double delay = marker.arc.empty() ? 0.0 : MarkerDrawMs;
        const float scale = 50.f * elasticOut(float(std::clamp((t - marker.created - delay) / 2000.0, 0.0, 1.0)));
        if (scale > 0.f) {
            if (QRhiShaderResourceBindings *srb = texturedSrb(marker.sprite)) {
                addSprite(m_spritePipeline.get(), srb, point * alt, cameraRight * (scale / 2.f),
                          cameraUp * (scale / 2.f), 0, 0, 1, 1, QVector4D(1, 1, 1, 0.7f), marker.id * 4 + 10);
            }
        }

        const float fade = float(std::clamp((t - marker.created) / 500.0, 0.0, 1.0));
        if (fade > 0.f) {
            if (QRhiShaderResourceBindings *srb = texturedSrb(marker.label)) {
                const QVector3D center(point.x() * alt * 1.1f,
                                       point.y() * alt * 1.05f + (point.y() < 0 ? -15.f : 30.f),
                                       point.z() * alt * 1.1f);
                addSprite(m_spritePipeline.get(), srb, center,
                          cameraRight * (marker.label.width() / 2.f), cameraUp * (marker.label.height() / 2.f),
                          0, 0, 1, 1, QVector4D(1, 1, 1, fade), marker.id * 4 + 11);
            }
        }
    }

    for (const SatelliteGroup &group : scene.satelliteGroups) {
        QRhiShaderResourceBindings *srb = texturedSrb(group.sheet);
        if (!srb)
            continue;
        const int sheetFrame = satelliteFrame(t - group.started, group.repeatAt);
        const float u0 = float(sheetFrame % SatelliteCols) / SatelliteCols;
        const float v0 = float(sheetFrame / SatelliteCols) / SatelliteRows;
        const float half = 75.f * group.size;

        for (const SatelliteState &sat : group.satellites) {
            const QVector3D pos = mapPoint(sat.lat, sat.lon, GlobeRadius * sat.altitude);
            // mesh.lookAt(camera) then two rotateZ calls (Satellite.tick).
            const QVector3D z = (eye - pos).normalized();
            const QVector3D x = QVector3D::crossProduct(QVector3D(0, 1, 0), z).normalized();
            const QVector3D y = QVector3D::crossProduct(z, x);
            const double tiltMultiplier = M_PI / 2 * (1 - std::abs(sat.lat / 90));
            const double tiltDirection = sat.lat > 0 ? -1 : 1;
            const double theta = tiltDirection * M_PI / 2
                - std::sin(frame.cameraAngle + (sat.lon / 180) * M_PI) * tiltMultiplier * tiltDirection;
            const QVector3D xr = x * float(std::cos(theta)) + y * float(std::sin(theta));
            const QVector3D yr = y * float(std::cos(theta)) - x * float(std::sin(theta));

            addSprite(m_satellitePipeline.get(), srb, pos, xr * half, yr * half,
                      u0, v0, u0 + 1.f / SatelliteCols, v0 + 1.f / SatelliteRows,
                      QVector4D(1, 1, 1, 1), group.id * 4 + 8);
        }
    }

    if (!smoke.empty()) {
        transparent.push_back({ m_smokePipeline.get(), m_uniformSrb.get(), &m_smokeBuffer, 0,
                                quint32(smoke.size() / SmokeFloats), originDepth, OrderSmoke });
    }

    if (!overlay.empty()) {
        const quint32 bytes = quint32(overlay.size() * sizeof(float));
        ensureBufferSize(m_rhi, m_overlayBuffer, bytes);
        u->updateDynamicBuffer(m_overlayBuffer.get(), 0, bytes, overlay.data());
    }
    if (!smoke.empty()) {
        const quint32 bytes = quint32(smoke.size() * sizeof(float));
        ensureBufferSize(m_rhi, m_smokeBuffer, bytes);
        u->updateDynamicBuffer(m_smokeBuffer.get(), 0, bytes, smoke.data());
    }
}

void OverlayRenderer::drawOpaque(QRhiCommandBuffer *cb, const QRhiViewport &viewport)
{
    if (m_lineVertexCount == 0 || !m_overlayBuffer)
        return;
    cb->setGraphicsPipeline(m_linePipeline.get());
    cb->setViewport(viewport);
    cb->setShaderResources(m_whiteTexture.srb.get());
    const QRhiCommandBuffer::VertexInput input(m_overlayBuffer.get(), 0);
    cb->setVertexInput(0, 1, &input);
    cb->draw(m_lineVertexCount);
}

} // namespace encom
