#version 330
in float vibration;
in float fleck;
out vec4 finalColor;
void main() {
    vec2 p=gl_PointCoord*2.0-1.0;
    float edge=max(abs(p.x),abs(p.y));
    float cover=1.0-smoothstep(.73,1.0,edge);
    vec3 sand=mix(vec3(.62,.53,.39),vec3(.82,.72,.54),fleck);
    sand*=.84+.16*(1.0-p.y)+vibration*.12;
    finalColor=vec4(sand,cover*.88);
}
