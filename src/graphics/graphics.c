#include "graphics/graphics.h"
#include "data/modelData.h"
#include "data/textureData.h"
#include "data/rasterizer.h"
#include "resources/shaders.h"
#include "event/event.h"
#include "math/math.h"
#include "math/mat4.h"
#include "math/vec3.h"
#include "util.h"
#include "lib/stb/stb_image.h"
#define _USE_MATH_DEFINES
#include <stdlib.h>
#include <string.h>
#include <math.h>

static GraphicsState state;

static void onCloseWindow(GLFWwindow* window) {
  if (window == state.window) {
    EventType type = EVENT_QUIT;
    EventData data = { .quit = { false, 0 } };
    Event event = { .type = type, .data = data };
    lovrEventPush(event);
  }
}

static void gammaCorrectColor(Color* color) {
  if (state.gammaCorrect) {
    color->r = lovrMathGammaToLinear(color->r);
    color->g = lovrMathGammaToLinear(color->g);
    color->b = lovrMathGammaToLinear(color->b);
  }
}

static GLenum convertCompareMode(CompareMode mode) {
  switch (mode) {
    case COMPARE_NONE: return GL_ALWAYS;
    case COMPARE_EQUAL: return GL_EQUAL;
    case COMPARE_NEQUAL: return GL_NOTEQUAL;
    case COMPARE_LESS: return GL_LESS;
    case COMPARE_LEQUAL: return GL_LEQUAL;
    case COMPARE_GREATER: return GL_GREATER;
    case COMPARE_GEQUAL: return GL_GEQUAL;
  }
}

static GLenum convertMeshDrawMode(MeshDrawMode drawMode) {
  switch (drawMode) {
    case MESH_POINTS: return GL_POINTS;
    case MESH_LINES: return GL_LINES;
    case MESH_LINE_STRIP: return GL_LINE_STRIP;
    case MESH_LINE_LOOP: return GL_LINE_LOOP;
    case MESH_TRIANGLE_STRIP: return GL_TRIANGLE_STRIP;
    case MESH_TRIANGLES: return GL_TRIANGLES;
    case MESH_TRIANGLE_FAN: return GL_TRIANGLE_FAN;
  }
}

// Base

void lovrGraphicsInit() {
  //
}

void lovrGraphicsDestroy() {
  if (!state.initialized) return;
  lovrGraphicsSetShader(NULL);
  lovrGraphicsSetFont(NULL);
  lovrGraphicsSetCanvas(NULL, 0);
  for (int i = 0; i < MAX_TEXTURES; i++) {
    lovrRelease(state.textures[i]);
  }
  for (int i = 0; i < DEFAULT_SHADER_COUNT; i++) {
    lovrRelease(state.defaultShaders[i]);
  }
  lovrRelease(state.defaultMaterial);
  lovrRelease(state.defaultFont);
  lovrRelease(state.defaultTexture);
  lovrRelease(state.mesh);
  memset(&state, 0, sizeof(GraphicsState));
}

void lovrGraphicsReset() {
  int w = lovrGraphicsGetWidth();
  int h = lovrGraphicsGetHeight();
  state.transform = 0;
  state.layer = 0;
  memcpy(state.layers[state.layer].viewport, (int[]) { 0, 0, w, h }, 4 * sizeof(uint32_t));
  mat4_perspective(state.layers[state.layer].projection, .01f, 100.f, 67 * M_PI / 180., (float) w / h);
  mat4_identity(state.layers[state.layer].view);
  lovrGraphicsSetBackgroundColor((Color) { 0, 0, 0, 1. });
  lovrGraphicsSetBlendMode(BLEND_ALPHA, BLEND_ALPHA_MULTIPLY);
  lovrGraphicsSetCanvas(NULL, 0);
  lovrGraphicsSetColor((Color) { 1., 1., 1., 1. });
  lovrGraphicsSetCullingEnabled(false);
  lovrGraphicsSetDefaultFilter((TextureFilter) { .mode = FILTER_TRILINEAR });
  lovrGraphicsSetDepthTest(COMPARE_LEQUAL, true);
  lovrGraphicsSetFont(NULL);
  lovrGraphicsSetLineWidth(1);
  lovrGraphicsSetPointSize(1);
  lovrGraphicsSetShader(NULL);
  lovrGraphicsSetStencilTest(COMPARE_NONE, 0);
  lovrGraphicsSetWinding(WINDING_COUNTERCLOCKWISE);
  lovrGraphicsSetWireframe(false);
  lovrGraphicsOrigin();
}

void lovrGraphicsClear(bool clearColor, bool clearDepth, bool clearStencil, Color color, float depth, int stencil) {
  Layer layer = state.layers[state.layer];
  Canvas* canvas = state.canvasCount > 0 ? state.canvas[0] : layer.canvas;
  gpuBindFramebuffer(canvas ? lovrCanvasGetId(canvas) : 0);

  if (clearColor) {
    gammaCorrectColor(&color);
    float c[4] = { color.r, color.g, color.b, color.a };
    glClearBufferfv(GL_COLOR, 0, c);
    for (int i = 1; i < state.canvasCount; i++) {
      glClearBufferfv(GL_COLOR, i, c);
    }
  }

  if (clearDepth) {
    glClearBufferfv(GL_DEPTH, 0, &depth);
  }

  if (clearStencil) {
    glClearBufferiv(GL_STENCIL, 0, &stencil);
  }
}

void lovrGraphicsPresent() {
  glfwSwapBuffers(state.window);
  state.stats.drawCalls = 0;
  state.stats.shaderSwitches = 0;
  gpuPresent();
}

