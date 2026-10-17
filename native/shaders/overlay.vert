#version 440

// Pin lines, pin sprites (top dot, label) and satellites. Positions arrive in
// world space; billboarding is done on the CPU.

layout(location = 0) in vec3 position;
layout(location = 1) in vec2 uv;
layout(location = 2) in vec4 color;

layout(location = 0) out vec2 vUv;
layout(location = 1) out vec4 vColor;
layout(location = 2) out float vDepth;

// Shared by every shader in this directory; must match Uniforms in globerenderer.cpp.
layout(std140, binding = 0) uniform buf {
    mat4 mvp;
    mat4 modelView;
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
    vec4 smokeColor;
};

void main()
{
    vUv = uv;
    vColor = color;
    vDepth = -(modelView * vec4(position, 1.0)).z;
    gl_Position = mvp * vec4(position, 1.0);
}
