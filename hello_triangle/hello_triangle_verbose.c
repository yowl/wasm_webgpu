#include <assert.h>
#include <stdio.h>
#include <miniprintf.h>
#include "lib_webgpu.h"

WGpuAdapter adapter;
WGpuPresentationContext presentationContext;
WGpuDevice device;
WGpuQueue defaultQueue;
WGpuRenderPipeline renderPipeline;

EM_BOOL raf(double time, void *userData)
{
  WGpuCommandEncoder encoder = wgpu_device_create_command_encoder(device, 0);
  assert(wgpu_is_command_encoder(encoder));

  WGpuRenderPassColorAttachment colorAttachment = {};
  WGpuTexture swapChainTexture = wgpu_presentation_context_get_current_texture(presentationContext);
  assert(wgpu_is_texture(swapChainTexture));

  // Calling .getCurrentTexture() several times within a single rAF() callback
  // should return the same binding to the swap chain texture.
  WGpuTexture swapChainTexture2 = wgpu_presentation_context_get_current_texture(presentationContext);
  assert(swapChainTexture == swapChainTexture2);

  colorAttachment.view = wgpu_texture_create_view(swapChainTexture, 0);
  colorAttachment.loadColor.a = 1.0;
  assert(wgpu_is_texture_view(colorAttachment.view));

  WGpuRenderPassDescriptor passDesc = {};
  passDesc.numColorAttachments = 1;
  passDesc.colorAttachments = &colorAttachment;

  WGpuRenderPassEncoder pass = wgpu_command_encoder_begin_render_pass(encoder, &passDesc);
  assert(wgpu_is_render_pass_encoder(pass));
  wgpu_render_pass_encoder_set_pipeline(pass, renderPipeline);
  wgpu_render_pass_encoder_draw(pass, 3, 1, 0, 0);
  wgpu_render_pass_encoder_end_pass(pass);

  WGpuCommandBuffer commandBuffer = wgpu_command_encoder_finish(encoder);
  assert(wgpu_is_command_buffer(commandBuffer));

  wgpu_queue_submit_one_and_destroy(defaultQueue, commandBuffer);

  static int numLiveObjects = 0;
  int numLiveNow = wgpu_get_num_live_objects();
  if (numLiveNow != numLiveObjects)
  {
    emscripten_mini_stdio_printf("Num live WebGPU objects: %u\n", numLiveNow);
    numLiveObjects = numLiveNow;
  }

  return EM_TRUE; // This is static content, but keep rendering to debug leaking WebGPU objects above
}

void ObtainedWebGpuDevice(WGpuDevice result, void *userData)
{
  assert(userData == (void*)43);
  assert(wgpu_is_device(result));
  device = result;
  assert(wgpu_device_get_adapter(device) == adapter);
  defaultQueue = wgpu_device_get_queue(device);
  assert(wgpu_is_queue(defaultQueue));

  // TODO: read device.features and device.limits;
  // TODO: register to device.lost (undocumented?)

  presentationContext = wgpu_canvas_get_canvas_context("canvas");
  assert(presentationContext);
  assert(wgpu_is_presentation_context(presentationContext));

  WGpuPresentationConfiguration config = WGPU_PRESENTATION_CONFIGURATION_DEFAULT_INITIALIZER;
  config.device = device;
  config.format = wgpu_presentation_context_get_preferred_format(presentationContext, adapter);
  emscripten_mini_stdio_printf("Preferred swap chain format: %s\n", wgpu_enum_to_string(config.format));

  wgpu_presentation_context_configure(presentationContext, &config);

  const char *vertexShader =
    "let pos : array<vec2<f32>, 3> = array<vec2<f32>, 3>("
      "vec2<f32>(0.0, 0.5),"
      "vec2<f32>(-0.5, -0.5),"
      "vec2<f32>(0.5, -0.5)"
    ");"

    "[[stage(vertex)]]"
    "fn main([[builtin(vertex_index)]] VertexIndex : i32) -> [[builtin(position)]] vec4<f32> {"
      "return vec4<f32>(pos[VertexIndex], 0.0, 1.0);"
    "}";

  const char *fragmentShader =
    ";"
    "[[stage(fragment)]]"
    "fn main() -> [[location(0)]] vec4<f32> {"
      "return vec4<f32>(1.0, 0.0, 0.0, 1.0);"
    "}";

  WGpuShaderModuleDescriptor shaderModuleDesc = {};
  shaderModuleDesc.code = vertexShader;
  WGpuShaderModule vs = wgpu_device_create_shader_module(device, &shaderModuleDesc);
  assert(wgpu_is_shader_module(vs));

  shaderModuleDesc.code = fragmentShader;
  WGpuShaderModule fs = wgpu_device_create_shader_module(device, &shaderModuleDesc);
  assert(wgpu_is_shader_module(fs));

  WGpuRenderPipelineDescriptor renderPipelineDesc = WGPU_RENDER_PIPELINE_DESCRIPTOR_DEFAULT_INITIALIZER;
  renderPipelineDesc.vertex.module = vs;
  renderPipelineDesc.vertex.entryPoint = "main";
  renderPipelineDesc.fragment.module = fs;
  renderPipelineDesc.fragment.entryPoint = "main";

  WGpuColorTargetState colorTarget = WGPU_COLOR_TARGET_STATE_DEFAULT_INITIALIZER;
  colorTarget.format = config.format;
  renderPipelineDesc.fragment.numTargets = 1;
  renderPipelineDesc.fragment.targets = &colorTarget;

  renderPipeline = wgpu_device_create_render_pipeline(device, &renderPipelineDesc);
  assert(wgpu_is_render_pipeline(renderPipeline));

  emscripten_request_animation_frame_loop(raf, 0);
}

