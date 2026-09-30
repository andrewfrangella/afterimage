#include "engine.hpp"
#include "gpu_engine.hpp"
#include "modulation.hpp"
#include "syphon_output.hpp"
#include <QElapsedTimer>
#include <QOffscreenSurface>
#include <QSurfaceFormat>
#include <QtWidgets>
#include <atomic>
#include <cmath>
#include <mutex>
#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>
#include <thread>

// All capture, processing and recording run off the GUI thread.
class VideoWorker {
public:
  std::mutex mutex;
  Params params;
  QString request = "demo", recordRequest;
  bool sourcePending = true, recordPending = false, paused = false, loop = true,
       clear = false, recordingActive = false, preferGpu = true,
       syphonEnabled = false, gpuActive = false;
  QImage output;
  QString status = "Ready";
  double fps = 30, available = 0;
  std::atomic<bool> quit{false};
  std::thread thread;
  QOffscreenSurface surface;
  VideoWorker() {
    QSurfaceFormat format;
    format.setVersion(3, 3);
#ifdef __APPLE__
    format.setVersion(4, 1);
#endif
    format.setProfile(QSurfaceFormat::CoreProfile);
    surface.setFormat(format);
    surface.create();
    thread = std::thread([this] { run(); });
  }
  ~VideoWorker() {
    quit = true;
    if (thread.joinable())
      thread.join();
  }
  void run() {
    cv::VideoCapture cap;
    cv::VideoWriter writer;
    Engine engine;
    GpuEngine gpu;
    bool gpuReady = gpu.initialize(&surface), previousGpu = false;
    SyphonOutput syphon;
    QString backend = gpuReady ? gpu.renderer()
                               : "CPU (GPU unavailable: " + gpu.error() + ")";
    QString source = "demo", pendingRecording;
    double rate = 30, time = 0;
    int index = 0;
    QElapsedTimer clock;
    clock.start();
    qint64 next = 0;
    while (!quit) {
      Params p;
      QString req, rec;
      bool change = false, record = false, stop = false, repeat = true,
           reset = false, useGpu = false, publishSyphon = false;
      {
        std::lock_guard<std::mutex> l(mutex);
        p = params;
        useGpu = preferGpu && gpuReady;
        publishSyphon = syphonEnabled;
        gpuActive = useGpu;
        stop = paused;
        repeat = loop;
        reset = clear;
        clear = false;
        if (sourcePending) {
          change = true;
          req = request;
          sourcePending = false;
        }
        if (recordPending) {
          record = true;
          rec = recordRequest;
          recordPending = false;
        }
      }
      if (useGpu != previousGpu) {
        engine.reset();
        gpu.reset();
        previousGpu = useGpu;
      }
      if (!publishSyphon || !useGpu) {
        if (gpuReady)
          gpu.makeCurrent();
        syphon.stop();
      }
      if (change) {
        {
          std::lock_guard<std::mutex> l(mutex);
          recordingActive = false;
        }
        pendingRecording.clear();
        writer.release();
        cap.release();
        source = req;
        engine.reset();
        gpu.reset();
        time = 0;
        index = 0;
        rate = 30;
        clock.restart();
        next = 0;
        if (source != "demo") {
          bool ok = source.startsWith("camera:")
                        ? cap.open(source.mid(7).toInt())
                        : cap.open(source.toStdString());
          if (!ok) {
            std::lock_guard<std::mutex> l(mutex);
            status = "Cannot open source. Check path, codec, camera index and "
                     "OS permissions.";
            source = "none";
          } else {
            double f = cap.get(cv::CAP_PROP_FPS);
            if (std::isfinite(f) && f > 0 && f <= 240)
              rate = f;
          }
        }
        {
          std::lock_guard<std::mutex> l(mutex);
          fps = rate;
        }
      }
      if (reset) {
        engine.reset();
        gpu.reset();
      }
      if (record) {
        writer.release();
        pendingRecording = rec;
        std::lock_guard<std::mutex> l(mutex);
        recordingActive = !rec.isEmpty();
      }
      if (stop || source == "none") {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        continue;
      }
      if (clock.elapsed() < next) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        continue;
      }
      cv::Mat frame;
      try {
        if (source == "demo") {
          frame = cv::Mat(480, 854, CV_8UC3, cv::Scalar(12, 14, 20));
          int x = (index * 5) % 754;
          cv::rectangle(frame, {x, 140, 100, 180}, {230, 140, 55}, -1);
          cv::circle(frame, {427 + int(180 * std::sin(index * .035)), 240}, 65,
                     {50, 70, 245}, -1);
          ++index;
        } else if (!cap.read(frame)) {
          if (!source.startsWith("camera:") && repeat) {
            cap.set(cv::CAP_PROP_POS_FRAMES, 0);
            engine.reset();
            gpu.reset();
            time = 0;
            if (!cap.read(frame)) {
              source = "none";
            }
          } else
            source = "none";
          if (frame.empty()) {
            writer.release();
            std::lock_guard<std::mutex> l(mutex);
            recordingActive = false;
            status = "Source ended or disconnected. Open a source to continue.";
            continue;
          }
        }
        // Bound the working resolution so long delays remain useful on ordinary
        // laptops.
        if (frame.cols > 1280 || frame.rows > 720) {
          double scale = std::min(1280. / frame.cols, 720. / frame.rows);
          cv::resize(frame, frame, {}, scale, scale, cv::INTER_AREA);
        }
        double stamp =
            source.startsWith("camera:") ? double(clock.elapsed()) : time;
        auto processed = useGpu ? gpu.process(frame, stamp, p)
                                : engine.process(frame, stamp, p);
        QString syphonStatus;
        if (publishSyphon && useGpu) {
          gpu.makeCurrent();
          if (syphon.start()) {
            syphon.publish(gpu.outputTexture(), processed.cols, processed.rows);
            syphonStatus = "  •  Syphon: Afterimage";
          } else
            syphonStatus = "  •  Syphon could not start";
        } else if (publishSyphon)
          syphonStatus = "  •  Syphon requires GPU";
        time += 1000. / rate;
        if (!pendingRecording.isEmpty()) {
          writer.open(pendingRecording.toStdString(),
                      cv::VideoWriter::fourcc('M', 'J', 'P', 'G'), rate,
                      processed.size());
          pendingRecording.clear();
          if (!writer.isOpened())
            throw std::runtime_error("Recording failed: choose a writable .avi "
                                     "path with MJPEG support.");
        }
        if (writer.isOpened())
          writer.write(processed);
        cv::Mat rgb;
        cv::cvtColor(processed, rgb, cv::COLOR_BGR2RGB);
        QImage image(rgb.data, rgb.cols, rgb.rows, int(rgb.step),
                     QImage::Format_RGB888);
        {
          std::lock_guard<std::mutex> l(mutex);
          output = image.copy();
          available = useGpu ? gpu.availableMs() : engine.availableMs();
          status = QString("%1  •  %2 × %3  •  %4 fps  •  history %5 ms%6")
                       .arg(source)
                       .arg(frame.cols)
                       .arg(frame.rows)
                       .arg(rate, 0, 'f', 1)
                       .arg(available, 0, 'f', 0)
                       .arg(writer.isOpened() ? "  •  REC" : "") +
                   "  •  " +
                   (useGpu ? "GPU: " + backend
                           : (gpuReady ? "CPU reference" : backend)) +
                   syphonStatus;
        }
      } catch (const std::exception &e) {
        writer.release();
        source = "none";
        std::lock_guard<std::mutex> l(mutex);
        recordingActive = false;
        status = QString("Video error: %1").arg(e.what());
      }
      next = std::max(next + qint64(std::round(1000. / rate)), clock.elapsed());
    }
  }
};
class Window : public QMainWindow {
  VideoWorker worker;
  QLabel *preview, *status, *delayInfo;
  QDoubleSpinBox *delay, *trails, *gain, *mix;
  QSpinBox *soft, *strobe, *camera;
  QComboBox *mode, *units, *backendSelect;
  QSlider *delaySlider;
  double maxDelayMs = 10000;
  int historyBudgetMiB = 256, previousUnits = 0;
  ModulationPanel *modulation;
  QCheckBox *syphonToggle;
  QCheckBox *invert, *loop;
  QPushButton *record;
  QPushButton *pauseButton;
  QWidget outputWindow;
  QLabel *outputPreview;
  QTimer refresh;
  bool recording = false;

public:
  Window() {
    setWindowTitle("AFTERIMAGE — Frame Differencer");
    QSettings saved("Afterimage", "Afterimage");
    maxDelayMs = std::clamp(
        saved.value("history/maximumDelayMs", 10000).toDouble(), 1., 600000.);
    historyBudgetMiB =
        std::clamp(saved.value("history/budgetMiB", 256).toInt(), 32, 4096);
    modulation = new ModulationPanel(this);
    modulation->setWindowFlag(Qt::Window, true);
    modulation->resize(1100, 650);
    resize(1180, 800);
    auto root = new QWidget;
    setCentralWidget(root);
    auto layout = new QVBoxLayout(root);
    auto title = new QLabel("AFTERIMAGE   /   temporal video instrument");
    title->setStyleSheet(
        "font-size:22px;font-weight:600;color:#d8f48a;padding:12px");
    layout->addWidget(title);
    auto toolbar = new QHBoxLayout;
    layout->addLayout(toolbar);
    auto button = [&](QString label, auto fn) {
      auto b = new QPushButton(label);
      toolbar->addWidget(b);
      connect(b, &QPushButton::clicked, this, fn);
      return b;
    };
    button("Open video…", [this] {
      auto path = QFileDialog::getOpenFileName(this, "Open video");
      if (!path.isEmpty())
        source(path);
    });
    camera = new QSpinBox;
    camera->setRange(0, 20);
    camera->setPrefix("Camera ");
    toolbar->addWidget(camera);
    button("Connect camera",
           [this] { source("camera:" + QString::number(camera->value())); });
    button("Demo", [this] { source("demo"); });
    auto pause = button("Pause", [this] {
      std::lock_guard<std::mutex> l(worker.mutex);
      worker.paused = !worker.paused;
    });
    pause->setCheckable(true);
    pauseButton = pause;
    button("Settings…", [this] { showSettings(); });
    button("Clear history", [this] {
      std::lock_guard<std::mutex> l(worker.mutex);
      worker.clear = true;
    });
    button("Audio / sensors / mappings…", [this] {
      modulation->show();
      modulation->raise();
    });
    button("Output window", [this] { outputWindow.show(); });
    record = button("Record AVI…", [this] {
      QString path;
      if (!recording) {
        path = QFileDialog::getSaveFileName(
            this, "Record processed video (silent)", "afterimage.avi",
            "MJPEG video (*.avi)");
        if (path.isEmpty())
          return;
        if (!path.endsWith(".avi", Qt::CaseInsensitive))
          path += ".avi";
      }
      std::lock_guard<std::mutex> l(worker.mutex);
      worker.recordRequest = path;
      worker.recordPending = true;
      worker.recordingActive = !path.isEmpty();
      recording = !path.isEmpty();
      record->setText(recording ? "Stop recording" : "Record AVI…");
    });
    auto middle = new QHBoxLayout;
    layout->addLayout(middle, 1);
    preview = new QLabel("Loading demo…");
    preview->setAlignment(Qt::AlignCenter);
    preview->setMinimumSize(400, 300);
    preview->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    preview->setStyleSheet("background:#08090c;border:1px solid #30343b");
    middle->addWidget(preview, 1);
    auto controls = new QWidget;
    controls->setFixedWidth(280);
    auto form = new QFormLayout(controls);
    middle->addWidget(controls);
    mode = new QComboBox;
    mode->addItems({"Absolute difference", "Signed difference",
                    "Temporal anaglyph", "Source / bypass", "Delayed tap"});
    form->addRow("Blend", mode);
    backendSelect = new QComboBox;
    backendSelect->addItems({"GPU (startup fallback)", "CPU reference"});
    form->addRow("Processing", backendSelect);
    syphonToggle = new QCheckBox("Publish Syphon: Afterimage");
    syphonToggle->setEnabled(SyphonOutput::supported());
    form->addRow(syphonToggle);
    if (!SyphonOutput::supported())
      syphonToggle->setToolTip(
          "Syphon output is available in the macOS build.");
    delay = new QDoubleSpinBox;
    delay->setObjectName("delayControl");
    delay->setRange(0, maxDelayMs);
    delay->setValue(120);
    delay->setSingleStep(10);
    form->addRow("Delay", delay);
    delaySlider = new QSlider(Qt::Horizontal);
    delaySlider->setRange(0, int(maxDelayMs));
    delaySlider->setValue(120);
    form->addRow(delaySlider);
    connect(delaySlider, &QSlider::valueChanged, delay,
            &QDoubleSpinBox::setValue);
    connect(delay, &QDoubleSpinBox::valueChanged, delaySlider,
            [this](double v) {
              QSignalBlocker block(delaySlider);
              delaySlider->setValue(int(v));
            });
    units = new QComboBox;
    units->setObjectName("delayUnits");
    units->addItems({"milliseconds", "frames"});
    form->addRow("Units", units);
    connect(units, &QComboBox::currentIndexChanged, this, [this](int now) {
      double fps;
      {
        std::lock_guard<std::mutex> lock(worker.mutex);
        fps = worker.fps;
      }
      const double milliseconds =
          previousUnits == 0 ? delay->value() : delay->value() * 1000. / fps;
      previousUnits = now;
      updateDelayRange();
      delay->setValue(now == 0 ? milliseconds : milliseconds * fps / 1000.);
    });
    delayInfo = new QLabel;
    delayInfo->setWordWrap(true);
    form->addRow(delayInfo);
    auto number = [&](QString name, double max, double value) {
      auto s = new QDoubleSpinBox;
      s->setRange(0, max);
      s->setDecimals(2);
      s->setSingleStep(.05);
      s->setValue(value);
      form->addRow(name, s);
      return s;
    };
    trails = number("Temporal blur / trails", .99, 0);
    gain = number("Difference gain", 8, 1);
    mix = number("Wet / dry", 1, 1);
    soft = new QSpinBox;
    soft->setRange(0, 20);
    form->addRow("Spatial softness", soft);
    strobe = new QSpinBox;
    strobe->setRange(1, 60);
    form->addRow("Strobe hold (frames)", strobe);
    invert = new QCheckBox("Invert output");
    form->addRow(invert);
    loop = new QCheckBox("Loop file");
    loop->setChecked(true);
    form->addRow(loop);
    auto presets = new QComboBox;
    presets->addItems({"Choose a starting patch…", "Motion contours",
                       "Long exposure", "Red / cyan time",
                       "Stroboscopic study"});
    form->addRow("Patch", presets);
    connect(presets, &QComboBox::currentIndexChanged, this, [this](int i) {
      if (!i)
        return;
      mode->setCurrentIndex(i == 3 ? 2 : 0);
      units->setCurrentIndex(0);
      delay->setValue(i == 2 ? 500 : 120);
      trails->setValue(i == 2 ? .9 : 0);
      strobe->setValue(i == 4 ? 6 : 1);
      gain->setValue(1);
      mix->setValue(1);
      soft->setValue(0);
      invert->setChecked(false);
    });
    auto help = new QLabel(
        "IN → history tap → difference → softness → wet/dry → temporal blur → "
        "strobe\n\nTrails retain the previous output: 0 = crisp, 0.9 = long "
        "exposure. Anaglyph places current red against delayed cyan. Strobe "
        "holds images; it does not flash to black.\n\nHistory limits are set "
        "in Settings. Early or unavailable taps use the oldest retained "
        "frame.");
    help->setWordWrap(true);
    help->setStyleSheet("color:#a0a8b2;padding-top:18px");
    form->addRow(help);
    status = new QLabel;
    layout->addWidget(status);
    auto outLayout = new QVBoxLayout(&outputWindow);
    outLayout->setContentsMargins(0, 0, 0, 0);
    outputPreview = new QLabel;
    outputPreview->setAlignment(Qt::AlignCenter);
    outLayout->addWidget(outputPreview);
    outputWindow.resize(960, 540);
    outputWindow.setWindowTitle(
        "AFTERIMAGE — Output (double-click for fullscreen)");
    outputPreview->installEventFilter(this);
    setStyleSheet(
        "QWidget{background:#171a20;color:#e9edf3;font-size:13px} "
        "QPushButton,QComboBox,QSpinBox,QDoubleSpinBox{background:#272d36;"
        "padding:6px;border:1px solid #414854;border-radius:4px} "
        "QPushButton:hover{border-color:#d8f48a} QLabel{border:0}");
    connect(&refresh, &QTimer::timeout, this, [this] {
      Params p;
      p.delayMs = delay->value();
      p.maxDelayMs = maxDelayMs;
      p.historyBudgetMiB = historyBudgetMiB;
      updateDelayRange();
      QImage img;
      QString s;
      double fps, available;
      {
        std::lock_guard<std::mutex> l(worker.mutex);
        fps = worker.fps;
        available = worker.available;
        if (units->currentIndex() == 1)
          p.delayMs *= 1000. / fps;
        p.mode = Mode(mode->currentIndex());
        p.persistence = trails->value();
        p.gain = gain->value();
        p.mix = mix->value();
        p.softness = soft->value();
        p.strobe = strobe->value();
        p.invert = invert->isChecked();
        p = modulation->apply(p);
        worker.params = p;
        worker.preferGpu = backendSelect->currentIndex() == 0;
        worker.syphonEnabled = syphonToggle->isChecked();
        worker.loop = loop->isChecked();
        img = worker.output;
        s = worker.status;
        recording = worker.recordingActive;
        record->setText(recording ? "Stop recording" : "Record AVI…");
      }
      delayInfo->setText(QString("%1 ms / %2 frames%3")
                             .arg(p.delayMs, 0, 'f', 1)
                             .arg(p.delayMs * fps / 1000., 0, 'f', 1)
                             .arg(p.delayMs > available
                                      ? "\nTap warming or memory limited"
                                      : ""));
      status->setText(s);
      if (!img.isNull()) {
        preview->setPixmap(QPixmap::fromImage(img).scaled(
            preview->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
        if (outputWindow.isVisible())
          outputPreview->setPixmap(QPixmap::fromImage(img).scaled(
              outputPreview->size(), Qt::KeepAspectRatio,
              Qt::SmoothTransformation));
      }
    });
    refresh.start(33);
    auto shortcut = new QShortcut(QKeySequence(Qt::Key_Space), this);
    connect(shortcut, &QShortcut::activated, pause, &QPushButton::click);
  }
  void updateDelayRange() {
    double fps;
    {
      std::lock_guard<std::mutex> lock(worker.mutex);
      fps = worker.fps;
    }
    const double limit =
        units->currentIndex() == 0 ? maxDelayMs : maxDelayMs * fps / 1000.;
    if (std::abs(delay->maximum() - limit) > .0001) {
      delay->setMaximum(limit);
      delaySlider->setMaximum(int(std::ceil(limit)));
    }
  }
  void setHistoryLimits(double maximumMs, int budgetMiB, bool persist = true) {
    maxDelayMs = std::clamp(maximumMs, 1., 600000.);
    historyBudgetMiB = std::clamp(budgetMiB, 32, 4096);
    updateDelayRange();
    if (persist) {
      QSettings saved("Afterimage", "Afterimage");
      saved.setValue("history/maximumDelayMs", maxDelayMs);
      saved.setValue("history/budgetMiB", historyBudgetMiB);
    }
  }
  void showSettings() {
    QDialog dialog(this);
    dialog.setWindowTitle("History settings");
    auto form = new QFormLayout(&dialog);
    auto maximum = new QDoubleSpinBox;
    maximum->setObjectName("maximumDelayMs");
    maximum->setRange(1, 600000);
    maximum->setDecimals(0);
    maximum->setSuffix(" ms");
    maximum->setValue(maxDelayMs);
    form->addRow("Maximum delay", maximum);
    auto budget = new QSpinBox;
    budget->setObjectName("historyBudgetMiB");
    budget->setRange(32, 4096);
    budget->setSuffix(" MiB");
    budget->setValue(historyBudgetMiB);
    form->addRow("History memory budget", budget);
    auto note = new QLabel(
        "Maximum delay sets the knob and mapping range (up to ten minutes). "
        "Actual history is also limited by memory and incoming "
        "resolution/frame rate. At 720p/30 fps, each second needs about 79 "
        "MiB. Increasing limits fills history over time; reducing them prunes "
        "it immediately on the next frame.");
    note->setWordWrap(true);
    form->addRow(note);
    auto buttons =
        new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() == QDialog::Accepted)
      setHistoryLimits(maximum->value(), budget->value());
  }
  bool hasOutput(bool requireGpu = false) {
    std::lock_guard<std::mutex> l(worker.mutex);
    return !worker.output.isNull() && (!requireGpu || worker.gpuActive);
  }
  bool eventFilter(QObject *o, QEvent *e) override {
    if (o == outputPreview && e->type() == QEvent::MouseButtonDblClick) {
      if (outputWindow.isFullScreen())
        outputWindow.showNormal();
      else
        outputWindow.showFullScreen();
      return true;
    }
    return QMainWindow::eventFilter(o, e);
  }
  void source(QString s) {
    pauseButton->setChecked(false);
    std::lock_guard<std::mutex> l(worker.mutex);
    worker.request = s;
    worker.sourcePending = true;
    worker.paused = false;
    recording = false;
    record->setText("Record AVI…");
  }
};
int main(int argc, char **argv) {
  QApplication app(argc, argv);
  Window window;
  window.show();
  if (app.arguments().contains("--smoke-test"))
    QTimer::singleShot(1200, &app, [&] {
      app.exit(window.hasOutput(app.arguments().contains("--require-gpu")) ? 0
                                                                           : 2);
    });
  return app.exec();
}
