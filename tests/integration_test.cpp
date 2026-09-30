#define main afterimage_app_main
#include "../src/main.cpp"
#undef main
#include <filesystem>
#include <iostream>
void verify(bool b, const char *m) {
  if (!b)
    throw std::runtime_error(m);
}
int main(int argc, char **argv) {
  QApplication app(argc, argv);
  QTemporaryDir temporary;
  verify(temporary.isValid(), "temporary directory");
  const auto inputPath = (temporary.path() + "/input.avi").toStdString();
  const auto outputPath = (temporary.path() + "/output.avi").toStdString();
  const char *input = inputPath.c_str();
  const char *output = outputPath.c_str();
  {
    cv::VideoWriter v(input, cv::VideoWriter::fourcc('M', 'J', 'P', 'G'), 20,
                      {160, 120});
    verify(v.isOpened(), "fixture writer");
    for (int i = 0; i < 12; i++)
      v.write(cv::Mat(120, 160, CV_8UC3, cv::Scalar(i * 15, 0, 200)));
  }
  {
    VideoWorker w;
    {
      std::lock_guard<std::mutex> l(w.mutex);
      w.paused = true;
      w.request = input;
      w.sourcePending = true;
      w.loop = false;
      w.params.mode = Mode::Source;
      w.recordRequest = output;
      w.recordPending = true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    {
      std::lock_guard<std::mutex> l(w.mutex);
      w.paused = false;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    {
      std::lock_guard<std::mutex> l(w.mutex);
      verify(w.output.size() == QSize(160, 120), "file source size");
      verify(w.status.contains("ended"), "file ended");
    }
  }
  cv::VideoCapture cap(output);
  verify(cap.isOpened(), "record file readable");
  int count = 0;
  cv::Mat f;
  while (cap.read(f)) {
    verify(f.size() == cv::Size(160, 120), "record size");
    count++;
  }
  verify(count == 12, "record all input frames");
  {
    VideoWorker w;
    {
      std::lock_guard<std::mutex> l(w.mutex);
      w.recordRequest = temporary.path() + "/missing/output.avi";
      w.recordPending = true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));
    {
      std::lock_guard<std::mutex> l(w.mutex);
      verify(w.status.contains("Recording failed"),
             "recording error stays visible");
      verify(!w.recordingActive, "recording error clears active state");
    }
  }
  {
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                       temporary.path() + "/settings");
    Window w;
    w.show();
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < 250) {
      app.processEvents();
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    auto boxes = w.findChildren<QComboBox *>();
    verify(boxes.size() >= 3, "controls populated");
    bool patched = false;
    for (auto b : boxes)
      if (b->findText("Red / cyan time") >= 0) {
        b->setCurrentIndex(b->findText("Red / cyan time"));
        patched = true;
      }
    verify(patched, "preset control");
    bool anaglyph = false;
    for (auto b : boxes)
      if (b->currentText() == "Temporal anaglyph")
        anaglyph = true;
    verify(anaglyph, "preset drives mode");
    w.setHistoryLimits(30000, 512);
    auto delayControl = w.findChild<QDoubleSpinBox *>("delayControl");
    auto delayUnits = w.findChild<QComboBox *>("delayUnits");
    verify(delayControl && delayUnits, "delay settings controls");
    verify(delayControl->maximum() == 30000, "maximum updates delay knob");
    delayControl->setValue(15000);
    delayUnits->setCurrentIndex(1);
    verify(std::abs(delayControl->value() - 450) < .1,
           "units preserve delay time");
    verify(std::abs(delayControl->maximum() - 900) < .1,
           "frame range follows maximum");
    w.setHistoryLimits(1000, 256, false);
    verify(delayControl->value() <= 30, "lowering max clamps current delay");
    QSettings settings("Afterimage", "Afterimage");
    verify(settings.value("history/maximumDelayMs").toDouble() == 30000,
           "maximum persisted");
    verify(settings.value("history/budgetMiB").toInt() == 512,
           "budget persisted");
  }
  std::cout
      << "Independent worker integration passed: file decode, paused recording "
         "request, all 12 recorded frames, preset GUI interaction\n";
}
