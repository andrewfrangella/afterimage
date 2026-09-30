#include "gpu_engine.hpp"
#include "syphon_output.hpp"
#import <OpenGL/OpenGL.h>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QOffscreenSurface>
#include <QOpenGLFunctions_3_3_Core>
#include <QSurfaceFormat>
#import <Syphon/Syphon.h>
#include <iostream>
#include <stdexcept>
#include <vector>

static void require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}
static void pump(QGuiApplication &app) {
  app.processEvents();
  [[NSRunLoop currentRunLoop]
      runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.005]];
}
int main(int argc, char **argv) {
  @autoreleasepool {
    QGuiApplication app(argc, argv);
    QSurfaceFormat format;
    format.setVersion(4, 1);
    format.setProfile(QSurfaceFormat::CoreProfile);
    QOffscreenSurface surface;
    surface.setFormat(format);
    surface.create();
    GpuEngine gpu;
    try {
      require(gpu.initialize(&surface), "GPU initialization failed");
      require(gpu.makeCurrent(), "GPU context cannot be made current");
      require(CGLGetCurrentContext() != nullptr, "No native CGL context");
      QOpenGLFunctions_3_3_Core gl;
      require(gl.initializeOpenGLFunctions(), "OpenGL functions unavailable");
      // Force directory initialization before announcement so discovery tests
      // the public server registration used by MAX and other Syphon clients.
      auto directory = [SyphonServerDirectory sharedDirectory];
      SyphonOutput output;
      require(output.start(), "Syphon output failed to start");
      auto description = (__bridge NSDictionary *)output.description();
      require(description != nil, "No Syphon server description");
      auto client = [[SyphonOpenGLClient alloc]
          initWithServerDescription:description
                            context:CGLGetCurrentContext()
                            options:nil
                    newFrameHandler:nil];
      require(client != nil && client.isValid, "Syphon client did not connect");
      constexpr int width = 16, height = 12;
      cv::Mat frame(height, width, CV_8UC3);
      frame.rowRange(0, height / 2).setTo(cv::Scalar(0, 0, 255));
      frame.rowRange(height / 2, height).setTo(cv::Scalar(255, 0, 0));
      Params params;
      params.mode = Mode::Source;
      QElapsedTimer timer;
      timer.start();
      bool received = false;
      int sequence = 0;
      while (timer.elapsed() < 5000) {
        gpu.process(frame, sequence++ * 33.0, params);
        require(gpu.makeCurrent(), "GPU context unavailable before Syphon publication");
        output.publish(gpu.outputTexture(), width, height);
        pump(app);
        if (client.hasNewFrame) {
          received = true;
          break;
        }
      }
      require(received, "No Syphon client frame received within 5 seconds");
      require(gpu.makeCurrent(), "GPU context lost after publication");
      auto image = [client newFrameImage];
      require(image != nil, "Syphon client frame image is nil");
      require(image.textureSize.width == width &&
                  image.textureSize.height == height,
              "Syphon frame dimensions differ");
      std::vector<unsigned char> pixels(width * height * 4);
      gl.glBindTexture(GL_TEXTURE_RECTANGLE, image.textureName);
      gl.glPixelStorei(GL_PACK_ALIGNMENT, 1);
      gl.glGetTexImage(GL_TEXTURE_RECTANGLE, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                       pixels.data());
      require(gl.glGetError() == GL_NO_ERROR,
              "Syphon client texture readback failed");
      for (int row : {0, height / 2, height - 1}) {
        const auto offset = row * width * 4;
        std::cout << "Client row " << row << " RGBA=" << int(pixels[offset]) << ","
                  << int(pixels[offset + 1]) << "," << int(pixels[offset + 2]) << ","
                  << int(pixels[offset + 3]) << std::endl;
      }
      // OpenGL readback begins at the bottom. flipped:YES must publish an
      // upright frame: blue at the bottom, red at the top.
      for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
          const auto offset = (y * width + x) * 4;
          require(pixels[offset] == (y < height / 2 ? 0 : 255) &&
                      pixels[offset + 1] == 0 &&
                      pixels[offset + 2] == (y < height / 2 ? 255 : 0),
                  "Syphon RGB pixels or upright orientation differ");
        }
      image = nil;
      gl.glBindTexture(GL_TEXTURE_RECTANGLE, 0);
      timer.restart();
      bool discovered = false;
      while (timer.elapsed() < 5000 && !discovered) {
        for (NSDictionary *entry in directory.servers) {
          if ([entry[SyphonServerDescriptionNameKey]
                  isEqualToString:@"Afterimage"] &&
              [entry[SyphonServerDescriptionUUIDKey]
                  isEqual:description[SyphonServerDescriptionUUIDKey]]) {
            discovered = true;
            break;
          }
        }
        if (!discovered)
          pump(app);
      }
      require(discovered,
              "Afterimage output absent from public Syphon directory");
      [client stop];
      client = nil;
      output.stop();
      require(!output.active(), "Syphon output did not stop");
      std::cout << "Syphon loopback passed: public discovery, dimensions, RGB "
                   "pixels, upright orientation\n";
      return 0;
    } catch (const std::exception &error) {
      std::cerr << error.what() << '\n';
      return 1;
    }
  }
}
