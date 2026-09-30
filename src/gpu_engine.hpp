#pragma once
#include "engine.hpp"
#include <QString>
#include <memory>
class QOffscreenSurface;
class QOpenGLContext;
// Construct, initialize, use, and destroy on the same processing thread.
// The supplied offscreen surface is created and destroyed on the GUI thread.
class GpuEngine {
public:
  GpuEngine();
  ~GpuEngine();
  bool initialize(QOffscreenSurface *surface);
  cv::Mat process(const cv::Mat &, double, const Params &);
  void reset();
  double availableMs() const;
  QString renderer() const;
  QString error() const;
  bool makeCurrent();
  QOpenGLContext *context() const;
  unsigned outputTexture() const;

private:
  struct Impl;
  std::unique_ptr<Impl> d;
};
