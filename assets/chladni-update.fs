#version 330
// The plate, shaking, one grain at a time. The tray is a grid of matter rather
// than a height map: each cell holds a whole number of grains, and sand is the
// only kind of matter in it so far.
//
// Grains move in 2x2 blocks (a Margolus neighbourhood). Every cell of a block
// works out the same single exchange and then reads off its own result, so two
// grains can never land in the same cell and none is ever duplicated or lost.
// The block offset alternates each sweep, or a grain would rattle inside its
// own four cells forever. The twin of src/chladni.hpp.
in vec2 fragTexCoord;
out vec4 finalColor;
uniform sampler2D texture0;
uniform float field;        // cells across the tray
uniform float phase;        // 0 or 1: which way the blocks are laid out
uniform float salt;         // changes every sweep
uniform float push;         // how hard the shaking throws a grain
uniform float agitation;
uniform float reposeGap;
uniform vec3 mode;          // rings, lobes, spin
uniform vec3 harmonic;
uniform float harmonicWeight;

float besselJ(float m,float x) {
    x=abs(x);
    float series=0.0;
    if(x<7.5) {
        float term=1.0;
        for(int i=1;i<=12;++i){if(float(i)>m)break;term*=x*0.5/float(i);}
        series=term;
        float quarter=x*x*0.25;
        for(int k=1;k<=18;++k){term*=-quarter/(float(k)*(float(k)+m));series+=term;}
    }
    float wave=x>1e-4?sqrt(2.0/(3.14159265*x))*cos(x-m*1.57079633-0.78539816):0.0;
    float t=clamp((x-5.0)*0.5,0.0,1.0);
    return series*(1.0-t*t*(3.0-2.0*t))+wave*t*t*(3.0-2.0*t);
}
float standing(vec3 m,vec2 p) {
    float argument=m.x*length(p);
    float radial=besselJ(m.y,argument)*sqrt(max(1.0,argument));
    float angle=m.y>0.5?atan(p.y,p.x):0.0;
    return radial*cos(m.y*angle+m.z);
}
float energyAt(vec2 p) {
    float d=standing(mode,p);
    if(harmonicWeight>0.001)d+=harmonicWeight*standing(harmonic,p);
    d/=1.0+harmonicWeight;
    return clamp(d*d*0.55,0.0,1.0);
}
bool onPlate(vec2 p) { return dot(p,p)<0.999*0.999; }
uint scramble(uint x) {
    x=(x^61u)^(x>>16u);x*=9u;x=x^(x>>4u);x*=0x27d4eb2du;x=x^(x>>15u);return x;
}
// A sine hash correlates along diagonals, and a block automaton prints that
// correlation straight onto the tray as hatching.
float hash(ivec2 block,float seed) {
    uint h=scramble(uint(block.x)*73856093u^uint(block.y)*19349663u^uint(int(seed))*83492791u);
    return float(h&0xffffffu)/float(0x1000000u);
}

void main() {
    ivec2 cell=ivec2(gl_FragCoord.xy);
    int side=int(field);
    int offset=int(phase);
    ivec2 origin=((cell-ivec2(offset))>>1)*2+ivec2(offset);
    ivec2 seat=cell-origin;
    int mine=seat.y*2+seat.x;

    float count[4];bool inside[4];float energy[4];
    for(int i=0;i<4;++i) {
        ivec2 at=origin+ivec2(i&1,i>>1);
        inside[i]=false;count[i]=0.0;energy[i]=0.0;
        if(at.x<0||at.y<0||at.x>=side||at.y>=side)continue;
        vec2 p=(vec2(at)+0.5)/field*2.0-1.0;
        if(!onPlate(p))continue;
        inside[i]=true;
        count[i]=texelFetch(texture0,at,0).r;
        energy[i]=energyAt(p);
    }
    if(!inside[mine]){finalColor=vec4(count[mine],0.0,0.0,1.0);return;}
    if(agitation<=0.0){finalColor=vec4(count[mine],0.0,0.0,1.0);return;}

    // Sand leaves the most violent cell it can and looks for the stillest.
    int from=-1,to=-1;
    for(int i=0;i<4;++i) {
        if(!inside[i])continue;
        if(count[i]>0.0&&(from<0||energy[i]>energy[from]))from=i;
        if(to<0||energy[i]<energy[to])to=i;
    }
    if(from>=0&&to>=0&&from!=to) {
        float chance=clamp(push*(energy[from]-energy[to])*agitation,0.0,0.95);
        if(hash(origin,salt)<chance) {
            float grains=max(1.0,floor(count[from]*chance*0.8));
            count[from]-=grains;count[to]+=grains;
        }
    }
    // A pile too steep for itself slumps, shaking or not, one grain at a time.
    int tall=-1,low=-1;
    for(int i=0;i<4;++i) {
        if(!inside[i])continue;
        if(tall<0||count[i]>count[tall])tall=i;
        if(low<0||count[i]<count[low])low=i;
    }
    if(tall>=0&&low>=0&&tall!=low&&count[tall]-count[low]>=reposeGap
       &&hash(origin,salt+37.0)<0.5) {
        count[tall]-=1.0;count[low]+=1.0;
    }
    finalColor=vec4(count[mine],0.0,0.0,1.0);
}