void ObtainedWebGpuAdapter(WGpuAdapter result, void *userData)
{
  assert(userData == (void*)42);
  assert(wgpu_is_adapter(result));

  adapter = result;

#ifndef NDEBUG
  char name[256];
  WGpuSupportedLimits limits;
  WGPU_FEATURES_BITFIELD features = wgpu_adapter_get_features(adapter);
  wgpu_adapter_get_name(adapter, name, sizeof(name));
  wgpu_adapter_get_limits(adapter, &limits);

  emscripten_mini_stdio_printf("Adapter name: %s\n", name);

#define TEST_FEATURE(x) emscripten_mini_stdio_printf("Adapter supports feature " #x ": %s\n", (features & (x) ? "yes" : "no"))
  TEST_FEATURE(WGPU_FEATURE_DEPTH_CLAMPING);
  TEST_FEATURE(WGPU_FEATURE_DEPTH24UNORM_STENCIL8);
  TEST_FEATURE(WGPU_FEATURE_DEPTH32FLOAT_STENCIL8);
  TEST_FEATURE(WGPU_FEATURE_PIPELINE_STATISTICS_QUERY);
  TEST_FEATURE(WGPU_FEATURE_TEXTURE_COMPRESSION_BC);
  TEST_FEATURE(WGPU_FEATURE_TIMESTAMP_QUERY);

#define ADAPTER_LIMIT(x) emscripten_mini_stdio_printf("Adapter limit " #x ": %u\n", limits. x);
  ADAPTER_LIMIT(maxTextureDimension1D);
  ADAPTER_LIMIT(maxTextureDimension2D);
  ADAPTER_LIMIT(maxTextureDimension3D);
  ADAPTER_LIMIT(maxTextureArrayLayers);
  ADAPTER_LIMIT(maxBindGroups);
  ADAPTER_LIMIT(maxDynamicUniformBuffersPerPipelineLayout);
  ADAPTER_LIMIT(maxDynamicStorageBuffersPerPipelineLayout);
  ADAPTER_LIMIT(maxSampledTexturesPerShaderStage);
  ADAPTER_LIMIT(maxSamplersPerShaderStage);
  ADAPTER_LIMIT(maxStorageBuffersPerShaderStage);
  ADAPTER_LIMIT(maxStorageTexturesPerShaderStage);
  ADAPTER_LIMIT(maxUniformBuffersPerShaderStage);
  ADAPTER_LIMIT(maxUniformBufferBindingSize);
  ADAPTER_LIMIT(maxStorageBufferBindingSize);
  ADAPTER_LIMIT(maxVertexBuffers);
  ADAPTER_LIMIT(maxVertexAttributes);
  ADAPTER_LIMIT(maxVertexBufferArrayStride);
#endif

  WGpuDeviceDescriptor deviceDesc = {};
  wgpu_adapter_request_device_async(adapter, &deviceDesc, ObtainedWebGpuDevice, (void*)43);
}

int main()
{
  WGpuRequestAdapterOptions options = {};
  navigator_gpu_request_adapter_async(&options, ObtainedWebGpuAdapter, (void*)42);
}
