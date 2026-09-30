#include "syphon_output.hpp"
SyphonOutput::~SyphonOutput() = default;
bool SyphonOutput::start() { return false; }
void SyphonOutput::stop() {}
bool SyphonOutput::active() const { return false; }
void SyphonOutput::publish(unsigned, int, int) {}
void *SyphonOutput::description() const { return nullptr; }
bool SyphonOutput::supported() { return false; }
