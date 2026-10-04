#version 330
layout(location=0) in vec3 grain; // pixel x, pixel y, local vibration
uniform vec2 resolution;
uniform float grainSize;
out float vibration;
out float fleck;
void main() {
    gl_Position=vec4(grain.xy/resolution*vec2(2.0,-2.0)+vec2(-1.0,1.0),0.0,1.0);
    gl_PointSize=grainSize;
    vibration=grain.z;
    fleck=fract(sin(float(gl_VertexID)*17.123+9.71)*43758.5453);
}
