#version 440

// Pin smoke (src/SmokeProvider.js). Particle motion is computed on the CPU.

layout(location = 0) in vec3 position;
layout(location = 1) in vec2 alphaSize; // x = opacity, y = point size in CSS px

layout(location = 0) out float vAlpha;
layout(location = 1) out float vDepth;

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
    vAlpha = alphaSize.x;
    vec4 mvPosition = modelView * vec4(position, 1.0);
    vDepth = -mvPosition.z;
    gl_PointSize = alphaSize.y * pointScale;
    gl_Position = mvp * vec4(position, 1.0);
}
