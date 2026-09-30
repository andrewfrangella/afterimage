#pragma once
#include <deque>
#include <opencv2/core.hpp>
enum class Mode { Difference, Signed, Anaglyph, Source, Delayed };
struct Params {
  double delayMs = 120, persistence = 0, gain = 1, mix = 1;
  int softness = 0, strobe = 1;
  Mode mode = Mode::Difference;
  bool invert = false;
  double maxDelayMs = 10000;
  int historyBudgetMiB = 256;
};
class Engine {
public:
  void reset();
  cv::Mat process(const cv::Mat &, double, const Params &);
  double availableMs() const;

private:
  struct Sample {
    double time;
    cv::Mat frame;
  };
  std::deque<Sample> history;
  cv::Mat accumulation, held;
  size_t bytes = 0;
  int count = 0;
};
