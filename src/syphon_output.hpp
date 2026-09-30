#pragma once
#include <QString>
// Lives on the rendering worker thread, with a current OpenGL context.
class SyphonOutput {
public:
  SyphonOutput() = default;
  ~SyphonOutput();
  SyphonOutput(const SyphonOutput &) = delete;
  SyphonOutput &operator=(const SyphonOutput &) = delete;
  bool start();
  void stop();
  bool active() const;
  void publish(unsigned texture, int width, int height);
  void *description() const;
  static bool supported();

private:
  void *server = nullptr;
};
