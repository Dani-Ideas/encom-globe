var THREE = require('three'),
    TWEEN = require('@tweenjs/tween.js'),
    Quadtree2 = require('quadtree2'),
    Vec2 = require('vec2'),
    motion = require('motion'),
    Globe = require('./Globe'),
    utils = require('./utils'),
    SmokeProvider = require('./SmokeProvider');

/* A particle version of the globe, after motion.dev's "three TSL" example: every land
 * tile from grid.js is a round particle that springs between the spinning globe and
 * a flat world map. Plain WebGL (RawShaderMaterial) rather than WebGPU/TSL so it also
 * runs inside Qt WebEngine.
 *
 * It opens like the hex globe: particles spawn in a west-to-east sweep, hovering at
 * introAltitude before dropping to the surface, while the intro lines spin around it.
 *
 * The mouse acts like a small gravity well: particles near the cursor are pulled
 * toward it and their turbulence rises.
 *
 * An optional atmosphere surrounds the globe: a larger concentric shell that only
 * glows at its rim (fresnel), plus a sparse layer of smaller particles between the
 * globe and the shell. It fades out when the globe flattens into the map.
 *
 * Pins, markers (with their arcs) and satellites from the hex globe can be added
 * too. They live in a group that gets the particles' rotation, and they hide while
 * the globe is flattened. */

var vertexShader = [
    "precision highp float;",
    "uniform mat4 projectionMatrix;",
    "uniform mat4 modelViewMatrix;",
    "uniform float globe;",           // 1 = sphere, 0 = flat map (springs overshoot both ways)
    "uniform float time;",            // seconds
    "uniform float introTime;",       // ms since start (clamped), for the spawn sweep
    "uniform float introDuration;",
    "uniform float introAltitude;",
    "uniform mat3 rotation;",         // auto spin + mouse drag, built in tick()
    "uniform float turbulence;",      // drift amplitude everywhere
    "uniform float mapScale;",        // world units per degree
    "uniform float pointSize;",       // world units on the sphere
    "uniform float mapPointSize;",    // world units on the map
    "uniform float pixelScale;",      // viewport height / (2 tan(fov / 2))
    "uniform float fogNear;",
    "uniform float fogFar;",
    "uniform vec2 viewScale;",        // (tan(fov / 2) * aspect, tan(fov / 2))
    "uniform vec2 mouse;",            // cursor in NDC, smoothed
    "uniform float hover;",           // 0..1, springs in/out as the cursor enters/leaves
    "uniform float hoverRadius;",     // world units, measured at each particle's depth
    "uniform float hoverPull;",       // fraction of the way to the cursor at its centre
    "uniform float hoverTurbulence;", // extra drift amplitude under the cursor
    "uniform float layer;",           // 0 = land particles, 1 = atmosphere particles
    "uniform vec4 atmosphereColor;",  // rgb + particle opacity for the atmosphere layer
    "attribute vec3 position;",       // on the sphere (or in the atmosphere shell)
    "attribute vec2 latLon;",
    "attribute vec3 color;",
    "attribute float phase;",
    "attribute float speed;",
    "varying vec3 vColor;",
    "varying float vAlpha;",
    "varying float vFog;",
    "varying float vSize;",
    "",
    "void main()",
    "{",
    "   bool atmosphere = layer > 0.5;",
    "   float globeAmount = clamp(globe, 0.0, 1.0);",
    "   vec3 wave = vec3(sin(time * speed + phase),",
    "                    cos(time * speed * 0.8 + phase * 1.7),",
    "                    sin(time * speed * 1.3 + phase * 0.6));",
    // Spawn sweep, as in the hex globe's shader: a particle appears when the sweep
    // reaches its longitude, hovers at introAltitude for 1/8 of the intro at 30%
    // opacity, then drops to the surface over 200 ms. Atmosphere particles fade in.
    "   float introStart = introDuration * ((180.0 + latLon.y) / 360.0);",
    "   float flyEnd = introStart + introDuration / 8.0;",
    "   float introAlpha = introTime > introStart ? 1.0 : 0.0;",
    "   float introScale = 1.0;",
    "   if (introTime > introStart && introTime < flyEnd) {",
    "      introScale = introAltitude;",
    "      introAlpha = 0.3;",
    "   }",
    "   if (introTime > flyEnd && introTime < flyEnd + 200.0) {",
    "      introScale = 1.0 + (introAltitude - 1.0) * (1.0 - (introTime - flyEnd) / 200.0);",
    "   }",
    "   if (atmosphere) {",
    "      introScale = 1.0;",
    "      introAlpha = clamp(introTime / max(introDuration, 1.0), 0.0, 1.0);",
    "   }",
    "   vec3 sphere = rotation * (position * introScale) + wave * turbulence;",
    "   vec3 onMap = vec3(latLon.y * mapScale, latLon.x * mapScale, 0.0);",
    // The atmosphere has no place on the map: it stays on its shell and fades out.
    "   vec4 mvPosition = modelViewMatrix * vec4(atmosphere ? sphere : mix(onMap, sphere, globe), 1.0);",
    "",
    // Mouse gravity, in the view plane at this particle's depth: a Gaussian well
    // around the cursor that pulls particles in and shakes them harder.
    "   float depth = -mvPosition.z;",
    "   vec2 toCursor = mouse * viewScale * depth - mvPosition.xy;",
    "   float influence = hover * exp(-dot(toCursor, toCursor) / (hoverRadius * hoverRadius));",
    "   mvPosition.xy += toCursor * hoverPull * influence;",
    "   mvPosition.xyz += wave.zxy * hoverTurbulence * influence;",
    "",
    "   float size = atmosphere ? pointSize : mix(mapPointSize, pointSize, globeAmount);",
    "   vSize = max(atmosphere ? 1.0 : 1.5, size * pixelScale / -mvPosition.z);",
    "   gl_PointSize = vSize;",
    "   gl_Position = projectionMatrix * mvPosition;",
    "   vColor = atmosphere ? atmosphereColor.rgb : color;",
    "   vAlpha = introAlpha * (atmosphere ? atmosphereColor.a * globeAmount : 1.0);",
    "   vFog = smoothstep(fogNear, fogFar, -mvPosition.z);",
    "}"
].join("\n");