void lovrGraphicsCreateWindow(int w, int h, bool fullscreen, int msaa, const char* title, const char* icon) {
  lovrAssert(!state.window, "Window is already created");

#ifndef EMSCRIPTEN
  if ((state.window = glfwGetCurrentContext()) == NULL) {
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
    glfwWindowHint(GLFW_SAMPLES, msaa);
    glfwWindowHint(GLFW_RESIZABLE, GL_FALSE);
    glfwWindowHint(GLFW_SRGB_CAPABLE, state.gammaCorrect);
#else
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    glfwWindowHint(GLFW_SAMPLES, msaa);
    glfwWindowHint(GLFW_SRGB_CAPABLE, state.gammaCorrect);
#endif

    GLFWmonitor* monitor = glfwGetPrimaryMonitor();
    const GLFWvidmode* mode = glfwGetVideoMode(monitor);
    if (fullscreen) {
      glfwWindowHint(GLFW_RED_BITS, mode->redBits);
      glfwWindowHint(GLFW_GREEN_BITS, mode->greenBits);
      glfwWindowHint(GLFW_BLUE_BITS, mode->blueBits);
      glfwWindowHint(GLFW_REFRESH_RATE, mode->refreshRate);
    }

    state.window = glfwCreateWindow(w ? w : mode->width, h ? h : mode->height, title, fullscreen ? monitor : NULL, NULL);
    if (!state.window) {
      glfwTerminate();
      lovrThrow("Could not create window");
    }

    if (icon) {
      GLFWimage image;
      image.pixels = stbi_load(icon, &image.width, &image.height, NULL, 3);
      glfwSetWindowIcon(state.window, 1, &image);
      free(image.pixels);
    }

    glfwMakeContextCurrent(state.window);
    glfwSetWindowCloseCallback(state.window, onCloseWindow);
#ifndef EMSCRIPTEN
  }

  glfwSwapInterval(0);
#endif
  gpuInit(state.gammaCorrect, glfwGetProcAddress);
  VertexFormat format;
  vertexFormatInit(&format);
  vertexFormatAppend(&format, "lovrPosition", ATTR_FLOAT, 3);
  vertexFormatAppend(&format, "lovrNormal", ATTR_FLOAT, 3);
  vertexFormatAppend(&format, "lovrTexCoord", ATTR_FLOAT, 2);
  state.mesh = lovrMeshCreate(64, format, MESH_TRIANGLES, MESH_STREAM);
  lovrGraphicsReset();
  state.initialized = true;
}

int lovrGraphicsGetWidth() {
  int width, height;
  glfwGetFramebufferSize(state.window, &width, &height);
  return width;
}

int lovrGraphicsGetHeight() {
  int width, height;
  glfwGetFramebufferSize(state.window, &width, &height);
  return height;
}

GraphicsStats lovrGraphicsGetStats() {
  return state.stats;
}

// State

Color lovrGraphicsGetBackgroundColor() {
  return state.backgroundColor;
}

void lovrGraphicsSetBackgroundColor(Color color) {
  state.backgroundColor = color;
  gammaCorrectColor(&color);
  glClearColor(color.r, color.g, color.b, color.a);
}

void lovrGraphicsGetBlendMode(BlendMode* mode, BlendAlphaMode* alphaMode) {
  *mode =  state.blendMode;
  *alphaMode = state.blendAlphaMode;
}

void lovrGraphicsSetBlendMode(BlendMode mode, BlendAlphaMode alphaMode) {
  GLenum srcRGB = mode == BLEND_MULTIPLY ? GL_DST_COLOR : GL_ONE;

  if (srcRGB == GL_ONE && alphaMode == BLEND_ALPHA_MULTIPLY) {
    srcRGB = GL_SRC_ALPHA;
  }

  switch (mode) {
    case BLEND_ALPHA:
      glBlendEquation(GL_FUNC_ADD);
      glBlendFuncSeparate(srcRGB, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
      break;

    case BLEND_ADD:
      glBlendEquation(GL_FUNC_ADD);
      glBlendFuncSeparate(srcRGB, GL_ONE, GL_ZERO, GL_ONE);
      break;

    case BLEND_SUBTRACT:
      glBlendEquation(GL_FUNC_REVERSE_SUBTRACT);
      glBlendFuncSeparate(srcRGB, GL_ONE, GL_ZERO, GL_ONE);
      break;

    case BLEND_MULTIPLY:
      glBlendEquation(GL_FUNC_ADD);
      glBlendFuncSeparate(srcRGB, GL_ZERO, GL_DST_COLOR, GL_ZERO);
      break;

    case BLEND_LIGHTEN:
      glBlendEquation(GL_MAX);
      glBlendFuncSeparate(srcRGB, GL_ZERO, GL_ONE, GL_ZERO);
      break;

    case BLEND_DARKEN:
      glBlendEquation(GL_MIN);
      glBlendFuncSeparate(srcRGB, GL_ZERO, GL_ONE, GL_ZERO);
      break;

    case BLEND_SCREEN:
      glBlendEquation(GL_FUNC_ADD);
      glBlendFuncSeparate(srcRGB, GL_ONE_MINUS_SRC_COLOR, GL_ONE, GL_ONE_MINUS_SRC_COLOR);
      break;

    case BLEND_REPLACE:
      glBlendEquation(GL_FUNC_ADD);
      glBlendFuncSeparate(srcRGB, GL_ZERO, GL_ONE, GL_ZERO);
      break;
  }
}

void lovrGraphicsGetCanvas(Canvas** canvas, int* count) {
  *count = state.canvasCount;
  memcpy(canvas, state.canvas, state.canvasCount * sizeof(Canvas*));
}

void lovrGraphicsSetCanvas(Canvas** canvas, int count) {
  if (count == state.canvasCount && !memcmp(state.canvas, canvas, count * sizeof(Canvas*))) {
    return;
  }

  lovrAssert(count <= MAX_CANVASES, "Attempt to simultaneously render to %d canvases (the maximum is %d)", count, MAX_CANVASES);

  if (state.canvasCount > 0) {
    lovrCanvasResolve(state.canvas[0]);
  }

  for (int i = 0; i < count; i++) {
    lovrRetain(canvas[i]);
  }

  for (int i = 0; i < state.canvasCount; i++) {
    lovrRelease(state.canvas[i]);
  }

  if (count > 0) {
    memcpy(state.canvas, canvas, count * sizeof(Canvas*));
    gpuBindFramebuffer(lovrCanvasGetId(canvas[0]));

    GLenum buffers[MAX_CANVASES];
    for (int i = 0; i < count; i++) {
      buffers[i] = GL_COLOR_ATTACHMENT0 + i;
      glFramebufferTexture2D(GL_FRAMEBUFFER, buffers[i], GL_TEXTURE_2D, lovrTextureGetId((Texture*) canvas[i]), 0);
    }
    glDrawBuffers(count, buffers);

    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    lovrAssert(status != GL_FRAMEBUFFER_INCOMPLETE_DIMENSIONS, "All multicanvas canvases must have the same dimensions");
    lovrAssert(status == GL_FRAMEBUFFER_COMPLETE, "Unable to bind framebuffer");
  }

  state.canvasCount = count;
}

Color lovrGraphicsGetColor() {
  return state.color;
}

void lovrGraphicsSetColor(Color color) {
  state.color = color;
}

bool lovrGraphicsIsCullingEnabled() {
  return state.culling;
}

void lovrGraphicsSetCullingEnabled(bool culling) {
  if (culling != state.culling) {
    state.culling = culling;
    if (culling) {
      glEnable(GL_CULL_FACE);
    } else {
      glDisable(GL_CULL_FACE);
    }
  }
}

TextureFilter lovrGraphicsGetDefaultFilter() {
  return state.defaultFilter;
}

void lovrGraphicsSetDefaultFilter(TextureFilter filter) {
  state.defaultFilter = filter;
}

void lovrGraphicsGetDepthTest(CompareMode* mode, bool* write) {
  *mode = state.depthTest;
  *write = state.depthWrite;
}

void lovrGraphicsSetDepthTest(CompareMode mode, bool write) {
  if (state.depthTest != mode) {
    state.depthTest = mode;
    if (mode != COMPARE_NONE) {
      glDepthFunc(convertCompareMode(mode));
      glEnable(GL_DEPTH_TEST);
    } else {
      glDisable(GL_DEPTH_TEST);
    }
  }

  if (state.depthWrite != write) {
    state.depthWrite = write;
    glDepthMask(write);
  }
}

Font* lovrGraphicsGetFont() {
  if (!state.font) {
    if (!state.defaultFont) {
      Rasterizer* rasterizer = lovrRasterizerCreate(NULL, 32);
      state.defaultFont = lovrFontCreate(rasterizer);
      lovrRelease(rasterizer);
    }

    lovrGraphicsSetFont(state.defaultFont);
  }

  return state.font;
}

void lovrGraphicsSetFont(Font* font) {
  lovrRetain(font);
  lovrRelease(state.font);
  state.font = font;
}

bool lovrGraphicsIsGammaCorrect() {
  return state.gammaCorrect;
}

void lovrGraphicsSetGammaCorrect(bool gammaCorrect) {
  state.gammaCorrect = gammaCorrect;
}

GraphicsLimits lovrGraphicsGetLimits() {
  if (!state.limits.initialized) {
#ifdef EMSCRIPTEN
    glGetFloatv(GL_ALIASED_POINT_SIZE_RANGE, state.limits.pointSizes);
#else
    glGetFloatv(GL_POINT_SIZE_RANGE, state.limits.pointSizes);
#endif
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &state.limits.textureSize);
    glGetIntegerv(GL_MAX_SAMPLES, &state.limits.textureMSAA);
    glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &state.limits.textureAnisotropy);
    state.limits.initialized = 1;
  }

  return state.limits;
}

