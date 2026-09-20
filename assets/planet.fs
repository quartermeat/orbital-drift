#version 330
in vec3 fragPosition;
in vec3 fragNormal;
in vec4 fragColor;
uniform vec4 colDiffuse;
uniform vec3 lightDir;
uniform vec3 viewPos;
uniform vec3 ambient;
uniform float terrain;    // 1 while drawing the globe, 0 for the props on it
uniform vec3 seaColor;
uniform vec3 landColor;
out vec4 finalColor;

float hash(vec3 p) { return fract(sin(dot(p, vec3(127.1, 311.7, 74.7))) * 43758.5453); }

float valueNoise(vec3 p) {
    vec3 cell = floor(p), part = fract(p);
    part = part * part * (3.0 - 2.0 * part);
    return mix(mix(mix(hash(cell + vec3(0, 0, 0)), hash(cell + vec3(1, 0, 0)), part.x),
                   mix(hash(cell + vec3(0, 1, 0)), hash(cell + vec3(1, 1, 0)), part.x), part.y),
               mix(mix(hash(cell + vec3(0, 0, 1)), hash(cell + vec3(1, 0, 1)), part.x),
                   mix(hash(cell + vec3(0, 1, 1)), hash(cell + vec3(1, 1, 1)), part.x), part.y), part.z);
}

// Continents, coastlines and a bit of relief, all from the surface direction so
// the terrain is stable however the globe is turned.
float elevation(vec3 direction) {
    float sum = 0.0, amplitude = 0.5;
    vec3 p = direction * 2.1;
    for (int octave = 0; octave < 5; ++octave) {
        sum += amplitude * valueNoise(p);
        p *= 2.07;
        amplitude *= 0.5;
    }
    return sum;
}

void main() {
    vec3 n = normalize(fragNormal);
    vec3 l = normalize(-lightDir);
    vec3 v = normalize(viewPos - fragPosition);
    vec3 h = normalize(l + v);
    vec3 base = (fragColor * colDiffuse).rgb;
    float gloss = 26.0, glossStrength = 0.22;

    if (terrain > 0.5) {
        vec3 direction = normalize(fragPosition);
        float height = elevation(direction);
        float land = smoothstep(0.47, 0.55, height);
        float shore = smoothstep(0.44, 0.47, height) * (1.0 - land);
        base = mix(seaColor, landColor, land);
        base = mix(base, landColor * 1.35, shore * 0.5);
        // Fine relief so land is not a flat wash, and ice toward the poles.
        base *= 0.82 + 0.36 * valueNoise(direction * 17.0);
        base = mix(base, vec3(0.86, 0.90, 0.94), smoothstep(0.80, 0.97, abs(direction.y)) * 0.75);
        gloss = 60.0;
        glossStrength = mix(0.30, 0.04, land);   // water shines, land does not
    }

    float diffuse = max(dot(n, l), 0.0);
    float wrapped = max((dot(n, l) + 0.35) / 1.35, 0.0);
    float specular = pow(max(dot(n, h), 0.0), gloss) * glossStrength;
    float rim = pow(1.0 - max(dot(n, v), 0.0), 3.0) * 0.30;
    vec3 threw = base * (ambient + wrapped * 0.55 + diffuse * 0.55) + vec3(specular) + base * rim;
    finalColor = vec4(threw, 1.0);
}