var fragmentShader = [
    "precision highp float;",
    "varying vec3 vColor;",
    "varying float vAlpha;",
    "varying float vFog;",
    "varying float vSize;",
    "void main()",
    "{",
    "   if (vAlpha <= 0.0) discard;",
    // Below ~3 px a round mask only dims the dot, so tiny points stay solid squares.
    "   float alpha = 1.0;",
    "   if (vSize >= 3.0) {",
    "      float d = length(gl_PointCoord - vec2(0.5));",
    "      if (d > 0.5) discard;",
    "      alpha = 1.0 - smoothstep(0.38, 0.5, d);",
    "   }",
    // Fog fades to transparent rather than to black, so the far side never forms a
    // dark shell that hides the atmosphere particles behind it.
    "   float a = alpha * vAlpha * (1.0 - vFog);",
    "   if (a <= 0.01) discard;",
    "   gl_FragColor = vec4(vColor, a);",
    "}"
].join("\n");

// Atmosphere shell: transparent where it faces the camera, glowing toward its rim.
var shellVertexShader = [
    "precision highp float;",
    "uniform mat4 projectionMatrix;",
    "uniform mat4 modelViewMatrix;",
    "uniform float shellRadius;",
    "attribute vec3 position;",
    "varying vec3 vNormal;",
    "varying vec3 vView;",
    "void main()",
    "{",
    "   vec3 n = normalize(position);",
    "   vec4 mvPosition = modelViewMatrix * vec4(n * shellRadius, 1.0);",
    "   vNormal = (modelViewMatrix * vec4(n, 0.0)).xyz;",
    "   vView = -mvPosition.xyz;",
    "   gl_Position = projectionMatrix * mvPosition;",
    "}"
].join("\n");

