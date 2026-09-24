#version 330
in vec2 fragTexCoord;
out vec4 finalColor;
uniform sampler2D texture0;
uniform vec2 resolution;
uniform vec2 ball;
uniform float texel;
uniform float warmth;
uniform float ballShown;
uniform float bed;          // grains in a levelled cell
uniform float field;        // cells across the tray

float hash(vec2 p) { return fract(sin(dot(p,vec2(127.1,311.7)))*43758.5453); }
// The tray is counted, not measured: one cell holds a whole number of grains.
float grainsAt(vec2 uv) { return texelFetch(texture0,ivec2(clamp(uv,vec2(0.0),vec2(.9999))*field),0).r; }
// Lighting wants the shape of the sand, which is the neighbourhood rather than
// the single cell -- the cell itself is the speckle.
float driftAt(vec2 uv) {
    float total=0.0;
    for(int y=-1;y<=1;++y)for(int x=-1;x<=1;++x)total+=grainsAt(uv+vec2(float(x),float(y))*texel);
    return total/9.0;
}
void main() {
    vec2 pixel=vec2(gl_FragCoord.x,resolution.y-gl_FragCoord.y);
    // A round tray fills the height, leaving a quiet dark tabletop around it.
    float scale=min(resolution.x*.45,resolution.y*.43);
    vec2 p=(pixel-resolution*.5)/scale;
    float r=length(p);
    float vignette=exp(-dot(p,p)*.28);
    float tablegrain=hash(floor(pixel))*.012;
    vec3 colour=vec3(.025,.031,.033)+vignette*vec3(.025,.027,.026)+tablegrain;
    float shadow=exp(-pow((length(p-vec2(.015,.035))-1.03)/.085,2.0));
    colour*=1.0-shadow*.65;
    if(r<1.077) {
        float a=atan(p.y,p.x);
        float grain=sin(a*85.0+sin(a*19.0)*3.0+r*470.0)*.5+.5;
        float lip=smoothstep(1.0,1.024,r)*(1.0-smoothstep(1.051,1.077,r));
        vec3 wood=mix(vec3(.085,.049,.031),vec3(.18,.107,.063),grain*.35+lip*.45);
        float highlight=pow(max(0.0,dot(normalize(vec3(p*.65,.3)),normalize(vec3(-.6,-.7,1.0)))),8.0);
        colour=wood+highlight*.07;
        colour+=vec3(.18,.13,.08)*exp(-pow((r-1.053)/.0028,2.0));
        colour*=.70+.30*lip;
    }
    if(r<1.0) {
        vec2 uv=p*.5+.5;
        float grains=grainsAt(uv);
        float drift=driftAt(uv);
        float dx=driftAt(uv+vec2(texel,0))-driftAt(uv-vec2(texel,0));
        float dy=driftAt(uv+vec2(0,texel))-driftAt(uv-vec2(0,texel));
        vec3 normal=normalize(vec3(-dx/max(bed,.001)*4.0,-dy/max(bed,.001)*4.0,1.0));
        vec3 light=normalize(vec3(-.65,-.8,1.05));
        float diffuse=max(0.0,dot(normal,light));
        float grit=hash(floor(uv*field));
        float fine=hash(floor(uv*field)+vec2(37.0,11.0));
        // Bare tray, seen through where the sand has gone.
        vec3 floorTone=vec3(.055,.048,.041)+vignette*vec3(.02,.018,.015);
        vec3 sand=mix(vec3(.68,.60,.45),vec3(.79,.70,.53),warmth*.2);
        // A cell with a couple of grains in it is not a thin wash of colour:
        // it is a couple of grains, so it is dithered rather than faded.
        float cover=clamp(grains/max(bed*.55,.001),0.0,1.0);
        cover=clamp(cover*1.15-grit*.30,0.0,1.0);
        cover=smoothstep(.18,.55,cover);
        // Deeper piles catch more light, which is what makes a ridge read.
        float depth=clamp(drift/max(bed*3.0,.001),0.0,1.0);
        vec3 lit=sand*(.40+.55*diffuse+.30*depth);
        lit+=(grit-.5)*.075+(fine-.5)*.03;
        lit+=pow(grit,34.0)*pow(diffuse,10.0)*.14;
        colour=mix(floorTone,lit,cover);
        colour*=1.0-.32*smoothstep(.958,1.0,r);
        vec2 relative=p-ball;
        float ballRadius=.022;
        float ballShadow=exp(-dot(relative-vec2(.010,.014),relative-vec2(.010,.014))/.00060);
        colour*=1.0-ballShadow*.56*ballShown;
        float br=length(relative)/ballRadius;
        // The plate has no ball in it; the same tray is lit either way.
        if(br<1.0&&ballShown>.5) {
            vec3 bn=normalize(vec3(relative/ballRadius,sqrt(1.0-br*br)));
            float spec=pow(max(0.0,dot(bn,normalize(vec3(-.36,-.45,1.0)))),65.0);
            float reflectBand=pow(max(0.0,1.0-abs(bn.y+.23)*3.0),6.0);
            float rim=pow(1.0-bn.z,3.0);
            colour=vec3(.11,.14,.16)+max(0.0,dot(bn,light))*.36;
            colour+=reflectBand*vec3(.38,.40,.39)+spec*.85+rim*.20;
        }
    }
    finalColor=vec4(colour,1.0);
}
