#include "gpu_engine.hpp"
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLShaderProgram>
#include <algorithm>
#include <array>
#include <opencv2/imgproc.hpp>
#include <stdexcept>
namespace {
const char *vertex = R"(#version 330 core
void main(){ vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2); gl_Position=vec4(p*2.0-1.0,0,1); }
)";
const char *fragment = R"(#version 330 core
uniform sampler2D src, tap, prior;
uniform int pass, mode, radius, axis, invertValue, fresh;
uniform float gainValue, mixValue, persistence, weights[41];
out vec4 color;
ivec2 reflected(ivec2 p,ivec2 size){
  for(int i=0;i<2;i++){ if(size[i]<=1){p[i]=0;continue;} int period=2*size[i]-2; int q=abs(p[i])%period; p[i]=q<size[i]?q:period-q; } return p;
}
vec3 quant(vec3 v){return roundEven(clamp(v,0.0,1.0)*255.0)/255.0;}
void main(){
 ivec2 p=ivec2(gl_FragCoord.xy); vec3 a=texelFetch(src,p,0).rgb; vec3 b=texelFetch(tap,p,0).rgb; vec3 o=a;
 if(pass==0){
   if(mode==0)o=abs(a-b); else if(mode==1)o=quant(a-b+128.0/255.0); else if(mode==2)o=vec3(a.r,b.g,b.b); else if(mode==4)o=b;
   o=quant(o*gainValue);
 } else if(pass==1){
   o=vec3(0); ivec2 size=textureSize(src,0);
   for(int i=-20;i<=20;i++) if(abs(i)<=radius) {ivec2 q=p+(axis==0?ivec2(i,0):ivec2(0,i)); o+=texelFetch(src,reflected(q,size),0).rgb*weights[i+radius];}
 } else if(pass==2){
   if(invertValue!=0)a=vec3(1)-a;
   o=quant(a*mixValue+b*(1.0-mixValue));
   if(fresh==0)o=o*(1.0-persistence)+texelFetch(prior,p,0).rgb*persistence;
 } else if(pass==3)o=quant(a);
 color=vec4(o,1);
})";
} // namespace
struct GpuEngine::Impl : QOpenGLFunctions_3_3_Core {
  std::unique_ptr<QOpenGLContext> ctx;
  std::unique_ptr<QOpenGLShaderProgram> shader;
  QOffscreenSurface *surface = nullptr;
  QString error, renderer;
  unsigned vao = 0, fbo = 0;
  // input, delayed, effect, horizontal blur, vertical blur, accumulation A/B,
  // held
  std::array<unsigned, 8> tex{};
  int width = 0, height = 0, acc = 5, count = 0;
  bool accumulated = false, hasHeld = false, functionsReady = false;
  struct Sample {
    double time;
    cv::Mat frame;
  };
  std::deque<Sample> history;
  size_t bytes = 0;
  void targets(int w, int h) {
    width = w;
    height = h;
    for (int i = 0; i < 8; i++) {
      glBindTexture(GL_TEXTURE_2D, tex[i]);
      glTexImage2D(GL_TEXTURE_2D, 0,
                   (i == 3 || i == 5 || i == 6) ? GL_RGBA32F : GL_RGBA8, w, h,
                   0, GL_RGBA, GL_FLOAT, nullptr);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    }
  }
  void bind(int unit, unsigned t) {
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, t);
  }
  void draw(int dst, int source, int delayed, int previous, int pass) {
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           tex[dst], 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
      throw std::runtime_error("GPU framebuffer incomplete");
    bind(0, tex[source]);
    bind(1, tex[delayed]);
    bind(2, tex[previous]);
    shader->setUniformValue("pass", pass);
    glDrawArrays(GL_TRIANGLES, 0, 3);
  }
};
GpuEngine::GpuEngine() : d(std::make_unique<Impl>()) {}
GpuEngine::~GpuEngine() {
  if (d->ctx && d->functionsReady && makeCurrent()) {
    d->shader.reset();
    d->glDeleteTextures(8, d->tex.data());
    if (d->fbo)
      d->glDeleteFramebuffers(1, &d->fbo);
    if (d->vao)
      d->glDeleteVertexArrays(1, &d->vao);
    d->ctx->doneCurrent();
  }
}
bool GpuEngine::initialize(QOffscreenSurface *surface) {
  if (d->ctx) {
    d->error = "GPU engine already initialized";
    return false;
  }
  d->surface = surface;
  d->ctx = std::make_unique<QOpenGLContext>();
  if (!surface || !surface->isValid()) {
    d->error = "Invalid offscreen surface";
    return false;
  }
  d->ctx->setFormat(surface->format());
  if (!d->ctx->create() || !makeCurrent()) {
    d->error = "Could not create OpenGL context";
    return false;
  }
  if (!d->initializeOpenGLFunctions()) {
    d->error = "OpenGL 3.3 core functions unavailable";
    return false;
  }
  d->functionsReady = true;
  d->renderer = QString::fromLatin1(
      reinterpret_cast<const char *>(d->glGetString(GL_RENDERER)));
  d->shader = std::make_unique<QOpenGLShaderProgram>();
  if (!d->shader->addShaderFromSourceCode(QOpenGLShader::Vertex, vertex) ||
      !d->shader->addShaderFromSourceCode(QOpenGLShader::Fragment, fragment) ||
      !d->shader->link()) {
    d->error = d->shader->log();
    return false;
  }
  d->glGenVertexArrays(1, &d->vao);
  d->glGenFramebuffers(1, &d->fbo);
  d->glGenTextures(8, d->tex.data());
  d->ctx->doneCurrent();
  return true;
}
bool GpuEngine::makeCurrent() {
  return d->ctx && d->surface && d->ctx->makeCurrent(d->surface);
}
QOpenGLContext *GpuEngine::context() const { return d->ctx.get(); }
QString GpuEngine::error() const { return d->error; }
QString GpuEngine::renderer() const { return d->renderer; }
unsigned GpuEngine::outputTexture() const { return d->hasHeld ? d->tex[7] : 0; }
void GpuEngine::reset() {
  d->history.clear();
  d->bytes = 0;
  d->accumulated = false;
  d->hasHeld = false;
  d->count = 0;
}
double GpuEngine::availableMs() const {
  return d->history.size() < 2
             ? 0
             : d->history.back().time - d->history.front().time;
}
cv::Mat GpuEngine::process(const cv::Mat &input, double time, const Params &p) {
  if (input.empty() || input.type() != CV_8UC3)
    throw std::invalid_argument("Expected BGR 8-bit frame");
  if (!d->shader || !makeCurrent())
    throw std::runtime_error("GPU context unavailable");
  if (!d->history.empty() && (input.size() != d->history.back().frame.size() ||
                              time < d->history.back().time))
    reset();
  if (d->width != input.cols || d->height != input.rows)
    d->targets(input.cols, input.rows);
  d->history.push_back({time, input.clone()});
  d->bytes += input.total() * input.elemSize();
  const double maxDelay = std::clamp(p.maxDelayMs, 1., 600000.);
  const size_t budget =
      size_t(std::clamp(p.historyBudgetMiB, 32, 4096)) * 1024u * 1024u;
  while (d->history.size() > 1 &&
         (d->bytes > budget || time - d->history.front().time > maxDelay)) {
    d->bytes -=
        d->history.front().frame.total() * d->history.front().frame.elemSize();
    d->history.pop_front();
  }
  const double target = time - std::clamp(p.delayMs, 0., maxDelay);
  const cv::Mat *tap = &d->history.front().frame;
  for (auto it = d->history.rbegin(); it != d->history.rend(); ++it)
    if (it->time <= target) {
      tap = &it->frame;
      break;
    }
  d->glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  d->glPixelStorei(GL_PACK_ALIGNMENT, 1);
  for (int i = 0; i < 2; i++) {
    const cv::Mat &frame = i == 0 ? input : *tap;
    cv::Mat packed = frame.isContinuous() ? frame : frame.clone();
    d->bind(0, d->tex[i]);
    d->glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, input.cols, input.rows, GL_BGR,
                       GL_UNSIGNED_BYTE, packed.data);
  }
  d->glViewport(0, 0, input.cols, input.rows);
  d->glDisable(GL_BLEND);
  d->glDisable(GL_DITHER);
  d->glDisable(GL_DEPTH_TEST);
  d->glBindVertexArray(d->vao);
  d->shader->bind();
  d->shader->setUniformValue("src", 0);
  d->shader->setUniformValue("tap", 1);
  d->shader->setUniformValue("prior", 2);
  d->shader->setUniformValue("mode", int(p.mode));
  d->shader->setUniformValue("gainValue", float(std::clamp(p.gain, 0., 8.)));
  d->draw(2, 0, 1, 0, 0);
  int effect = 2;
  if (p.softness > 0) {
    int radius = std::clamp(p.softness, 0, 20);
    cv::Mat kernel = cv::getGaussianKernel(radius * 2 + 1, 0, CV_32F);
    d->shader->setUniformValueArray("weights", kernel.ptr<float>(),
                                    radius * 2 + 1, 1);
    d->shader->setUniformValue("radius", radius);
    d->shader->setUniformValue("axis", 0);
    d->draw(3, 2, 1, 0, 1);
    d->shader->setUniformValue("axis", 1);
    d->draw(4, 3, 1, 0, 1);
    effect = 4;
  }
  int next = d->acc == 5 ? 6 : 5;
  d->shader->setUniformValue("mixValue", float(std::clamp(p.mix, 0., 1.)));
  d->shader->setUniformValue("persistence",
                             float(std::clamp(p.persistence, 0., .99)));
  d->shader->setUniformValue("invertValue", p.invert ? 1 : 0);
  d->shader->setUniformValue("fresh", d->accumulated ? 0 : 1);
  d->draw(next, effect, 0, d->acc, 2);
  d->acc = next;
  d->accumulated = true;
  if (!d->hasHeld || d->count % std::max(1, p.strobe) == 0) {
    d->draw(7, d->acc, 0, 0, 3);
    d->hasHeld = true;
  }
  ++d->count;
  d->glBindFramebuffer(GL_FRAMEBUFFER, d->fbo);
  d->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                            d->tex[7], 0);
  cv::Mat out(input.size(), CV_8UC3);
  d->glReadPixels(0, 0, input.cols, input.rows, GL_BGR, GL_UNSIGNED_BYTE,
                  out.data);
  const unsigned error = d->glGetError();
  d->shader->release();
  d->ctx->doneCurrent();
  if (error)
    throw std::runtime_error("OpenGL processing error " +
                             std::to_string(error));
  return out;
}