var shellFragmentShader = [
    "precision highp float;",
    "uniform vec3 shellColor;",
    "uniform float shellOpacity;",
    "uniform float rimPower;",
    "uniform float globe;",
    "varying vec3 vNormal;",
    "varying vec3 vView;",
    "void main()",
    "{",
    "   float facing = abs(dot(normalize(vNormal), normalize(vView)));",
    "   float alpha = pow(1.0 - facing, rimPower) * shellOpacity * clamp(globe, 0.0, 1.0);",
    "   gl_FragColor = vec4(shellColor, alpha);",
    "}"
].join("\n");

// Springs from the motion.dev example, plus a softer one for the hover well.
var springs = {
    flatten: { type: "spring", stiffness: 120, damping: 18, mass: 0.9 },
    globe:   { type: "spring", stiffness: 70,  damping: 16, mass: 1 },
    hover:   { type: "spring", stiffness: 60,  damping: 14, mass: 1 }
};

function ParticleGlobe(width, height, opts){
    var defaults = {
        tiles: [],
        baseColor: "#ffcc00",
        scale: 1.0,
        dayLength: 28000,
        tilt: -0.28,          // radians around X; negative tips the north pole away (more of the south shows)
        startLongitude: -90,  // degrees of longitude facing the viewer when the globe starts
        pointSize: 2,
        turbulence: 4,
        introDuration: 2000,  // spawn sweep + intro lines, ms (Globe's introLinesDuration)
        introAltitude: 1.10,
        introLinesColor: "#8FD8D8",
        introLinesCount: 60,
        hoverRadius: 40,     // size of the cursor's gravity well, world units (globe radius 500)
        hoverPull: -0.6,       // how far particles at the centre move toward the cursor (0..1)
        hoverTurbulence: 100,  // turbulence under the cursor (vs. `turbulence` elsewhere)
        atmosphere: true,
        atmosphereColor: null,          // defaults to baseColor
        atmosphereScale: 1.12,          // shell radius relative to the globe
        atmosphereOpacity: 0,           // shell rim brightness; 0 = no shell, particles only
        atmosphereRimPower: 3,          // higher = thinner rim
        atmosphereParticles: 2500,
        atmospherePointSize: 1.2,       // world units, vs. pointSize for land
        atmosphereParticleOpacity: 0.5,
        pinColor: "#00eeee",
        markerColor: "#ffcc00",
        satelliteColor: "#ff0000",
        font: "monospace",    // labels; Qt WebEngine needs a generic family
        maxPins: 500,
        maxMarkers: 4,
        interactive: true,    // drag to rotate (X/Y axes); Shift + drag rotates around Z
        autoMorph: 0          // ms between automatic globe/map switches (0 = off)
    };

    opts = opts || {};
    for (var key in defaults) {
        this[key] = opts[key] !== undefined ? opts[key] : defaults[key];
    }

    this.width = width;
    this.height = height;
    this.cameraDistance = 1700 / this.scale;

    this.renderer = new THREE.WebGLRenderer({ antialias: true });
    this.renderer.setSize(width, height);
    this.domElement = this.renderer.domElement;

    this.globeValue = motion.motionValue(0);
    this._flat = false;

    // Hover: strength springs 0..1; the well trails the cursor slightly.
    this.hoverValue = motion.motionValue(0);
    this._hovering = false;
    this._mouseTarget = new THREE.Vector2();
    this._mouse = new THREE.Vector2();

    // Orientation, applied as Rz(roll) * Rx(pitch) * Ry(yaw). Yaw also carries the
    // automatic spin; velocities (radians/ms) give the drag some inertia on release.
    // The longitude facing the camera is 90° - yaw (see utils.mapPoint).
    this._yaw = (90 - this.startLongitude) * Math.PI / 180;
    this._pitch = this.tilt;
    this._roll = 0;
    this._velocity = { yaw: 0, pitch: 0, roll: 0 };
    this._dragging = null;
    this._rotationMatrix = new THREE.Matrix4();
    this._axisMatrix = new THREE.Matrix4();
}

