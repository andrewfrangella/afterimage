#include "syphon_output.hpp"
#import <OpenGL/OpenGL.h>
#import <OpenGL/gl3.h>
#import <Syphon/Syphon.h>
SyphonOutput::~SyphonOutput() { stop(); }
bool SyphonOutput::start() {
  @autoreleasepool {
    if (server)
      return true;
    auto context = CGLGetCurrentContext();
    if (!context)
      return false;
    auto object = [[SyphonOpenGLServer alloc] initWithName:@"Afterimage"
                                                   context:context
                                                   options:nil];
    if (!object)
      return false;
    server = (__bridge_retained void *)object;
    return true;
  }
}
void SyphonOutput::stop() {
  @autoreleasepool {
    if (!server)
      return;
    auto object = (__bridge_transfer SyphonOpenGLServer *)server;
    [object stop];
    server = nullptr;
  }
}
bool SyphonOutput::active() const { return server != nullptr; }
void SyphonOutput::publish(unsigned texture, int width, int height) {
  @autoreleasepool {
    if (!server || !texture)
      return;
    auto object = (__bridge SyphonOpenGLServer *)server;
    // OpenCV's top row is the texture's bottom row; communicate that
    // orientation.
    [object publishFrameTexture:texture
                  textureTarget:GL_TEXTURE_2D
                    imageRegion:NSMakeRect(0, 0, width, height)
              textureDimensions:NSMakeSize(width, height)
                        flipped:YES];
  }
}
void *SyphonOutput::description() const {
  if (!server)
    return nullptr;
  return (
      __bridge void *)[(__bridge SyphonOpenGLServer *)server serverDescription];
}
bool SyphonOutput::supported() { return true; }
