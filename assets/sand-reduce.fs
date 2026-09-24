#version 330
// One small pass so the grains can be counted. Each output texel sums its own
// patch of the tray -- grains, grains squared, and how many cells it actually
// covered -- and the whole gauge adds up to how much sand is on the tray and
// how far from level it lies.
in vec2 fragTexCoord;
out vec4 finalColor;
uniform sampler2D texture0;
uniform float cell;
void main() {
    vec2 base=fragTexCoord-vec2(cell*0.5);
    float grains=0.0,squares=0.0,counted=0.0;
    for(int y=0;y<24;++y)for(int x=0;x<24;++x) {
        vec2 uv=base+vec2((float(x)+0.5)/24.0,(float(y)+0.5)/24.0)*cell;
        vec2 p=uv*2.0-1.0;
        if(dot(p,p)>=0.999*0.999)continue;
        float n=texture(texture0,clamp(uv,vec2(0.0),vec2(1.0))).r;
        grains+=n;squares+=n*n;counted+=1.0;
    }
    finalColor=vec4(grains,squares,counted,1.0);
}