ParticleGlobe.prototype.init = function(cb){
    var _this = this;

    this.scene = new THREE.Scene();
    this.camera = new THREE.PerspectiveCamera(50, this.width / this.height, 1, this.cameraDistance + 1000);
    this.camera.position.set(0, 0, this.cameraDistance);
    this.camera.lookAt(this.scene.position);

    this.uniforms = {
        globe:           { value: 0 },
        time:            { value: 0 },
        introTime:       { value: 0 },
        introDuration:   { value: this.introDuration },
        introAltitude:   { value: this.introAltitude },
        rotation:        { value: new THREE.Matrix3() },
        turbulence:      { value: this.turbulence },
        mapScale:        { value: 1 },
        pointSize:       { value: this.pointSize },
        mapPointSize:    { value: this.pointSize },
        pixelScale:      { value: 1 },
        fogNear:         { value: this.cameraDistance },
        fogFar:          { value: this.cameraDistance + 300 },
        viewScale:       { value: new THREE.Vector2(1, 1) },
        mouse:           { value: this._mouse },
        hover:           { value: 0 },
        hoverRadius:     { value: this.hoverRadius },
        hoverPull:       { value: this.hoverPull },
        hoverTurbulence: { value: this.hoverTurbulence },
        layer:           { value: 0 },
        atmosphereColor: { value: new THREE.Vector4() }
    };

    this._createParticles();
    this._createAtmosphere();
    this._createOverlays();
    this._createIntroLines();
    this._layout();

    // Open as the globe; the spawn sweep in the shader is the intro.
    this.globeValue.set(1);

    this._bindPointer();

    if (this.autoMorph > 0) {
        this._autoTimer = setInterval(function(){ _this.toggle(); }, this.autoMorph);
    }

    this._startTime = Date.now();
    this._lastTick = this._startTime;
    if (typeof cb === "function") {
        setTimeout(cb, 0);
    }
};

ParticleGlobe.prototype._createParticles = function(){
    var tiles = this.tiles,
        count = tiles.length,
        positions = new Float32Array(count * 3),
        latLons = new Float32Array(count * 2),
        colors = new Float32Array(count * 3),
        phases = new Float32Array(count),
        speeds = new Float32Array(count);

    // Same hue set as Globe.js createParticles.
    var baseHsl = {};
    new THREE.Color(this.baseColor).getHSL(baseHsl);
    var palette = [];
    for (var ci = 0; ci < 10; ci++) {
        palette.push(new THREE.Color().setHSL(
            (baseHsl.h + (ci - 5) * 0.02 + 1) % 1,
            baseHsl.s,
            Math.min(1, baseHsl.l + Math.random() / 3.0)
        ));
    }

    for (var i = 0; i < count; i++) {
        var t = tiles[i], x = 0, y = 0, z = 0;
        for (var j = 0; j < t.b.length; j++) {
            x += t.b[j].x;
            y += t.b[j].y;
            z += t.b[j].z;
        }
        positions[i * 3]     = x / t.b.length;
        positions[i * 3 + 1] = y / t.b.length;
        positions[i * 3 + 2] = z / t.b.length;

        // grid.js stores latitude with the opposite sign (hexasphere's convention).
        latLons[i * 2]     = -t.lat;
        latLons[i * 2 + 1] = t.lon;

        var c = palette[Math.floor(Math.random() * palette.length)];
        colors[i * 3]     = c.r;
        colors[i * 3 + 1] = c.g;
        colors[i * 3 + 2] = c.b;

        phases[i] = Math.random() * Math.PI * 2;
        speeds[i] = 0.4 + Math.random() * 0.8;
    }

    var geometry = new THREE.BufferGeometry();
    geometry.setAttribute('position', new THREE.BufferAttribute(positions, 3));
    geometry.setAttribute('latLon',   new THREE.BufferAttribute(latLons, 2));
    geometry.setAttribute('color',    new THREE.BufferAttribute(colors, 3));
    geometry.setAttribute('phase',    new THREE.BufferAttribute(phases, 1));
    geometry.setAttribute('speed',    new THREE.BufferAttribute(speeds, 1));

    var material = new THREE.RawShaderMaterial({
        uniforms: this.uniforms,
        vertexShader: vertexShader,
        fragmentShader: fragmentShader,
        transparent: true
    });

    if (this.points) {
        this.scene.remove(this.points);
        this.points.geometry.dispose();
        this.points.material.dispose();
    }
    this.points = new THREE.Points(geometry, material);
    this.points.frustumCulled = false; // vertices move in the shader
    this.scene.add(this.points);
};

