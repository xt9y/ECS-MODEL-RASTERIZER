#ifndef RW_ENGINE_RENDERER_PATHTRACER_RESTIR_SHADERS_HPP
#define RW_ENGINE_RENDERER_PATHTRACER_RESTIR_SHADERS_HPP

namespace Renderer::RestirShaders {

inline constexpr const char *common = R"GLSL(
#version 430
struct SurfaceData { vec4 position_depth; vec4 normal_material; vec4 uv_source; };
struct ReservoirData { vec4 sample_position_m; vec4 radiance_weight; };
uniform ivec2 uResolution; uniform uint uFrameIndex; uniform int uHistoryValid;
uint hashUint(uint value){value^=value>>16;value*=0x7feb352du;value^=value>>15;value*=0x846ca68bu;value^=value>>16;return value;}
float randomFloat(inout uint state){state=hashUint(state);return float(state)*(1.0/4294967296.0);}
float luminance(vec3 c){return dot(max(c,vec3(0)),vec3(0.2126,0.7152,0.0722));}
bool validSurface(SurfaceData s){return s.position_depth.w>0.0;}
bool geometryCompatible(SurfaceData a,SurfaceData b){if(!validSurface(a)||!validSurface(b))return false;float ns=dot(a.normal_material.xyz,b.normal_material.xyz);float ds=max(max(a.position_depth.w,b.position_depth.w),1.0);float pe=length(a.position_depth.xyz-b.position_depth.xyz);return ns>=0.85&&pe<=0.08*ds;}
void mergeReservoir(inout ReservoirData result,ReservoirData candidate,float scale,inout uint rng){
 float cm=candidate.sample_position_m.w;float csum=candidate.radiance_weight.w;if(cm<=0.0||csum<=0.0)return;float capped_m=min(cm,20.0);float weight=csum*scale*(capped_m/max(cm,1.0));if(weight<=0.0)return;
 float old_sum=result.radiance_weight.w;float new_sum=old_sum+weight;if(old_sum<=0.0||randomFloat(rng)<weight/max(new_sum,1.0e-8)){result.sample_position_m.xyz=candidate.sample_position_m.xyz;result.radiance_weight.rgb=candidate.radiance_weight.rgb;}
 result.sample_position_m.w=min(max(result.sample_position_m.w,0.0)+capped_m,20.0);result.radiance_weight.w=new_sum;}
)GLSL";

inline constexpr const char *temporal_reuse = R"GLSL(
layout(local_size_x=8,local_size_y=8) in;
layout(std430,binding=0) readonly buffer CurrentSurfaces{SurfaceData current_surfaces[];};
layout(std430,binding=1) readonly buffer PreviousSurfaces{SurfaceData previous_surfaces[];};
layout(std430,binding=2) readonly buffer InitialReservoirs{ReservoirData initial_reservoirs[];};
layout(std430,binding=3) readonly buffer PreviousReservoirs{ReservoirData previous_reservoirs[];};
layout(std430,binding=4) writeonly buffer TemporalReservoirs{ReservoirData temporal_reservoirs[];};
uniform mat4 uPreviousViewProjection;uniform vec3 uPreviousCameraPosition;
void main(){ivec2 pixel=ivec2(gl_GlobalInvocationID.xy);if(any(greaterThanEqual(pixel,uResolution)))return;uint index=uint(pixel.y*uResolution.x+pixel.x);SurfaceData current=current_surfaces[index];ReservoirData result=initial_reservoirs[index];
 if(uHistoryValid==0||!validSurface(current)){temporal_reservoirs[index]=result;return;}uint rng=hashUint(index*9781u^uFrameIndex*6271u^0x91e10da5u);vec4 clip=uPreviousViewProjection*vec4(current.position_depth.xyz,1.0);
 if(clip.w>1.0e-6){vec2 uv=clip.xy/clip.w*0.5+0.5;ivec2 pp=ivec2(floor(uv*vec2(uResolution)));if(all(greaterThanEqual(pp,ivec2(0)))&&all(lessThan(pp,uResolution))){uint pi=uint(pp.y*uResolution.x+pp.x);SurfaceData previous=previous_surfaces[pi];float expected=length(current.position_depth.xyz-uPreviousCameraPosition);float depth_error=abs(previous.position_depth.w-expected)/max(max(previous.position_depth.w,expected),1.0e-3);float world_error=length(previous.position_depth.xyz-current.position_depth.xyz);float tolerance=max(0.03,expected*0.03);if(validSurface(previous)&&dot(previous.normal_material.xyz,current.normal_material.xyz)>=0.85&&depth_error<=0.05&&world_error<=tolerance)mergeReservoir(result,previous_reservoirs[pi],1.0,rng);}}
 temporal_reservoirs[index]=result;}
)GLSL";

inline constexpr const char *spatial_reuse = R"GLSL(
layout(local_size_x=8,local_size_y=8) in;
layout(std430,binding=0) readonly buffer CurrentSurfaces{SurfaceData current_surfaces[];};
layout(std430,binding=1) readonly buffer TemporalReservoirs{ReservoirData temporal_reservoirs[];};
layout(std430,binding=2) writeonly buffer SpatialReservoirs{ReservoirData spatial_reservoirs[];};
const ivec2 OFFSETS[8]=ivec2[8](ivec2(1,0),ivec2(-1,0),ivec2(0,1),ivec2(0,-1),ivec2(2,1),ivec2(-2,-1),ivec2(1,-2),ivec2(-1,2));
void main(){ivec2 pixel=ivec2(gl_GlobalInvocationID.xy);if(any(greaterThanEqual(pixel,uResolution)))return;uint index=uint(pixel.y*uResolution.x+pixel.x);SurfaceData current=current_surfaces[index];ReservoirData result=temporal_reservoirs[index];if(!validSurface(current)){spatial_reservoirs[index]=result;return;}uint rng=hashUint(index*1597u^uFrameIndex*5171u^0xb5297a4du);int rotation=int(uFrameIndex&7u);
 for(int s=0;s<4;++s){ivec2 np=pixel+OFFSETS[(rotation+s*2)&7];if(any(lessThan(np,ivec2(0)))||any(greaterThanEqual(np,uResolution)))continue;uint ni=uint(np.y*uResolution.x+np.x);if(!geometryCompatible(current,current_surfaces[ni]))continue;mergeReservoir(result,temporal_reservoirs[ni],0.5,rng);}spatial_reservoirs[index]=result;}
)GLSL";

} // namespace Renderer::RestirShaders
#endif
