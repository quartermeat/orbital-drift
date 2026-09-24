#version 330
// Levelling the tray: every cell gets the same number of grains.
out vec4 finalColor;
uniform float level;
void main() { finalColor=vec4(level,0.0,0.0,1.0); }
