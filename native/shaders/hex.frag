#version 440

layout(location = 0) in vec4 vColor;
layout(location = 1) in float vDepth;

layout(location = 0) out vec4 fragColor;

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
    // Tiles that haven't appeared yet must not write depth and hide the ones behind them.
    if (vColor.a <= 0.0)
        discard;

    float fogFactor = smoothstep(fogNear, fogFar, vDepth);
    fragColor = mix(vColor, vec4(0.0, 0.0, 0.0, vColor.a), fogFactor);
}