ParticleGlobe.prototype._atmosphereColor = function(){
    return new THREE.Color(this.atmosphereColor || this.baseColor);
};

ParticleGlobe.prototype._createAtmosphere = function(){
    this._disposeAtmosphere();
    if (!this.atmosphere) {
        return;
    }
    var radius = 500,
        color = this._atmosphereColor(),
        count = this.atmosphereParticles,
        positions = new Float32Array(count * 3),
        phases = new Float32Array(count),
        speeds = new Float32Array(count);

    this.uniforms.atmosphereColor.value.set(color.r, color.g, color.b, this.atmosphereParticleOpacity);

    // Uniform directions, spread through the layer between the globe and the shell.
    for (var i = 0; i < count; i++) {
        var z = Math.random() * 2 - 1,
            angle = Math.random() * Math.PI * 2,
            ring = Math.sqrt(1 - z * z),
            r = radius * (1.02 + Math.random() * (this.atmosphereScale - 1.02));
        positions[i * 3]     = r * ring * Math.cos(angle);
        positions[i * 3 + 1] = r * z;
        positions[i * 3 + 2] = r * ring * Math.sin(angle);
        phases[i] = Math.random() * Math.PI * 2;
        speeds[i] = 0.2 + Math.random() * 0.5;
    }

    var geometry = new THREE.BufferGeometry();
    geometry.setAttribute('position', new THREE.BufferAttribute(positions, 3));
    geometry.setAttribute('latLon',   new THREE.BufferAttribute(new Float32Array(count * 2), 2));
    geometry.setAttribute('color',    new THREE.BufferAttribute(new Float32Array(count * 3), 3));
    geometry.setAttribute('phase',    new THREE.BufferAttribute(phases, 1));
    geometry.setAttribute('speed',    new THREE.BufferAttribute(speeds, 1));

    // Same shader as the land; the shared uniform objects keep both layers in sync.
    var uniforms = Object.assign({}, this.uniforms, {
        layer:     { value: 1 },
        pointSize: { value: this.atmospherePointSize }
    });
    this.atmospherePoints = new THREE.Points(geometry, new THREE.RawShaderMaterial({
        uniforms: uniforms,
        vertexShader: vertexShader,
        fragmentShader: fragmentShader,
        transparent: true,
        depthWrite: false
    }));
    this.atmospherePoints.frustumCulled = false;
    this.scene.add(this.atmospherePoints);

    if (this.atmosphereOpacity <= 0) {
        return;
    }
    this.atmosphereShell = new THREE.Mesh(new THREE.SphereGeometry(1, 96, 48), new THREE.RawShaderMaterial({
        uniforms: {
            shellRadius:  { value: radius * this.atmosphereScale },
            shellColor:   { value: new THREE.Vector3(color.r, color.g, color.b) },
            shellOpacity: { value: this.atmosphereOpacity },
            rimPower:     { value: this.atmosphereRimPower },
            globe:        this.uniforms.globe
        },
        vertexShader: shellVertexShader,
        fragmentShader: shellFragmentShader,
        transparent: true,
        depthWrite: false,
        blending: THREE.AdditiveBlending
    }));
    this.atmosphereShell.renderOrder = 1; // after the particles
    this.scene.add(this.atmosphereShell);
};

ParticleGlobe.prototype._disposeAtmosphere = function(){
    [this.atmospherePoints, this.atmosphereShell].forEach(function(object){
        if (object) {
            object.parent.remove(object);
            object.geometry.dispose();
            object.material.dispose();
        }
    }, this);
    this.atmospherePoints = this.atmosphereShell = null;
};

