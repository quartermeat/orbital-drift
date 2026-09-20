#version 330
uniform vec2 resolution;
uniform float time;
uniform float energy;
out vec4 finalColor;
float hash(vec2 p) { return fract(sin(dot(p,vec2(127.1,311.7)))*43758.5453); }
float noise(vec2 p) {
    vec2 i=floor(p),f=fract(p); f=f*f*(3.-2.*f);
    return mix(mix(hash(i),hash(i+vec2(1,0)),f.x),mix(hash(i+vec2(0,1)),hash(i+vec2(1,1)),f.x),f.y);
}
float fbm(vec2 p) {
    float v=0.,a=.5;
    for(int i=0;i<5;i++){v+=a*noise(p);p=mat2(.8,-.6,.6,.8)*p*2.03+13.;a*=.5;}
    return v;
}
void main() {
    vec2 uv=gl_FragCoord.xy/resolution;
    vec2 p=(uv-.5)*vec2(resolution.x/resolution.y,1.);
    float t=time*.015;
    float cloud=fbm(p*3.+vec2(t,-t*.4));
    float veil=fbm(p*5.+vec2(cloud*2.,t));
    float core=exp(-length(p-vec2(.0,.10))*3.7);
    float filament=pow(max(0.,1.-abs(veil-.52)*5.),4.);
    vec3 c=vec3(.012,.024,.048);
    c+=vec3(.055,.055,.15)*pow(cloud,2.)*1.1;
    c+=vec3(.015,.11,.13)*core*veil*(.6+energy*1.5);
    c+=vec3(.09,.05,.16)*filament*cloud*.28;
    c*=1.-.36*length(uv-.5);
    finalColor=vec4(c,1.);
}