float lovrGraphicsGetLineWidth() {
  return state.lineWidth;
}

void lovrGraphicsSetLineWidth(float width) {
  state.lineWidth = width;
  glLineWidth(width);
}

float lovrGraphicsGetPointSize() {
  return state.pointSize;
}

void lovrGraphicsSetPointSize(float size) {
  state.pointSize = size;
}

Shader* lovrGraphicsGetShader() {
  return state.shader;
}

void lovrGraphicsSetShader(Shader* shader) {
  if (shader != state.shader) {
    lovrRetain(shader);
    lovrRelease(state.shader);
    state.shader = shader;
  }
}

void lovrGraphicsGetStencilTest(CompareMode* mode, int* value) {
  *mode = state.stencilMode;
  *value = state.stencilValue;
}

void lovrGraphicsSetStencilTest(CompareMode mode, int value) {
  state.stencilMode = mode;
  state.stencilValue = value;

  if (state.stencilWriting) {
    return;
  }

  if (mode != COMPARE_NONE) {
    if (!state.stencilEnabled) {
      glEnable(GL_STENCIL_TEST);
      state.stencilEnabled = true;
    }

    GLenum glMode = GL_ALWAYS;
    switch (mode) {
      case COMPARE_EQUAL: glMode = GL_EQUAL; break;
      case COMPARE_NEQUAL: glMode = GL_NOTEQUAL; break;
      case COMPARE_LESS: glMode = GL_GREATER; break;
      case COMPARE_LEQUAL: glMode = GL_GEQUAL; break;
      case COMPARE_GREATER: glMode = GL_LESS; break;
      case COMPARE_GEQUAL: glMode = GL_LEQUAL; break;
      default: break;
    }

    glStencilFunc(glMode, value, 0xff);
    glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
  } else if (state.stencilEnabled) {
    glDisable(GL_STENCIL_TEST);
    state.stencilEnabled = false;
  }
}

Winding lovrGraphicsGetWinding() {
  return state.winding;
}

void lovrGraphicsSetWinding(Winding winding) {
  if (winding != state.winding) {
    state.winding = winding;
    GLenum glWinding = winding == WINDING_CLOCKWISE ? GL_CW : GL_CCW;
    glFrontFace(glWinding);
  }
}

bool lovrGraphicsIsWireframe() {
  return state.wireframe;
}

void lovrGraphicsSetWireframe(bool wireframe) {
#ifndef EMSCRIPTEN
  if (state.wireframe != wireframe) {
    state.wireframe = wireframe;
    glPolygonMode(GL_FRONT_AND_BACK, wireframe ? GL_LINE : GL_FILL);
  }
#endif
}

// Transforms

void lovrGraphicsPush() {
  if (++state.transform >= MAX_TRANSFORMS) {
    lovrThrow("Unbalanced matrix stack (more pushes than pops?)");
  }

  memcpy(state.transforms[state.transform], state.transforms[state.transform - 1], 2 * 16 * sizeof(float));
}