// Pins, markers and satellites reuse the hex globe's code: `this.overlays` is a
// Globe-prototype object that only has the fields addPin/addMarker/addSatellite/
// addConstellation read, with `scene` pointed at a group that follows the particles.
ParticleGlobe.prototype._createOverlays = function(){
    // Only built-in materials (sprites, lines, satellites) use scene fog; it hides
    // overlays on the far side the way the hex globe does.
    this.scene.fog = new THREE.Fog(0x000000, this.cameraDistance, this.cameraDistance + 300);

    this.overlayGroup = new THREE.Group();
    this.overlayGroup.matrixAutoUpdate = false;
    this.scene.add(this.overlayGroup);

    var host = this.overlays = Object.create(Globe.prototype);
    host.scene = this.overlayGroup;
    host.smokeProvider = new SmokeProvider(this.overlayGroup);
    host.pins = [];
    host.markers = [];
    host.satellites = {};
    host.quadtree = new Quadtree2(new Vec2(180, 360), 5);
    host.pinColor = this.pinColor;
    host.markerColor = this.markerColor;
    host.satelliteColor = this.satelliteColor;
    host.font = this.font;
    host.maxPins = this.maxPins;
    host.maxMarkers = this.maxMarkers;
};

['addPin', 'addMarker', 'addSatellite', 'addConstellation', 'setMaxPins', 'setMaxMarkers'].forEach(function(name){
    ParticleGlobe.prototype[name] = function(){
        return Globe.prototype[name].apply(this.overlays, arguments);
    };
});

// Globe.js createIntroLines: short arcs high above the globe that spin once and
// fade out during the intro. They live in the overlay group so they turn with it.
ParticleGlobe.prototype._createIntroLines = function(){
    var material = new THREE.LineBasicMaterial({
        color: this.introLinesColor,
        transparent: true,
        opacity: 0.5
    });
    this.introLines = new THREE.Group();

    for (var i = 0; i < this.introLinesCount; i++) {
        var lat = Math.random() * 180 + 90,
            lon = Math.random() * 5,
            lenBase = 4 + Math.floor(Math.random() * 5);

        if (Math.random() < 0.3) {
            lon = Math.random() * 30 - 50;
            lenBase = 3 + Math.floor(Math.random() * 3);
        }

        var positions = new Float32Array(lenBase * 3);
        for (var j = 0; j < lenBase; j++) {
            var p = utils.mapPoint(lat, lon - j * 5);
            positions[j * 3]     = p.x * this.introAltitude;
            positions[j * 3 + 1] = p.y * this.introAltitude;
            positions[j * 3 + 2] = p.z * this.introAltitude;
        }
        var geometry = new THREE.BufferGeometry();
        geometry.setAttribute('position', new THREE.BufferAttribute(positions, 3));
        this.introLines.add(new THREE.Line(geometry, material));
    }
    this.overlayGroup.add(this.introLines);
};

// Globe.tick: full opacity until the last 20% of the intro, then fade; one turn.
ParticleGlobe.prototype._tickIntroLines = function(elapsed){
    if (!this.introLines) {
        return;
    }
    if (elapsed >= this.introDuration) {
        this.introLines.parent.remove(this.introLines);
        this.introLines.children.forEach(function(line){ line.geometry.dispose(); });
        if (this.introLines.children.length) {
            this.introLines.children[0].material.dispose();
        }
        this.introLines = null;
        return;
    }
    var progress = elapsed / this.introDuration;
    if (this.introLines.children.length) {
        this.introLines.children[0].material.opacity = progress > 0.8 ? Math.max(1 - progress, 0) * 5 : 1;
    }
    this.introLines.rotation.y = 2 * Math.PI * progress;
};

// Fit the 2:1 map to the viewport and keep point sizes in world units.
ParticleGlobe.prototype._layout = function(){
    var fovRadians = this.camera.fov * Math.PI / 180,
        tanHalfFov = Math.tan(fovRadians / 2),
        visibleHeight = 2 * this.cameraDistance * tanHalfFov,
        visibleWidth = visibleHeight * this.camera.aspect,
        mapWidth = 0.85 * Math.min(visibleWidth, 2 * visibleHeight),
        mapScale = mapWidth / 360;

    this.uniforms.mapScale.value = mapScale;
    // Keep a small gap between dots on the map. Tile spacing depends on the grid's
    // density (bin/buildgrid -d); estimate it from the tile count, assuming ~32% of
    // the sphere's tiles are land.
    var spacingDegrees = Math.sqrt(41253 * 0.32 / Math.max(1, this.tiles.length));
    this.uniforms.mapPointSize.value = Math.min(this.pointSize, mapScale * spacingDegrees * 0.6);
    this.uniforms.pixelScale.value = this.renderer.getDrawingBufferSize(new THREE.Vector2()).y / (2 * tanHalfFov);
    this.uniforms.viewScale.value.set(tanHalfFov * this.camera.aspect, tanHalfFov);
};

