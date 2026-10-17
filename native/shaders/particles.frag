#version 440

layout(location = 0) in vec3 vColor;
layout(location = 1) in float vAlpha;
layout(location = 2) in float vFog;
layout(location = 3) in float vSize;

layout(location = 0) out vec4 fragColor;

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
    if (vAlpha <= 0.0)
        discard;
    // Below ~3 px a round mask only dims the dot, so tiny points stay solid squares.
    float alpha = 1.0;
    if (vSize >= 3.0) {
        float d = length(gl_PointCoord - vec2(0.5));
        if (d > 0.5)
            discard;
        alpha = 1.0 - smoothstep(0.38, 0.5, d);
    }
    // Fog fades to transparent rather than to black, so the far side never forms a
    // dark shell that hides the atmosphere particles behind it.
    float a = alpha * vAlpha * (1.0 - vFog);
    if (a <= 0.01)
        discard;
    fragColor = vec4(vColor, a);
}