void lovrGraphicsPop() {
  if (--state.transform < 0) {
    lovrThrow("Unbalanced matrix stack (more pops than pushes?)");
  }
}

void lovrGraphicsOrigin() {
  mat4_identity(state.transforms[state.transform]);
}

void lovrGraphicsTranslate(float x, float y, float z) {
  mat4_translate(state.transforms[state.transform], x, y, z);
}

void lovrGraphicsRotate(float angle, float ax, float ay, float az) {
  mat4_rotate(state.transforms[state.transform], angle, ax, ay, az);
}

void lovrGraphicsScale(float x, float y, float z) {
  mat4_scale(state.transforms[state.transform], x, y, z);
}

void lovrGraphicsMatrixTransform(mat4 transform) {
  mat4_multiply(state.transforms[state.transform], transform);
}

// Primitives

VertexPointer lovrGraphicsGetVertexPointer(uint32_t count) {
  lovrMeshResize(state.mesh, count);
  return lovrMeshMapVertices(state.mesh, 0, count, false, true);
}

void lovrGraphicsPoints(uint32_t count) {
  lovrMeshSetDrawMode(state.mesh, MESH_POINTS);
  lovrMeshSetDrawRange(state.mesh, 0, count);
  lovrMeshSetMaterial(state.mesh, NULL);
  lovrMeshWriteIndices(state.mesh, 0, 0);
  lovrGraphicsDraw(state.mesh, NULL, SHADER_DEFAULT, 1);
}

void lovrGraphicsLine(uint32_t count) {
  lovrMeshSetDrawMode(state.mesh, MESH_LINE_STRIP);
  lovrMeshSetDrawRange(state.mesh, 0, count);
  lovrMeshSetMaterial(state.mesh, NULL);
  lovrMeshWriteIndices(state.mesh, 0, 0);
  lovrGraphicsDraw(state.mesh, NULL, SHADER_DEFAULT, 1);
}

void lovrGraphicsTriangle(DrawMode mode, Material* material, float points[9]) {
  if (mode == DRAW_MODE_LINE) {
    VertexPointer vertexPointer = lovrGraphicsGetVertexPointer(3);

    float vertices[24] = {
      points[0], points[1], points[2], 0, 0, 0, 0, 0,
      points[3], points[4], points[5], 0, 0, 0, 0, 0,
      points[6], points[7], points[8], 0, 0, 0, 0, 0
    };

    memcpy(vertexPointer.raw, vertices, 3 * 8 * sizeof(float));
    lovrMeshSetDrawMode(state.mesh, MESH_LINE_LOOP);
  } else {
    float normal[3];
    vec3_cross(vec3_init(normal, &points[0]), &points[3]);

    float vertices[24] = {
      points[0], points[1], points[2], normal[0], normal[1], normal[2], 0, 0,
      points[3], points[4], points[5], normal[0], normal[1], normal[2], 0, 0,
      points[6], points[7], points[8], normal[0], normal[1], normal[2], 0, 0
    };

    VertexPointer vertexPointer = lovrGraphicsGetVertexPointer(3);
    memcpy(vertexPointer.raw, vertices, 3 * 8 * sizeof(float));
    lovrMeshSetDrawMode(state.mesh, MESH_TRIANGLES);
  }

  lovrMeshSetDrawRange(state.mesh, 0, 3);
  lovrMeshSetMaterial(state.mesh, material);
  lovrGraphicsDraw(state.mesh, NULL, SHADER_DEFAULT, 1);
}

void lovrGraphicsPlane(DrawMode mode, Material* material, mat4 transform) {
  if (mode == DRAW_MODE_LINE) {
    float vertices[] = {
      -.5, .5, 0,  0, 0, 0, 0, 0,
      .5, .5, 0,   0, 0, 0, 0, 0,
      .5, -.5, 0,  0, 0, 0, 0, 0,
      -.5, -.5, 0, 0, 0, 0, 0, 0
    };

    VertexPointer vertexPointer = lovrGraphicsGetVertexPointer(4);
    memcpy(vertexPointer.raw, vertices, 4 * 8 * sizeof(float));
    lovrMeshSetDrawMode(state.mesh, MESH_LINE_LOOP);
  } else if (mode == DRAW_MODE_FILL) {
    float vertices[] = {
      -.5, .5, 0,  0, 0, -1, 0, 1,
      -.5, -.5, 0, 0, 0, -1, 0, 0,
      .5, .5, 0,   0, 0, -1, 1, 1,
      .5, -.5, 0,  0, 0, -1, 1, 0
    };

    VertexPointer vertexPointer = lovrGraphicsGetVertexPointer(4);
    memcpy(vertexPointer.raw, vertices, 4 * 8 * sizeof(float));
    lovrMeshSetDrawMode(state.mesh, MESH_TRIANGLE_STRIP);
  }

  lovrMeshSetMaterial(state.mesh, material);
  lovrMeshWriteIndices(state.mesh, 0, 0);
  lovrMeshSetDrawRange(state.mesh, 0, 4);
  lovrGraphicsDraw(state.mesh, transform, SHADER_DEFAULT, 1);
}