ParticleGlobe.prototype._setHover = function(on){
    if (on === this._hovering) {
        return;
    }
    this._hovering = on;
    if (this._hoverAnimation) {
        this._hoverAnimation.stop();
    }
    this._hoverAnimation = motion.animate(this.hoverValue, on ? 1 : 0, springs.hover);
};

// Hover moves the cursor's gravity well. With `interactive`, dragging also rotates:
// horizontal -> Y axis, vertical -> X axis, Shift + horizontal -> Z axis. A drag across
// the full canvas height turns the globe half a revolution.
ParticleGlobe.prototype._bindPointer = function(){
    var _this = this,
        el = this.domElement;

    this._onPointerDown = function(e){
        if (!_this.interactive) {
            return;
        }
        try {
            el.setPointerCapture(e.pointerId); // keep receiving moves outside the canvas
        } catch (err) {}
        _this._dragging = { id: e.pointerId, x: e.clientX, y: e.clientY, time: performance.now() };
        _this._velocity = { yaw: 0, pitch: 0, roll: 0 };
    };

    this._onPointerMove = function(e){
        var rect = el.getBoundingClientRect();
        _this._mouseTarget.set(
            (e.clientX - rect.left) / rect.width * 2 - 1,
            1 - (e.clientY - rect.top) / rect.height * 2
        );
        if (!_this._hovering) {
            // Entering: start the well under the cursor instead of sliding in from afar.
            _this._mouse.copy(_this._mouseTarget);
        }
        _this._setHover(true);

        var drag = _this._dragging;
        if (!drag || drag.id !== e.pointerId) {
            return;
        }
        var now = performance.now(),
            dt = Math.max(1, now - drag.time),
            perPixel = Math.PI / el.clientHeight,
            dx = (e.clientX - drag.x) * perPixel,
            dy = (e.clientY - drag.y) * perPixel,
            delta = e.shiftKey ? { yaw: 0, pitch: 0, roll: -dx } : { yaw: dx, pitch: dy, roll: 0 };

        _this._yaw += delta.yaw;
        _this._pitch += delta.pitch;
        _this._roll += delta.roll;

        // Smoothed so a final jittery event doesn't decide the throw, and capped (about
        // 1.6 turns per second) because events within the same millisecond would make
        // it explode.
        for (var axis in delta) {
            var velocity = 0.6 * _this._velocity[axis] + 0.4 * (delta[axis] / dt);
            _this._velocity[axis] = Math.max(-0.01, Math.min(0.01, velocity));
        }
        drag.x = e.clientX;
        drag.y = e.clientY;
        drag.time = now;
    };

    this._onPointerUp = function(e){
        if (_this._dragging && _this._dragging.id === e.pointerId) {
            // Holding still before letting go means no throw.
            if (performance.now() - _this._dragging.time > 80) {
                _this._velocity = { yaw: 0, pitch: 0, roll: 0 };
            }
            _this._dragging = null;
        }
        // Touch has no hover: release the well when the finger lifts.
        if (e.pointerType !== "mouse") {
            _this._setHover(false);
        }
    };

    this._onPointerLeave = function(){
        if (!_this._dragging) {
            _this._setHover(false);
        }
    };

    el.addEventListener('pointerdown', this._onPointerDown);
    el.addEventListener('pointermove', this._onPointerMove);
    el.addEventListener('pointerup', this._onPointerUp);
    el.addEventListener('pointercancel', this._onPointerUp);
    el.addEventListener('pointerleave', this._onPointerLeave);
};

