#version 440

// Atmosphere shell: a unit sphere scaled to shellRadius.

layout(location = 0) in vec3 position;

layout(location = 0) out vec3 vNormal;
layout(location = 1) out vec3 vView;

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
    vec3 n = normalize(position);
    vec4 mvPosition = modelView * vec4(n * shellRadius, 1.0);
    vNormal = (modelView * vec4(n, 0.0)).xyz;
    vView = -mvPosition.xyz;
    gl_Position = projection * mvPosition;
}
