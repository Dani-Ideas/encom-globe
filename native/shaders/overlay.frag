#version 440

layout(location = 0) in vec2 vUv;
layout(location = 1) in vec4 vColor;
layout(location = 2) in float vDepth;

layout(location = 0) out vec4 fragColor;

layout(binding = 1) uniform sampler2D tex;

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

// three.js built-in materials convert their (linear) result to sRGB on output.
vec3 linearToSrgb(vec3 c)
{
    return mix(pow(c, vec3(0.41666)) * 1.055 - vec3(0.055), c * 12.92, vec3(lessThanEqual(c, vec3(0.0031308))));
}

void main()
{
    vec4 c = texture(tex, vUv) * vColor;
    if (c.a <= 0.004)
        discard;
    float fogFactor = smoothstep(fogNear, fogFar, vDepth);
    fragColor = vec4(mix(linearToSrgb(c.rgb), vec3(0.0), fogFactor), c.a);
}
