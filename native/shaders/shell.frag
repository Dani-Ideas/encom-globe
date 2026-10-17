#version 440

// Transparent where the shell faces the camera, glowing toward its rim (additive).

layout(location = 0) in vec3 vNormal;
layout(location = 1) in vec3 vView;

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
    float facing = abs(dot(normalize(vNormal), normalize(vView)));
    float alpha = pow(1.0 - facing, rimPower) * shellOpacity * clamp(globe, 0.0, 1.0);
    fragColor = vec4(color.rgb, alpha);
}
