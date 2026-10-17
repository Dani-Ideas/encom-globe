#version 440

// Port of the hex grid vertex shader in src/Globe.js (createParticles).

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 color;
layout(location = 2) in vec2 latLng; // x = lat, y = lng

layout(location = 0) out vec4 vColor;
layout(location = 1) out float vDepth;

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
    float lat = latLng.x;
    float lng = latLng.y;

    vec3 newPos = position;
    float opacityVal = 0.0;
    float introStart = introDuration * ((180.0 + lng) / 360.0);

    if (currentTime > introStart) {
        opacityVal = 1.0;
    }
    if (currentTime > introStart && currentTime < introStart + introDuration / 8.0) {
        newPos = position * introAltitude;
        opacityVal = 0.3;
    }
    if (currentTime > introStart + introDuration / 8.0 && currentTime < introStart + introDuration / 8.0 + 200.0) {
        newPos = position * (1.0 + ((introAltitude - 1.0) * (1.0 - (currentTime - introStart - (introDuration / 8.0)) / 200.0)));
    }

    vec3 finalColor = color;
    if (highlightActive > 0.5) {
        float dlat = lat - highlightLat;
        float dlon = lng - highlightLon;
        if (dlon > 180.0) dlon -= 360.0;
        if (dlon < -180.0) dlon += 360.0;
        if (sqrt(dlat * dlat + dlon * dlon) < 2.0) {
            finalColor = mix(color, vec3(1.0), pulse * 0.7);
        }
    }

    vColor = vec4(finalColor, opacityVal);
    vDepth = -(modelView * vec4(newPos, 1.0)).z;
    gl_Position = mvp * vec4(newPos, 1.0);
}
