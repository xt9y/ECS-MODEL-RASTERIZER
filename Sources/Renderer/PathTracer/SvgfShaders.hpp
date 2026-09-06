#ifndef RW_ENGINE_RENDERER_PATHTRACER_SVGF_SHADERS_HPP
#define RW_ENGINE_RENDERER_PATHTRACER_SVGF_SHADERS_HPP

namespace Renderer::SvgfShaders {

inline constexpr const char *common = R"GLSL(
#version 430
struct SurfaceData { vec4 position_depth; vec4 normal_material; vec4 uv_source; };
struct ReservoirData { vec4 sample_position_m; vec4 radiance_weight; };
struct LightingData { vec4 color; };
struct MomentsData { vec4 value; };
uniform int uResolutionX, uResolutionY, uHistoryValid;
#define uResolution ivec2(uResolutionX, uResolutionY)
uint pixelIndex(ivec2 p){return uint(p.y*uResolution.x+p.x);}bool inBounds(ivec2 p){return all(greaterThanEqual(p,ivec2(0)))&&all(lessThan(p,uResolution));}
bool validSurface(SurfaceData s){return s.position_depth.w>0.0;}float luminance(vec3 c){return dot(max(c,vec3(0)),vec3(0.2126,0.7152,0.0722));}
bool historyCompatible(SurfaceData current,SurfaceData previous,vec3 previous_camera_position){if(!validSurface(current)||!validSurface(previous))return false;float expected=length(current.position_depth.xyz-previous_camera_position);float de=abs(previous.position_depth.w-expected)/max(max(previous.position_depth.w,expected),1.0e-3);float ns=dot(current.normal_material.xyz,previous.normal_material.xyz);float we=length(current.position_depth.xyz-previous.position_depth.xyz);float wt=max(0.025,expected*0.025);return ns>=0.88&&de<=0.05&&we<=wt;}
)GLSL";

inline constexpr const char *compose = R"GLSL(
layout(local_size_x=8,local_size_y=8) in;
layout(std430,binding=0) readonly buffer PrimarySurfaces{SurfaceData primary_surfaces[];};
layout(std430,binding=1) readonly buffer Reservoirs{ReservoirData reservoirs[];};
layout(std430,binding=2) readonly buffer DirectLighting{LightingData direct_lighting[];};
layout(std430,binding=3) writeonly buffer CurrentLighting{LightingData current_lighting[];};
void main(){ivec2 pixel=ivec2(gl_GlobalInvocationID.xy);if(!inBounds(pixel))return;uint index=pixelIndex(pixel);SurfaceData surface=primary_surfaces[index];vec3 color=vec3(0);if(validSurface(surface)){color=max(direct_lighting[index].color.rgb,vec3(0));ReservoirData r=reservoirs[index];float m=r.sample_position_m.w;float wsum=r.radiance_weight.w;float target=max(luminance(r.radiance_weight.rgb),1.0e-6);if(m>0.0&&wsum>0.0){float normalization=clamp(wsum/max(target*m,1.0e-8),0.0,4.0);color+=max(r.radiance_weight.rgb,vec3(0))*normalization;}}current_lighting[index].color=vec4(color,1);}
)GLSL";

inline constexpr const char *temporal_filter = R"GLSL(
layout(local_size_x=8,local_size_y=8) in;
layout(std430,binding=0) readonly buffer CurrentSurfaces{SurfaceData current_surfaces[];};
layout(std430,binding=1) readonly buffer PreviousSurfaces{SurfaceData previous_surfaces[];};
layout(std430,binding=2) readonly buffer CurrentLighting{LightingData current_lighting[];};
layout(std430,binding=3) readonly buffer PreviousLighting{LightingData previous_lighting[];};
layout(std430,binding=4) readonly buffer PreviousMoments{MomentsData previous_moments[];};
layout(std430,binding=5) writeonly buffer TemporalLighting{LightingData temporal_lighting[];};
layout(std430,binding=6) writeonly buffer CurrentMoments{MomentsData current_moments[];};
uniform mat4 uPreviousViewProjection;uniform vec3 uPreviousCameraPosition;
void main(){ivec2 pixel=ivec2(gl_GlobalInvocationID.xy);if(!inBounds(pixel))return;uint index=pixelIndex(pixel);SurfaceData current_surface=current_surfaces[index];vec3 current_color=max(current_lighting[index].color.rgb,vec3(0));float current_l=luminance(current_color);vec3 nmin=current_color,nmax=current_color;
 for(int y=-1;y<=1;++y)for(int x=-1;x<=1;++x){ivec2 sp=pixel+ivec2(x,y);if(!inBounds(sp))continue;vec3 c=max(current_lighting[pixelIndex(sp)].color.rgb,vec3(0));nmin=min(nmin,c);nmax=max(nmax,c);}vec3 out_color=current_color;float m1=current_l,m2=current_l*current_l,history=1.0;
 if(uHistoryValid!=0&&validSurface(current_surface)){vec4 clip=uPreviousViewProjection*vec4(current_surface.position_depth.xyz,1);if(clip.w>1.0e-6){vec2 uv=clip.xy/clip.w*0.5+0.5;ivec2 pp=ivec2(floor(uv*vec2(uResolution)));if(inBounds(pp)){uint pi=pixelIndex(pp);SurfaceData ps=previous_surfaces[pi];if(historyCompatible(current_surface,ps,uPreviousCameraPosition)){vec3 hc=clamp(previous_lighting[pi].color.rgb,nmin,nmax);MomentsData hm=previous_moments[pi];history=min(hm.value.w+1.0,32.0);float alpha=max(1.0/history,0.06);out_color=mix(hc,current_color,alpha);m1=mix(hm.value.x,current_l,alpha);m2=mix(hm.value.y,current_l*current_l,alpha);}}}}
 float variance=max(m2-m1*m1,0.0);temporal_lighting[index].color=vec4(max(out_color,vec3(0)),1);current_moments[index].value=vec4(m1,m2,variance,history);}
)GLSL";

