#version 440

layout(location = 0) in float vAlpha;
layout(location = 1) in float vDepth;

layout(location = 0) out vec4 fragColor;

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
    if (vAlpha <= 0.0)
        discard;
    float fogFactor = smoothstep(1500.0, 1800.0, vDepth);
    fragColor = mix(vec4(smokeColor.rgb, vAlpha), vec4(0.0, 0.0, 0.0, vAlpha), fogFactor);
}