void lovrGraphicsBox(DrawMode mode, Material* material, mat4 transform) {
  if (mode == DRAW_MODE_LINE) {
    float vertices[] = {
      // Front
      -.5, .5, -.5,  0, 0, 0, 0, 0,
      .5, .5, -.5,   0, 0, 0, 0, 0,
      .5, -.5, -.5,  0, 0, 0, 0, 0,
      -.5, -.5, -.5, 0, 0, 0, 0, 0,

      // Back
      -.5, .5, .5,   0, 0, 0, 0, 0,
      .5, .5, .5,    0, 0, 0, 0, 0,
      .5, -.5, .5,   0, 0, 0, 0, 0,
      -.5, -.5, .5,  0, 0, 0, 0, 0
    };

    uint16_t indices[] = {
      0, 1, 1, 2, 2, 3, 3, 0, // Front
      4, 5, 5, 6, 6, 7, 7, 4, // Back
      0, 4, 1, 5, 2, 6, 3, 7  // Connections
    };

    VertexPointer vertexPointer = lovrGraphicsGetVertexPointer(8);
    memcpy(vertexPointer.raw, vertices, 8 * 8 * sizeof(float));
    IndexPointer indexPointer = lovrMeshWriteIndices(state.mesh, 24, sizeof(uint16_t));
    memcpy(indexPointer.shorts, indices, 24 * sizeof(uint16_t));
    lovrMeshSetDrawMode(state.mesh, MESH_LINES);
    lovrMeshSetDrawRange(state.mesh, 0, 24);
  } else {
    float vertices[] = {
      // Front
      -.5, -.5, -.5,  0, 0, -1, 0, 0,
      -.5, .5, -.5,   0, 0, -1, 0, 1,
      .5, -.5, -.5,   0, 0, -1, 1, 0,
      .5, .5, -.5,    0, 0, -1, 1, 1,

      // Right
      .5, .5, -.5,    1, 0, 0,  0, 1,
      .5, .5, .5,     1, 0, 0,  1, 1,
      .5, -.5, -.5,   1, 0, 0,  0, 0,
      .5, -.5, .5,    1, 0, 0,  1, 0,

      // Back
      .5, -.5, .5,    0, 0, 1,  0, 0,
      .5, .5, .5,     0, 0, 1,  0, 1,
      -.5, -.5, .5,   0, 0, 1,  1, 0,
      -.5, .5, .5,    0, 0, 1,  1, 1,

      // Left
      -.5, .5, .5,   -1, 0, 0,  0, 1,
      -.5, .5, -.5,  -1, 0, 0,  1, 1,
      -.5, -.5, .5,  -1, 0, 0,  0, 0,
      -.5, -.5, -.5, -1, 0, 0,  1, 0,

      // Bottom
      -.5, -.5, -.5,  0, -1, 0, 0, 0,
      .5, -.5, -.5,   0, -1, 0, 1, 0,
      -.5, -.5, .5,   0, -1, 0, 0, 1,
      .5, -.5, .5,    0, -1, 0, 1, 1,

      // Adjust
      .5, -.5, .5,    0, 1, 0,  0, 1,
      -.5, .5, -.5,   0, 1, 0,  0, 1,

      // Top
      -.5, .5, -.5,   0, 1, 0,  0, 1,
      -.5, .5, .5,    0, 1, 0,  0, 0,
      .5, .5, -.5,    0, 1, 0,  1, 1,
      .5, .5, .5,     0, 1, 0,  1, 0
    };

    VertexPointer vertexPointer = lovrGraphicsGetVertexPointer(26);
    memcpy(vertexPointer.floats, vertices, 26 * 8 * sizeof(float));
    lovrMeshWriteIndices(state.mesh, 0, 0);
    lovrMeshSetDrawMode(state.mesh, MESH_TRIANGLE_STRIP);
    lovrMeshSetDrawRange(state.mesh, 0, 26);
  }

  lovrMeshSetMaterial(state.mesh, material);
  lovrGraphicsDraw(state.mesh, transform, SHADER_DEFAULT, 1);
}

void lovrGraphicsArc(DrawMode mode, ArcMode arcMode, Material* material, mat4 transform, float theta1, float theta2, int segments) {
  if (fabsf(theta1 - theta2) >= 2 * M_PI) {
    theta1 = 0;
    theta2 = 2 * M_PI;
  }

  bool hasCenterPoint = arcMode == ARC_MODE_PIE && fabsf(theta1 - theta2) < 2 * M_PI;
  uint32_t count = segments + 1 + hasCenterPoint;

  VertexPointer vertices = lovrGraphicsGetVertexPointer(count);

  if (hasCenterPoint) {
    *vertices.floats++ = 0;
    *vertices.floats++ = 0;
    *vertices.floats++ = 0;
    *vertices.floats++ = 0;
    *vertices.floats++ = 0;
    *vertices.floats++ = 1;
    *vertices.floats++ = .5;
    *vertices.floats++ = .5;
  }

  float theta = theta1;
  float angleShift = (theta2 - theta1) / (float) segments;

  for (int i = 0; i <= segments; i++) {
    float x = cos(theta) * .5;
    float y = sin(theta) * .5;

    *vertices.floats++ = x;
    *vertices.floats++ = y;
    *vertices.floats++ = 0;
    *vertices.floats++ = 0;
    *vertices.floats++ = 0;
    *vertices.floats++ = 1;
    *vertices.floats++ = x + .5;
    *vertices.floats++ = 1 - (y + .5);

    theta += angleShift;
  }

  lovrMeshSetMaterial(state.mesh, material);
  lovrMeshSetDrawMode(state.mesh, mode == DRAW_MODE_LINE ? (arcMode == ARC_MODE_OPEN ? MESH_LINE_STRIP : MESH_LINE_LOOP) : MESH_TRIANGLE_FAN);
  lovrMeshSetDrawRange(state.mesh, 0, count);
  lovrGraphicsDraw(state.mesh, transform, SHADER_DEFAULT, 1);
}

void lovrGraphicsCircle(DrawMode mode, Material* material, mat4 transform, int segments) {
  lovrGraphicsArc(mode, ARC_MODE_OPEN, material, transform, 0, 2 * M_PI, segments);
}

