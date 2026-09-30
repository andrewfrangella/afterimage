#include "engine.hpp"
#include <algorithm>
#include <opencv2/imgproc.hpp>
#include <stdexcept>
void Engine::reset() {
  history.clear();
  accumulation.release();
  held.release();
  bytes = 0;
  count = 0;
}
double Engine::availableMs() const {
  return history.size() < 2 ? 0 : history.back().time - history.front().time;
}
cv::Mat Engine::process(const cv::Mat &input, double time, const Params &p) {
  if (input.empty() || input.type() != CV_8UC3)
    throw std::invalid_argument("Expected BGR 8-bit frame");
  if (!history.empty() && (history.back().frame.size() != input.size() ||
                           time < history.back().time))
    reset();
  history.push_back({time, input.clone()});
  bytes += input.total() * input.elemSize();
  const double maximum = std::clamp(p.maxDelayMs, 1., 600000.);
  const size_t budget =
      size_t(std::clamp(p.historyBudgetMiB, 32, 4096)) * 1024 * 1024;
  while (history.size() > 1 &&
         (bytes > budget || time - history.front().time > maximum)) {
    bytes -= history.front().frame.total() * history.front().frame.elemSize();
    history.pop_front();
  }
  const double target = time - std::clamp(p.delayMs, 0., maximum);
  const cv::Mat *delayed = &history.front().frame;
  for (auto it = history.rbegin(); it != history.rend(); ++it)
    if (it->time <= target) {
      delayed = &it->frame;
      break;
    }
  cv::Mat out;
  switch (p.mode) {
  case Mode::Difference:
    cv::absdiff(input, *delayed, out);
    break;
  case Mode::Signed: {
    cv::Mat a, b;
    input.convertTo(a, CV_32F);
    delayed->convertTo(b, CV_32F);
    cv::Mat signedFrame = a - b + cv::Scalar::all(128);
    signedFrame.convertTo(out, CV_8U);
    break;
  }
  case Mode::Anaglyph: {
    std::vector<cv::Mat> a, b;
    cv::split(input, a);
    cv::split(*delayed, b);
    cv::merge(std::vector<cv::Mat>{b[0], b[1], a[2]}, out);
    break;
  }
  case Mode::Source:
    out = input.clone();
    break;
  case Mode::Delayed:
    out = delayed->clone();
    break;
  }
  out.convertTo(out, CV_8U, std::clamp(p.gain, 0., 8.));
  if (p.softness > 0) {
    int k = std::clamp(p.softness, 0, 20) * 2 + 1;
    cv::GaussianBlur(out, out, {k, k}, 0);
  }
  if (p.invert)
    cv::bitwise_not(out, out);
  cv::addWeighted(out, std::clamp(p.mix, 0., 1.), input,
                  1 - std::clamp(p.mix, 0., 1.), 0, out);
  cv::Mat f;
  out.convertTo(f, CV_32F);
  double persistence = std::clamp(p.persistence, 0., .99);
  if (accumulation.empty())
    accumulation = f;
  else
    cv::addWeighted(f, 1 - persistence, accumulation, persistence, 0,
                    accumulation);
  accumulation.convertTo(out, CV_8U);
  if (held.empty() || count % std::max(1, p.strobe) == 0) {
    held = out.clone();
  }
  ++count;
  return held.clone();
}
