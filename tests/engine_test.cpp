#include "engine.hpp"
#include <iostream>
#include <stdexcept>
void check(bool b, const char *s) {
  if (!b)
    throw std::runtime_error(s);
}
int main() {
  Engine e;
  Params p;
  p.delayMs = 100;
  cv::Mat a(2, 2, CV_8UC3, cv::Scalar(200, 100, 20)),
      b(2, 2, CV_8UC3, cv::Scalar(20, 140, 200));
  e.process(a, 0, p);
  auto out = e.process(b, 100, p);
  check(out.at<cv::Vec3b>(0, 0) == cv::Vec3b(180, 40, 180),
        "absolute difference must not wrap");
  p.delayMs = 0;
  check(cv::countNonZero(e.process(b, 110, p).reshape(1)) == 0,
        "zero delay black");
  e.reset();
  p.delayMs = 100;
  e.process(a, 0, p);
  check(cv::countNonZero(e.process(a, 100, p).reshape(1)) == 0, "static black");
  e.reset();
  p.delayMs = 0;
  p.mode = Mode::Source;
  p.persistence = .5;
  e.process(a, 0, p);
  out = e.process(b, 30, p);
  check(out.at<cv::Vec3b>(0, 0) == cv::Vec3b(110, 120, 110),
        "temporal accumulation");
  e.reset();
  p.persistence = 0;
  p.mode = Mode::Anaglyph;
  p.delayMs = 100;
  e.process(a, 0, p);
  out = e.process(b, 100, p);
  check(out.at<cv::Vec3b>(0, 0) == cv::Vec3b(200, 100, 200),
        "current red and delayed cyan");
  e.reset();
  p.mode = Mode::Difference;
  e.process(a, 0, p);
  auto c = cv::Mat(4, 4, CV_8UC3, cv::Scalar(50, 50, 50));
  check(cv::countNonZero(e.process(c, 100, p).reshape(1)) == 0,
        "resolution resets history");
  std::cout << "Engine checks passed\n";
}