ParticleGlobe.prototype._morph = function(target, spring){
    this._flat = target === 0;
    if (this._animation) {
        this._animation.stop();
    }
    this._animation = motion.animate(this.globeValue, target, spring);
};

ParticleGlobe.prototype.flatten = function(){
    this._morph(0, springs.flatten);
};

ParticleGlobe.prototype.toGlobe = function(){
    this._morph(1, springs.globe);
};

ParticleGlobe.prototype.toggle = function(){
    if (this._flat) {
        this.toGlobe();
    } else {
        this.flatten();
    }
};

ParticleGlobe.prototype.setBaseColor = function(color){
    this.baseColor = color;
    this._createParticles();
    if (!this.atmosphereColor) {
        this._createAtmosphere();
    }
};

ParticleGlobe.prototype.resize = function(width, height){
    this.width = width;
    this.height = height;
    this.renderer.setSize(width, height);
    this.camera.aspect = width / height;
    this.camera.updateProjectionMatrix();
    this._layout();
};

ParticleGlobe.prototype.tick = function(){
    if (!this.scene) {
        return;
    }
    var now = Date.now(),
        elapsed = now - this._startTime,
        dt = Math.min(100, now - this._lastTick);
    this._lastTick = now;

    if (!this._dragging) {
        // Inertia from the last drag, decaying with a ~300 ms time constant.
        var decay = Math.exp(-dt / 300);
        this._yaw += this._velocity.yaw * dt;
        this._pitch += this._velocity.pitch * dt;
        this._roll += this._velocity.roll * dt;
        this._velocity.yaw *= decay;
        this._velocity.pitch *= decay;
        this._velocity.roll *= decay;
        // Automatic spin around the globe's own axis.
        this._yaw += 2 * Math.PI * dt / this.dayLength;
    }

    this._rotationMatrix.makeRotationZ(this._roll)
        .multiply(this._axisMatrix.makeRotationX(this._pitch))
        .multiply(this._axisMatrix.makeRotationY(this._yaw));
    this.uniforms.rotation.value.setFromMatrix4(this._rotationMatrix);

    // The well trails the cursor with a ~80 ms lag, which reads as weight.
    this._mouse.lerp(this._mouseTarget, 1 - Math.exp(-dt / 80));

    this.uniforms.globe.value = this.globeValue.get();
    this.uniforms.hover.value = this.hoverValue.get();

    // Overlays turn with the particles; satellites get the camera angle in the
    // globe's frame (the hex globe orbits its camera instead of the globe).
    this.overlayGroup.visible = this.uniforms.globe.value > 0.6;
    this.overlayGroup.matrix.copy(this._rotationMatrix);
    this.overlayGroup.updateMatrixWorld(true);
    TWEEN.update();
    this._tickIntroLines(elapsed);
    this.overlays.smokeProvider.tick(elapsed);
    for (var key in this.overlays.satellites) {
        this.overlays.satellites[key].tick(this.camera.position, this._yaw + Math.PI / 2, dt);
    }

    this.uniforms.time.value = (elapsed / 1000) % 3600;
    // The sweep only needs the first seconds; clamping keeps float precision safe.
    this.uniforms.introTime.value = Math.min(elapsed, 1e7);
    this.renderer.render(this.scene, this.camera);
};

ParticleGlobe.prototype.destroy = function(){
    if (this._animation) {
        this._animation.stop();
    }
    if (this._hoverAnimation) {
        this._hoverAnimation.stop();
    }
    if (this._onPointerDown) {
        var el = this.domElement;
        el.removeEventListener('pointerdown', this._onPointerDown);
        el.removeEventListener('pointermove', this._onPointerMove);
        el.removeEventListener('pointerup', this._onPointerUp);
        el.removeEventListener('pointercancel', this._onPointerUp);
        el.removeEventListener('pointerleave', this._onPointerLeave);
    }
    clearInterval(this._autoTimer);
    this._disposeAtmosphere();
    if (this.points) {
        this.points.geometry.dispose();
        this.points.material.dispose();
    }
    this.renderer.dispose();
};

module.exports = ParticleGlobe;
