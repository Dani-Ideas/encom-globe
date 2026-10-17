#version 440

// Port of the vertex shader in src/ParticleGlobe.js.

layout(location = 0) in vec3 position; // on the sphere (or in the atmosphere layer)
layout(location = 1) in vec2 latLon;
layout(location = 2) in vec3 color0;
layout(location = 3) in float phase;
layout(location = 4) in float speed;

layout(location = 0) out vec3 vColor;
layout(location = 1) out float vAlpha;
layout(location = 2) out float vFog;
layout(location = 3) out float vSize;

// Shared by particles.* and shell.*; must match ParticleUniforms in particlerenderer.cpp.
layout(std140, binding = 0) uniform buf {
    mat4 projection;
    mat4 modelView;
    mat4 rotation;          // auto spin + drag (upper 3x3)
    vec4 color;             // atmosphere rgb + particle opacity
    vec2 mouse;             // cursor in NDC, smoothed
    vec2 viewScale;         // (tan(fov / 2) * aspect, tan(fov / 2))
    float globe;            // 1 = sphere, 0 = map (springs overshoot)
    float time;             // seconds
    float turbulence;
    float mapScale;         // world units per degree
    float pointSize;        // world units
    float mapPointSize;
    float pixelScale;       // viewport height / (2 tan(fov / 2))
    float fogNear;
    float fogFar;
    float hover;            // 0..1 cursor gravity strength
    float hoverRadius;
    float hoverPull;
    float hoverTurbulence;
    float layer;            // 0 = land particles, 1 = atmosphere particles
    float shellRadius;
    float rimPower;
    float shellOpacity;
    float introTime;        // ms since start (clamped), for the spawn sweep
    float introDuration;
    float introAltitude;
};

void main()
{
    bool atmosphere = layer > 0.5;
    float globeAmount = clamp(globe, 0.0, 1.0);
    vec3 wave = vec3(sin(time * speed + phase),
                     cos(time * speed * 0.8 + phase * 1.7),
                     sin(time * speed * 1.3 + phase * 0.6));
    // Spawn sweep, as in hex.vert: a particle appears when the sweep reaches its
    // longitude, hovers at introAltitude for 1/8 of the intro at 30% opacity, then
    // drops to the surface over 200 ms. Atmosphere particles fade in instead.
    float introStart = introDuration * ((180.0 + latLon.y) / 360.0);
    float flyEnd = introStart + introDuration / 8.0;
    float introAlpha = introTime > introStart ? 1.0 : 0.0;
    float introScale = 1.0;
    if (introTime > introStart && introTime < flyEnd) {
        introScale = introAltitude;
        introAlpha = 0.3;
    }
    if (introTime > flyEnd && introTime < flyEnd + 200.0)
        introScale = 1.0 + (introAltitude - 1.0) * (1.0 - (introTime - flyEnd) / 200.0);
    if (atmosphere) {
        introScale = 1.0;
        introAlpha = clamp(introTime / max(introDuration, 1.0), 0.0, 1.0);
    }
    vec3 sphere = mat3(rotation) * (position * introScale) + wave * turbulence;
    vec3 onMap = vec3(latLon.y * mapScale, latLon.x * mapScale, 0.0);
    // The atmosphere has no place on the map: it stays on its shell and fades out.
    vec4 mvPosition = modelView * vec4(atmosphere ? sphere : mix(onMap, sphere, globe), 1.0);

    // Mouse gravity, in the view plane at this particle's depth.
    float depth = -mvPosition.z;
    vec2 toCursor = mouse * viewScale * depth - mvPosition.xy;
    float influence = hover * exp(-dot(toCursor, toCursor) / (hoverRadius * hoverRadius));
    mvPosition.xy += toCursor * hoverPull * influence;
    mvPosition.xyz += wave.zxy * hoverTurbulence * influence;

    float size = atmosphere ? pointSize : mix(mapPointSize, pointSize, globeAmount);
    vSize = max(atmosphere ? 1.0 : 1.5, size * pixelScale / -mvPosition.z);
    gl_PointSize = vSize;
    gl_Position = projection * mvPosition;
    vColor = atmosphere ? color.rgb : color0;
    vAlpha = introAlpha * (atmosphere ? color.a * globeAmount : 1.0);
    vFog = smoothstep(fogNear, fogFar, -mvPosition.z);
}
