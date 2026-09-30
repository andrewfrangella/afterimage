#include "gpu_engine.hpp"
#include <QGuiApplication>
#include <QOffscreenSurface>
#include <QSurfaceFormat>
#include <iostream>
#include <stdexcept>
int main(int argc, char **argv) {
  QGuiApplication app(argc, argv);
  QSurfaceFormat format;
  format.setVersion(3, 3);
  format.setProfile(QSurfaceFormat::CoreProfile);
#ifdef __APPLE__
  format.setVersion(4, 1);
#endif
  QOffscreenSurface surface;
  surface.setFormat(format);
  surface.create();
  GpuEngine gpu;
  if (!gpu.initialize(&surface)) {
    std::cerr << "GPU context unavailable: " << gpu.error().toStdString()
              << '\n';
    return qEnvironmentVariableIsSet("AFTERIMAGE_REQUIRE_GPU") ? 1 : 77;
  }
  std::cout << gpu.renderer().toStdString() << '\n';
  try {
    for (int mode = 0; mode < 5; ++mode)
      for (int blur : {0, 1, 2, 5, 20}) {
        Engine cpu;
        gpu.reset();
        Params p;
        p.mode = Mode(mode);
        p.softness = blur;
        p.delayMs = 47;
        p.gain = 1.3;
        p.mix = .71;
        p.persistence = .6;
        p.strobe = 3;
        p.invert = true;
        for (int n = 0; n < 12; ++n) {
          cv::Mat input(13, 17, CV_8UC3);
          for (int y = 0; y < input.rows; ++y)
            for (int x = 0; x < input.cols; ++x)
              input.at<cv::Vec3b>(y, x) =
                  cv::Vec3b((x * 31 + y * 13 + n * 17) % 256,
                            (x * 3 + y * 39 + n * 9) % 256,
                            (x * 19 + y * 7 + n * 23) % 256);
          auto expected = cpu.process(input, n * 23.5, p),
               actual = gpu.process(input, n * 23.5, p);
          if (cv::norm(expected, actual, cv::NORM_INF) > 3)
            throw std::runtime_error(
                "CPU/GPU mismatch mode=" + std::to_string(mode) + " blur=" +
                std::to_string(blur) + " frame=" + std::to_string(n) + " max=" +
                std::to_string(cv::norm(expected, actual, cv::NORM_INF)));
          if (std::abs(cpu.availableMs() - gpu.availableMs()) > .01)
            throw std::runtime_error("Delay history mismatch");
        }
      }
    // Exact byte preservation checks channels and asymmetric row orientation.
    gpu.reset();
    Params direct;
    direct.mode = Mode::Source;
    cv::Mat backing(8, 11, CV_8UC3);
    for (int y = 0; y < backing.rows; ++y)
      for (int x = 0; x < backing.cols; ++x)
        backing.at<cv::Vec3b>(y, x) = cv::Vec3b(x * 17, y * 29, x + y * 3);
    cv::Mat roi = backing(cv::Rect(2, 1, 7, 5));
    if (cv::norm(roi, gpu.process(roi, 0, direct), cv::NORM_INF) != 0)
      throw std::runtime_error(
          "Asymmetric orientation/strided upload mismatch");
    direct.mode = Mode::Difference;
    direct.delayMs = 0;
    if (cv::norm(gpu.process(roi, 23, direct), cv::NORM_INF) != 0)
      throw std::runtime_error("Zero-delay difference not black");
    gpu.process(roi, 20000, direct);
    if (gpu.availableMs() != 0)
      throw std::runtime_error("History exceeded ten-second bound");
    gpu.reset();
    Engine longCpu;
    Params longDelay;
    longDelay.maxDelayMs = 25000;
    longDelay.delayMs = 25000;
    longDelay.mode = Mode::Delayed;
    cv::Mat oldest(2, 4, CV_8UC3, cv::Scalar(17, 101, 207));
    longCpu.process(oldest, 0, longDelay);
    gpu.process(oldest, 0, longDelay);
    cv::Mat latest(2, 4, CV_8UC3, cv::Scalar(203, 51, 9));
    auto retained = gpu.process(latest, 15000, longDelay);
    auto cpuRetained = longCpu.process(latest, 15000, longDelay);
    if (gpu.availableMs() != 15000 || longCpu.availableMs() != 15000 ||
        cv::norm(retained, oldest, cv::NORM_INF) != 0 ||
        cv::norm(retained, cpuRetained, cv::NORM_INF) != 0)
      throw std::runtime_error("Configured long-delay retention/tap mismatch");
    longDelay.maxDelayMs = 1000;
    longCpu.process(latest, 15001, longDelay);
    gpu.process(latest, 15001, longDelay);
    if (gpu.availableMs() != 1 || longCpu.availableMs() != 1)
      throw std::runtime_error(
          "Reducing history limit did not prune immediately");
    Params p;
    p.mode = Mode::Source;
    gpu.reset();
    cv::Mat frame(3, 7, CV_8UC3, cv::Scalar(2, 67, 231));
    if (cv::norm(frame, gpu.process(frame, 100, p), cv::NORM_INF) != 0)
      throw std::runtime_error("Source orientation/channels");
    frame = cv::Mat(2, 4, CV_8UC3, cv::Scalar(4, 90, 13));
    if (cv::norm(frame, gpu.process(frame, 0, p), cv::NORM_INF) != 0)
      throw std::runtime_error("Resize/time reset");
    bool rejected = false;
    try {
      gpu.process(cv::Mat(), 0, p);
    } catch (const std::invalid_argument &) {
      rejected = true;
    }
    if (!rejected)
      throw std::runtime_error("Invalid input accepted");
    if (!gpu.outputTexture())
      throw std::runtime_error("No output texture");
    std::cout << "GPU effects, fractional delay taps, trails, strobe, resize, "
                 "reset, channels passed\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