void lovrGraphicsCylinder(Material* material, float x1, float y1, float z1, float x2, float y2, float z2, float r1, float r2, bool capped, int segments) {
  float axis[3] = { x1 - x2, y1 - y2, z1 - z2 };
  float n[3] = { x1 - x2, y1 - y2, z1 - z2 };
  float p[3];
  float q[3];

  uint32_t vertexCount = ((capped && r1) * (segments + 2) + (capped && r2) * (segments + 2) + 2 * (segments + 1));
  uint32_t indexCount = 3 * segments * ((capped && r1) + (capped && r2) + 2);

  VertexPointer vertices = lovrGraphicsGetVertexPointer(vertexCount);
  IndexPointer indices = lovrMeshWriteIndices(state.mesh, indexCount, sizeof(uint32_t));
  float* baseVertex = vertices.floats;

  vec3_init(p, n);

  if (n[0] == 0 && n[2] == 0) {
    p[0] += 1;
  } else {
    p[1] += 1;
  }

  vec3_init(q, p);
  vec3_cross(q, n);
  vec3_cross(n, q);
  vec3_init(p, n);
  vec3_normalize(p);
  vec3_normalize(q);
  vec3_normalize(axis);

#define PUSH_CYLINDER_VERTEX(x, y, z, nx, ny, nz) \
  *vertices.floats++ = x; \
  *vertices.floats++ = y; \
  *vertices.floats++ = z; \
  *vertices.floats++ = nx; \
  *vertices.floats++ = ny; \
  *vertices.floats++ = nz; \
  *vertices.floats++ = 0; \
  *vertices.floats++ = 0;
#define PUSH_CYLINDER_TRIANGLE(i1, i2, i3) \
  *indices.ints++ = i1; \
  *indices.ints++ = i2; \
  *indices.ints++ = i3; \

  // Ring
  for (int i = 0; i <= segments; i++) {
    float theta = i * (2 * M_PI) / segments;
    n[0] = cos(theta) * p[0] + sin(theta) * q[0];
    n[1] = cos(theta) * p[1] + sin(theta) * q[1];
    n[2] = cos(theta) * p[2] + sin(theta) * q[2];
    PUSH_CYLINDER_VERTEX(x1 + r1 * n[0], y1 + r1 * n[1], z1 + r1 * n[2], n[0], n[1], n[2]);
    PUSH_CYLINDER_VERTEX(x2 + r2 * n[0], y2 + r2 * n[1], z2 + r2 * n[2], n[0], n[1], n[2]);
  }

  // Top
  int topOffset = (segments + 1) * 2;
  if (capped && r1 != 0) {
    PUSH_CYLINDER_VERTEX(x1, y1, z1, axis[0], axis[1], axis[2]);
    for (int i = 0; i <= segments; i++) {
      int j = i * 2 * 8;
      PUSH_CYLINDER_VERTEX(baseVertex[j + 0], baseVertex[j + 1], baseVertex[j + 2], axis[0], axis[1], axis[2]);
    }
  }

  // Bottom
  int bottomOffset = (segments + 1) * 2 + (1 + segments + 1) * (capped && r1 != 0);
  if (capped && r2 != 0) {
    PUSH_CYLINDER_VERTEX(x2, y2, z1, -axis[0], -axis[1], -axis[2]);
    for (int i = 0; i <= segments; i++) {
      int j = i * 2 * 8 + 8;
      PUSH_CYLINDER_VERTEX(baseVertex[j + 0], baseVertex[j + 1], baseVertex[j + 2], -axis[0], -axis[1], -axis[2]);
    }
  }

  // Indices
  for (int i = 0; i < segments; i++) {
    int j = 2 * i;
    PUSH_CYLINDER_TRIANGLE(j, j + 1, j + 2);
    PUSH_CYLINDER_TRIANGLE(j + 1, j + 3, j + 2);

    if (capped && r1 != 0) {
      PUSH_CYLINDER_TRIANGLE(topOffset, topOffset + i + 1, topOffset + i + 2);
    }

    if (capped && r2 != 0) {
      PUSH_CYLINDER_TRIANGLE(bottomOffset, bottomOffset + i + 1, bottomOffset + i + 2);
    }
  }
#undef PUSH_CYLINDER_VERTEX
#undef PUSH_CYLINDER_TRIANGLE

  lovrMeshSetMaterial(state.mesh, material);
  lovrMeshSetDrawMode(state.mesh, MESH_TRIANGLES);
  lovrMeshSetDrawRange(state.mesh, 0, indexCount);
  lovrGraphicsDraw(state.mesh, NULL, SHADER_DEFAULT, 1);
}

void lovrGraphicsSphere(Material* material, mat4 transform, int segments) {
  VertexPointer vertices = lovrGraphicsGetVertexPointer((segments + 1) * (segments + 1));
  IndexPointer indices = lovrMeshWriteIndices(state.mesh, segments * segments * 6, sizeof(uint32_t));

  for (int i = 0; i <= segments; i++) {
    float v = i / (float) segments;
    for (int j = 0; j <= segments; j++) {
      float u = j / (float) segments;

      float x = sin(u * 2 * M_PI) * sin(v * M_PI);
      float y = cos(v * M_PI);
      float z = -cos(u * 2 * M_PI) * sin(v * M_PI);

      *vertices.floats++ = x;
      *vertices.floats++ = y;
      *vertices.floats++ = z;
      *vertices.floats++ = x;
      *vertices.floats++ = y;
      *vertices.floats++ = z;
      *vertices.floats++ = u;
      *vertices.floats++ = 1 - v;
    }
  }

  for (int i = 0; i < segments; i++) {
    unsigned int offset0 = i * (segments + 1);
    unsigned int offset1 = (i + 1) * (segments + 1);
    for (int j = 0; j < segments; j++) {
      unsigned int index0 = offset0 + j;
      unsigned int index1 = offset1 + j;
      *indices.ints++ = index0;
      *indices.ints++ = index1;
      *indices.ints++ = index0 + 1;
      *indices.ints++ = index1;
      *indices.ints++ = index1 + 1;
      *indices.ints++ = index0 + 1;
    }
  }

  lovrMeshSetDrawMode(state.mesh, GL_TRIANGLES);
  lovrMeshSetDrawRange(state.mesh, 0, segments * segments * 6);
  lovrMeshSetMaterial(state.mesh, material);
  lovrGraphicsDraw(state.mesh, transform, SHADER_DEFAULT, 1);
}

