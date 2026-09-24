#version 330
// The ball, raking. It pushes grains out of its path and heaps them to either
// side. Unlike the plate this is a carve rather than a transport: it writes what
// the tray looks like behind the ball, so it is not grain-conserving and never
// claimed to be.
in vec2 fragTexCoord;
out vec4 finalColor;
uniform sampler2D texture0;
uniform vec2 ballFrom;
uniform vec2 ballTo;
uniform float carve;
uniform float bed;          // grains in a levelled cell

void main() {
    vec2 uv=fragTexCoord;
    vec2 p=uv*2.0-1.0;
    float old=texture(texture0,uv).r;
    vec2 line=ballTo-ballFrom;
    float t=clamp(dot(p-ballFrom,line)/max(dot(line,line),1e-9),0.0,1.0);
    float d=length(p-(ballFrom+t*line));
    float groove=-.50*exp(-pow(d/.010,2.0));
    float ridge=.21*exp(-pow((d-.018)/.006,2.0));
    float influence=(1.0-smoothstep(.023,.036,d))*carve;
    float grains=max(0.0,floor(bed*(1.0+groove+ridge)+0.5));
    float h=mix(old,grains,influence);
    // The untouched tray persists exactly. There is no global fade or timer.
    finalColor=vec4(floor(h+0.5),0.0,0.0,1.0);
}
