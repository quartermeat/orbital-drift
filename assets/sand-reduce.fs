#version 330
// One small pass so the grains can be counted. Each output texel sums its own
// patch of the tray -- grains, grains squared, and how many cells it actually
// covered -- and the whole gauge adds up to how much sand is on the tray and
// how far from level it lies.
in vec2 fragTexCoord;
out vec4 finalColor;
uniform sampler2D texture0;
void main() {
    ivec2 field=textureSize(texture0,0);
    ivec2 patch=ivec2(gl_FragCoord.xy);
    // The 32x32 gauge partitions every cell exactly once, including the edges.
    ivec2 first=patch*field/32,last=(patch+1)*field/32;
    float grains=0.0,squares=0.0,counted=0.0;
    for(int y=first.y;y<last.y;++y)for(int x=first.x;x<last.x;++x) {
        float n=texelFetch(texture0,ivec2(x,y),0).r;
        grains+=n;squares+=n*n;counted+=1.0;
    }
    finalColor=vec4(grains,squares,counted,1.0);
}