void lovrGraphicsSkybox(Texture* texture, float angle, float ax, float ay, float az) {
  float vertices[] = {
    -1, 1, 1,  0, 0, 0, 0, 0,
    -1, -1, 1, 0, 0, 0, 0, 0,
    1, 1, 1,   0, 0, 0, 0, 0,
    1, -1, 1,  0, 0, 0, 0, 0
  };

  TextureType type = lovrTextureGetType(texture);
  lovrAssert(type == TEXTURE_CUBE || type == TEXTURE_2D, "Only 2D and cube textures can be used as skyboxes");
  MaterialTexture materialTexture = type == TEXTURE_CUBE ? TEXTURE_ENVIRONMENT_MAP : TEXTURE_DIFFUSE;
  DefaultShader shader = type == TEXTURE_CUBE ? SHADER_CUBE : SHADER_PANO;
  Winding winding = state.winding;
  lovrGraphicsSetWinding(WINDING_COUNTERCLOCKWISE);
  Material* material = lovrGraphicsGetDefaultMaterial();
  lovrMaterialSetTexture(material, materialTexture, texture);
  VertexPointer vertexPointer = lovrGraphicsGetVertexPointer(4);
  memcpy(vertexPointer.raw, vertices, 4 * 8 * sizeof(float));
  lovrMeshWriteIndices(state.mesh, 0, 0);
  lovrMeshSetMaterial(state.mesh, material);
  lovrMeshSetDrawMode(state.mesh, MESH_TRIANGLE_STRIP);
  lovrMeshSetDrawRange(state.mesh, 0, 4);
  lovrGraphicsDraw(state.mesh, NULL, shader, 1);
  lovrMaterialSetTexture(material, materialTexture, NULL);
  lovrGraphicsSetWinding(winding);
}

void lovrGraphicsPrint(const char* str, mat4 transform, float wrap, HorizontalAlign halign, VerticalAlign valign) {
  Font* font = lovrGraphicsGetFont();
  float scale = 1 / font->pixelDensity;
  float offsety;
  uint32_t vertexCount;
  uint32_t maxVertices = strlen(str) * 6;
  VertexPointer vertexPointer = lovrGraphicsGetVertexPointer(maxVertices);
  lovrFontRender(font, str, wrap, halign, valign, vertexPointer, &offsety, &vertexCount);
  lovrMeshWriteIndices(state.mesh, 0, 0);

  lovrGraphicsPush();
  lovrGraphicsMatrixTransform(transform);
  lovrGraphicsScale(scale, scale, scale);
  lovrGraphicsTranslate(0, offsety, 0);
  Material* material = lovrGraphicsGetDefaultMaterial();
  lovrMaterialSetTexture(material, TEXTURE_DIFFUSE, font->texture);
  CompareMode mode;
  bool write;
  lovrGraphicsGetDepthTest(&mode, &write);
  lovrGraphicsSetDepthTest(mode, false);
  lovrMeshSetMaterial(state.mesh, material);
  lovrMeshSetDrawMode(state.mesh, MESH_TRIANGLES);
  lovrMeshSetDrawRange(state.mesh, 0, vertexCount);
  lovrGraphicsDraw(state.mesh, NULL, SHADER_FONT, 1);
  lovrGraphicsSetDepthTest(mode, write);
  lovrMaterialSetTexture(material, TEXTURE_DIFFUSE, NULL);
  lovrGraphicsPop();
}

void lovrGraphicsStencil(StencilAction action, int replaceValue, StencilCallback callback, void* userdata) {
  CompareMode mode;
  bool write;
  lovrGraphicsGetDepthTest(&mode, &write);
  lovrGraphicsSetDepthTest(mode, false);
  glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);

  if (!state.stencilEnabled) {
    glEnable(GL_STENCIL_TEST);
    state.stencilEnabled = true;
  }

  GLenum glAction;
  switch (action) {
    case STENCIL_REPLACE: glAction = GL_REPLACE; break;
    case STENCIL_INCREMENT: glAction = GL_INCR; break;
    case STENCIL_DECREMENT: glAction = GL_DECR; break;
    case STENCIL_INCREMENT_WRAP: glAction = GL_INCR_WRAP; break;
    case STENCIL_DECREMENT_WRAP: glAction = GL_DECR_WRAP; break;
    case STENCIL_INVERT: glAction = GL_INVERT; break;
  }

  glStencilFunc(GL_ALWAYS, replaceValue, 0xff);
  glStencilOp(GL_KEEP, GL_KEEP, glAction);

  state.stencilWriting = true;
  callback(userdata);
  state.stencilWriting = false;

  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
  lovrGraphicsSetDepthTest(mode, write);
  lovrGraphicsSetStencilTest(state.stencilMode, state.stencilValue);
}

void lovrGraphicsFill(Texture* texture) {
  float vertices[] = {
    -1, 1, 0,  0, 0, 0, 0, 1,
    -1, -1, 0, 0, 0, 0, 0, 0,
    1, 1, 0,   0, 0, 0, 1, 1,
    1, -1, 0,  0, 0, 0, 1, 0
  };

  CompareMode mode;
  bool write;
  lovrGraphicsGetDepthTest(&mode, &write);
  lovrGraphicsSetDepthTest(COMPARE_NONE, false);
  Material* material = lovrGraphicsGetDefaultMaterial();
  lovrMaterialSetTexture(material, TEXTURE_DIFFUSE, texture);
  VertexPointer vertexPointer = lovrGraphicsGetVertexPointer(4);
  memcpy(vertexPointer.raw, vertices, 4 * 8 * sizeof(float));
  lovrMeshWriteIndices(state.mesh, 0, 0);
  lovrMeshSetMaterial(state.mesh, material);
  lovrMeshSetDrawMode(state.mesh, MESH_TRIANGLE_STRIP);
  lovrMeshSetDrawRange(state.mesh, 0, 4);
  lovrGraphicsDraw(state.mesh, NULL, SHADER_FILL, 1);
  lovrMaterialSetTexture(material, TEXTURE_DIFFUSE, NULL);
  lovrGraphicsSetDepthTest(mode, write);
}

