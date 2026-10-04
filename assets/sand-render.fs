#version 330
in vec2 fragTexCoord;
out vec4 finalColor;
uniform sampler2D texture0;
uniform vec2 resolution;
uniform vec2 texel;
uniform float warmth;
uniform float bed;          // grains in a levelled cell
uniform vec2 field;         // width and height in cells

float hash(vec2 p) { return fract(sin(dot(p,vec2(127.1,311.7)))*43758.5453); }
// The tray is counted, not measured: one cell holds a whole number of grains.
float grainsAt(vec2 uv) { return texelFetch(texture0,clamp(ivec2(uv*field),ivec2(0),ivec2(field)-1),0).r; }
// Lighting wants the shape of the sand, which is the neighbourhood rather than
// the single cell -- the cell itself is the speckle.
float driftAt(vec2 uv) {
    float total=0.0;
    for(int y=-1;y<=1;++y)for(int x=-1;x<=1;++x)total+=grainsAt(uv+vec2(float(x),float(y))*texel);
    return total/9.0;
}
void main() {
    vec2 pixel=vec2(gl_FragCoord.x,resolution.y-gl_FragCoord.y);
    // Every pixel belongs to the sand surface, including the corners.
    vec3 colour;
    float opacity=0.0;
    {
        vec2 uv=pixel/resolution;
        float grains=grainsAt(uv);
        float drift=driftAt(uv);
        float dx=driftAt(uv+vec2(texel.x,0))-driftAt(uv-vec2(texel.x,0));
        float dy=driftAt(uv+vec2(0,texel.y))-driftAt(uv-vec2(0,texel.y));
        vec3 normal=normalize(vec3(-dx/max(bed,.001)*4.0,-dy/max(bed,.001)*4.0,1.0));
        vec3 light=normalize(vec3(-.65,-.8,1.05));
        float diffuse=max(0.0,dot(normal,light));
        float grit=hash(floor(uv*field));
        float fine=hash(floor(uv*field)+vec2(37.0,11.0));
        // Bare tray, seen through where the sand has gone.
        vec3 floorTone=vec3(.075,.066,.056);
        vec3 sand=mix(vec3(.68,.60,.45),vec3(.79,.70,.53),warmth*.2);
        // A cell with a couple of grains in it is not a thin wash of colour:
        // it is a couple of grains, so it is dithered rather than faded.
        float cover=clamp(grains/max(bed*.55,.001),0.0,1.0);
        cover=clamp(cover*1.15-grit*.30,0.0,1.0);
        cover=smoothstep(.18,.55,cover);
        opacity=cover;
        // Deeper piles catch more light, which is what makes a ridge read.
        float depth=clamp(drift/max(bed*3.0,.001),0.0,1.0);
        vec3 lit=sand*(.40+.55*diffuse+.30*depth);
        lit+=(grit-.5)*.075+(fine-.5)*.03;
        lit+=pow(grit,34.0)*pow(diffuse,10.0)*.14;
        colour=mix(floorTone,lit,cover);
    }
    // The cleared ground belongs to the desktop behind the transparent window.
    finalColor=vec4(colour,opacity);
}