inline constexpr const char *atrous = R"GLSL(
layout(local_size_x=8,local_size_y=8) in;
layout(std430,binding=0) readonly buffer Surfaces{SurfaceData surfaces[];};
layout(std430,binding=1) readonly buffer InputLighting{LightingData input_lighting[];};
layout(std430,binding=2) readonly buffer Moments{MomentsData moments[];};
layout(std430,binding=3) writeonly buffer OutputLighting{LightingData output_lighting[];};
uniform int uStep;const float KERNEL[3]=float[3](1.0,2.0,1.0);
void main(){ivec2 pixel=ivec2(gl_GlobalInvocationID.xy);if(!inBounds(pixel))return;uint index=pixelIndex(pixel);SurfaceData cs=surfaces[index];vec3 cc=input_lighting[index].color.rgb;if(!validSurface(cs)){output_lighting[index].color=vec4(cc,1);return;}float cl=luminance(cc);float variance=max(moments[index].value.z,1.0e-6);float sigma=sqrt(variance)+0.02;vec3 sum=vec3(0);float wsum=0.0;
 for(int y=-1;y<=1;++y)for(int x=-1;x<=1;++x){ivec2 sp=pixel+ivec2(x,y)*max(uStep,1);if(!inBounds(sp))continue;uint si=pixelIndex(sp);SurfaceData ss=surfaces[si];if(!validSurface(ss))continue;vec3 sc=input_lighting[si].color.rgb;float nw=pow(max(dot(cs.normal_material.xyz,ss.normal_material.xyz),0.0),32.0);float ds=max(cs.position_depth.w,1.0);float dw=exp(-abs(ss.position_depth.w-cs.position_depth.w)/(0.02*ds+1.0e-4));float lw=exp(-abs(luminance(sc)-cl)/(4.0*sigma+0.02));float kw=KERNEL[abs(x)]*KERNEL[abs(y)];float w=kw*nw*dw*lw;sum+=sc*w;wsum+=w;}vec3 filtered=wsum>1.0e-6?sum/wsum:cc;output_lighting[index].color=vec4(max(filtered,vec3(0)),1);}
)GLSL";

inline constexpr const char *copy_to_image = R"GLSL(
#version 430
layout(local_size_x=8,local_size_y=8) in;
struct LightingData{vec4 color;};
layout(std430,binding=0) readonly buffer Lighting{LightingData lighting[];};
layout(rgba16f,binding=0) uniform writeonly image2D uOutput;
uniform int uResolutionX, uResolutionY;
void main(){ivec2 resolution=ivec2(uResolutionX,uResolutionY);ivec2 pixel=ivec2(gl_GlobalInvocationID.xy);if(any(greaterThanEqual(pixel,resolution)))return;uint index=uint(pixel.y*resolution.x+pixel.x);imageStore(uOutput,pixel,vec4(max(lighting[index].color.rgb,vec3(0)),1));}
)GLSL";

inline constexpr const char *present_vertex = R"GLSL(
#version 430 compatibility
out vec2 vUv;void main(){gl_Position=vec4(gl_Vertex.xy,0,1);vUv=gl_Vertex.xy*0.5+0.5;}
)GLSL";
inline constexpr const char *present_fragment = R"GLSL(
#version 430 compatibility
uniform sampler2D uOutput;uniform float uExposure;in vec2 vUv;layout(location=0)out vec4 outColor;
vec3 acesApprox(vec3 x){const float a=2.51,b=0.03,c=2.43,d=0.59,e=0.14;return clamp((x*(a*x+b))/(x*(c*x+d)+e),0.0,1.0);}void main(){vec3 linear_color=max(texture(uOutput,vUv).rgb,vec3(0))*max(uExposure,0.0);outColor=vec4(pow(acesApprox(linear_color),vec3(1.0/2.2)),1);}
)GLSL";

} // namespace Renderer::SvgfShaders
#endif