// Internal
void lovrGraphicsDraw(Mesh* mesh, mat4 transform, DefaultShader defaultShader, int instances) {

  // Layer
  Layer layer = state.layers[state.layer];
  Canvas* canvas = state.canvasCount > 0 ? state.canvas[0] : layer.canvas;
  gpuBindFramebuffer(canvas ? lovrCanvasGetId(canvas) : 0);
  gpuSetViewport(state.canvasCount > 0 ?
    (uint32_t[4]) { 0, 0, lovrTextureGetWidth((Texture*) state.canvas[0]), lovrTextureGetHeight((Texture*) state.canvas[0]) } :
    layer.viewport
  );

  // Transforms
  if (transform) {
    lovrGraphicsPush();
    lovrGraphicsMatrixTransform(transform);
  }

  Shader* shader = state.shader ? state.shader : state.defaultShaders[defaultShader];

  if (!shader) {
    shader = state.defaultShaders[defaultShader] = lovrShaderCreateDefault(defaultShader);
  }

  lovrShaderSetMatrix(shader, "lovrProjection", layer.projection, 16);
  lovrShaderSetMatrix(shader, "lovrView", layer.view, 16);

  mat4 model = state.transforms[state.transform];
  lovrShaderSetMatrix(shader, "lovrModel", model, 16);

  float modelView[16];
  mat4_multiply(mat4_set(modelView, layer.view), model);
  lovrShaderSetMatrix(shader, "lovrTransform", modelView, 16);

  if (lovrShaderGetUniform(shader, "lovrNormalMatrix")) {
    if (mat4_invert(modelView)) {
      mat4_transpose(modelView);
    } else {
      mat4_identity(modelView);
    }

    float normalMatrix[9] = {
      modelView[0], modelView[1], modelView[2],
      modelView[4], modelView[5], modelView[6],
      modelView[8], modelView[9], modelView[10],
    };

    lovrShaderSetMatrix(shader, "lovrNormalMatrix", normalMatrix, 9);
  }

  // Color
  Color color = state.color;
  gammaCorrectColor(&color);
  float data[4] = { color.r, color.g, color.b, color.a };
  lovrShaderSetFloat(shader, "lovrColor", data, 4);

  // Point size
  lovrShaderSetFloat(shader, "lovrPointSize", &state.pointSize, 1);

  // Pose
  float* pose = lovrMeshGetPose(mesh);
  if (pose) {
    lovrShaderSetMatrix(shader, "lovrPose", pose, MAX_BONES * 16);
  } else {
    float identity[16];
    mat4_identity(identity);
    lovrShaderSetMatrix(shader, "lovrPose", identity, 16);
  }

  // Material
  Material* material = lovrMeshGetMaterial(mesh);

  if (!material) {
    material = lovrGraphicsGetDefaultMaterial();
  }

  for (int i = 0; i < MAX_MATERIAL_SCALARS; i++) {
    float value = lovrMaterialGetScalar(material, i);
    lovrShaderSetFloat(shader, lovrShaderScalarUniforms[i], &value, 1);
  }

  for (int i = 0; i < MAX_MATERIAL_COLORS; i++) {
    Color color = lovrMaterialGetColor(material, i);
    gammaCorrectColor(&color);
    float data[4] = { color.r, color.g, color.b, color.a };
    lovrShaderSetFloat(shader, lovrShaderColorUniforms[i], data, 4);
  }

  for (int i = 0; i < MAX_MATERIAL_TEXTURES; i++) {
    Texture* texture = lovrMaterialGetTexture(material, i);
    lovrShaderSetTexture(shader, lovrShaderTextureUniforms[i], &texture, 1);
  }

  gpuUseProgram(lovrShaderGetProgram(shader));
  lovrShaderBind(shader);
  lovrMeshBind(mesh, shader);

  uint32_t rangeStart, rangeCount;
  lovrMeshGetDrawRange(mesh, &rangeStart, &rangeCount);
  uint32_t indexCount;
  size_t indexSize;
  lovrMeshReadIndices(mesh, &indexCount, &indexSize);
  GLenum glDrawMode = convertMeshDrawMode(lovrMeshGetDrawMode(mesh));
  if (indexCount > 0) {
    size_t count = rangeCount ? rangeCount : indexCount;
    GLenum indexType = indexSize == sizeof(uint16_t) ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT;
    size_t offset = rangeStart * indexSize;
    if (instances > 1) {
      glDrawElementsInstanced(glDrawMode, count, indexType, (GLvoid*) offset, instances);
    } else {
      glDrawElements(glDrawMode, count, indexType, (GLvoid*) offset);
    }
  } else {
    size_t count = rangeCount ? rangeCount : lovrMeshGetVertexCount(mesh);
    if (instances > 1) {
      glDrawArraysInstanced(glDrawMode, rangeStart, count, instances);
    } else {
      glDrawArrays(glDrawMode, rangeStart, count);
    }
  }

  if (transform) {
    lovrGraphicsPop();
  }

  state.stats.drawCalls++;
}

void lovrGraphicsPushLayer(Canvas* canvas) {
  if (++state.layer >= MAX_LAYERS) {
    lovrThrow("Layer overflow");
  }

  memcpy(&state.layers[state.layer], &state.layers[state.layer - 1], sizeof(Layer));
  state.layers[state.layer].canvas = canvas;
}

void lovrGraphicsPopLayer() {
  if (state.canvasCount == 0 && state.layers[state.layer].canvas) {
    lovrCanvasResolve(state.layers[state.layer].canvas);
  }

  lovrAssert(--state.layer >= 0, "Layer underflow");
}

void lovrGraphicsSetCamera(mat4 projection, mat4 view) {
  mat4_set(state.layers[state.layer].projection, projection);
  mat4_set(state.layers[state.layer].view, view);
}

void lovrGraphicsSetViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height) {
  state.layers[state.layer].viewport[0] = x;
  state.layers[state.layer].viewport[1] = y;
  state.layers[state.layer].viewport[2] = width;
  state.layers[state.layer].viewport[3] = height;
}

Texture* lovrGraphicsGetTexture(int slot) {
  return state.textures[slot];
}

void lovrGraphicsBindTexture(Texture* texture, GLenum type, int slot) {
  if (!texture) {
    if (!state.defaultTexture) {
      TextureData* textureData = lovrTextureDataGetBlank(1, 1, 0xff, FORMAT_RGBA);
      state.defaultTexture = lovrTextureCreate(TEXTURE_2D, &textureData, 1, true, false);
      lovrRelease(textureData);
    }

    texture = state.defaultTexture;
  }

  if (texture != state.textures[slot]) {
    lovrRetain(texture);
    lovrRelease(state.textures[slot]);
    state.textures[slot] = texture;
    glActiveTexture(GL_TEXTURE0 + slot);
    glBindTexture(type, lovrTextureGetId(texture));
  }
}

Material* lovrGraphicsGetDefaultMaterial() {
  if (!state.defaultMaterial) {
    state.defaultMaterial = lovrMaterialCreate(true);
  }

  return state.defaultMaterial;
}
