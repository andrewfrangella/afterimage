#include "modulation.hpp"
#include <QApplication>
#include <QAudioDevice>
#include <QAudioFormat>
#include <QAudioSource>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMediaDevices>
#include <QNetworkDatagram>
#include <QPermission>
#include <QPushButton>
#include <QSocketNotifier>
#include <QSpinBox>
#include <QTableWidget>
#include <QTimer>
#include <QUdpSocket>
#include <QVBoxLayout>
#include <QtEndian>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>

namespace {
bool oscString(const QByteArray &b, int &p, QString &s) {
  int end = b.indexOf('\0', p);
  if (end < 0)
    return false;
  s = QString::fromUtf8(b.constData() + p, end - p);
  int next = (end + 4) & ~3;
  if (next > b.size())
    return false;
  for (int j = end; j < next; ++j)
    if (b[j] != 0)
      return false;
  p = next;
  return true;
}
bool parseOsc(const QByteArray &b, QMap<QString, double> &out, int depth = 0) {
  if (depth > 8 || b.size() > 65536)
    return false;
  if (b.startsWith(QByteArray("#bundle\0", 8))) {
    if (b.size() < 16)
      return false;
    int p = 16;
    while (p < b.size()) {
      if (p + 4 > b.size())
        return false;
      quint32 n = qFromBigEndian<quint32>(
          reinterpret_cast<const uchar *>(b.constData() + p));
      p += 4;
      if (n == 0 || n > quint32(b.size() - p))
        return false;
      if (!parseOsc(b.mid(p, n), out, depth + 1))
        return false;
      p += int(n);
    }
    return true;
  }
  int p = 0;
  QString address, tags;
  if (!oscString(b, p, address) || !address.startsWith("/sensor/") ||
      !oscString(b, p, tags) || (tags != ",f" && tags != ",i") ||
      p + 4 != b.size())
    return false;
  quint32 bits = qFromBigEndian<quint32>(
      reinterpret_cast<const uchar *>(b.constData() + p));
  double value;
  if (tags == ",f") {
    float f;
    std::memcpy(&f, &bits, 4);
    value = f;
  } else
    value = qint32(bits);
  if (!std::isfinite(value))
    return false;
  QString name = address.mid(8);
  if (name.isEmpty())
    return false;
  out[name] = value;
  return true;
}
QStringList targets() {
  return {"delayMs",    "persistence",  "gain",         "mix",
          "softness",   "strobe",       "mode",         "invert",
          "audio.gain", "audio.attack", "audio.release"};
}
QDoubleSpinBox *spin(double lo, double hi, double value, int decimals = 3) {
  auto *s = new QDoubleSpinBox;
  s->setRange(lo, hi);
  s->setDecimals(decimals);
  s->setValue(value);
  return s;
}
} // namespace
void ModulationState::setSource(const QString &name, double value, double now) {
  if (name.isEmpty() || name.size() > 128 || !std::isfinite(value) ||
      !std::isfinite(now))
    return;
  if (!sources.contains(name) && sources.size() >= 256)
    return;
  sources[name] = {value, now};
}
bool ModulationState::ingest(const QByteArray &bytes, double now) {
  if (bytes.isEmpty() || bytes.size() > 65536)
    return false;
  QMap<QString, double> values;
  if (bytes.trimmed().startsWith('{')) {
    QJsonParseError error;
    auto doc = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject())
      return false;
    auto object = doc.object();
    for (auto i = object.begin(); i != object.end(); ++i)
      if (i.value().isDouble() && std::isfinite(i.value().toDouble()))
        values[i.key()] = i.value().toDouble();
  } else if (!parseOsc(bytes, values))
    return false;
  if (values.isEmpty())
    return false;
  for (auto i = values.begin(); i != values.end(); ++i)
    setSource(i.key(), i.value(), now);
  return true;
}
double ModulationState::modulate(const QString &target, double base, double lo,
                                 double hi, double now) {
  double value = std::clamp(base, lo, hi);
  for (auto &m : mappings) {
    if (!m.enabled || m.target != target)
      continue;
    auto s = sources.constFind(m.source);
    if (s == sources.cend() || now - s->timeMs > timeoutMs || now < s->timeMs) {
      m.lastMs = -1;
      continue;
    }
    if (!std::isfinite(m.intensity) || !std::isfinite(m.offset) ||
        !std::isfinite(m.inputMin) || !std::isfinite(m.inputMax) ||
        !std::isfinite(m.smoothingMs) || m.inputMax <= m.inputMin)
      continue;
    double normalized =
        std::clamp((s->value - m.inputMin) / (m.inputMax - m.inputMin), 0., 1.);
    if (m.lastMs < 0 || m.smoothingMs <= 0)
      m.smoothed = normalized;
    else
      m.smoothed +=
          (normalized - m.smoothed) *
          (1 - std::exp(-std::max(0., now - m.lastMs) / m.smoothingMs));
    m.lastMs = now;
    value += (m.intensity * m.smoothed + m.offset) * (hi - lo);
  }
  return std::clamp(value, lo, hi);
}
Params ModulationState::apply(Params p, double now) {
  p.delayMs = modulate("delayMs", p.delayMs, 0,
                       std::clamp(p.maxDelayMs, 1., 600000.), now);
  p.persistence = modulate("persistence", p.persistence, 0, .99, now);
  p.gain = modulate("gain", p.gain, 0, 8, now);
  p.mix = modulate("mix", p.mix, 0, 1, now);
  p.softness = int(std::round(modulate("softness", p.softness, 0, 20, now)));
  p.strobe = int(std::round(modulate("strobe", p.strobe, 1, 60, now)));
  p.mode = Mode(int(std::round(modulate("mode", int(p.mode), 0, 4, now))));
  p.invert = modulate("invert", p.invert ? 1 : 0, 0, 1, now) >= .5;
  return p;
}
double ModulationState::audioValue(const QString &target, double base,
                                   double now) {
  if (target == "audio.gain")
    return modulate(target, base, 0, 20, now);
  if (target == "audio.attack")
    return modulate(target, base, 1, 2000, now);
  if (target == "audio.release")
    return modulate(target, base, 1, 5000, now);
  return base;
}

void AudioEnvelope::reset() {
  values.fill(0);
  low = highLow = 0;
}
void AudioEnvelope::feed(const float *mono, int frames, int rate, double gain,
                         double attackMs, double releaseMs) {
  if (!mono || frames <= 0 || rate <= 0 || !std::isfinite(gain) ||
      !std::isfinite(attackMs) || !std::isfinite(releaseMs))
    return;
  double sums[4] = {};
  double al = 1 - std::exp(-2 * 3.141592653589793 * 250 / rate),
         ah = 1 - std::exp(-2 * 3.141592653589793 * 2500 / rate);
  for (int f = 0; f < frames; ++f) {
    double v = std::isfinite(mono[f]) ? mono[f] : 0;
    low += al * (v - low);
    highLow += ah * (v - highLow);
    double bands[] = {v, low, highLow - low, v - highLow};
    for (int b = 0; b < 4; ++b)
      sums[b] += bands[b] * bands[b];
  }
  double duration = 1000. * frames / rate;
  for (int b = 0; b < 4; ++b) {
    double v = std::clamp(std::sqrt(sums[b] / frames) * gain, 0., 1.);
    double tau = std::max(1., v > values[b] ? attackMs : releaseMs);
    values[b] += (v - values[b]) * (1 - std::exp(-duration / tau));
  }
}

struct ModulationPanel::Impl {
  ModulationPanel *q;
  ModulationState state;
  QElapsedTimer clock;
  QTableWidget *table;
  QLabel *status, *live;
  QComboBox *devices, *ports, *baud;
  QSpinBox *udpPort;
  QDoubleSpinBox *audioGain, *attack, *release, *timeout;
  QPushButton *audioButton;
  QUdpSocket udp;
  std::unique_ptr<QAudioSource> audio;
  QAudioFormat format;
  QByteArray pcm, serialBuffer;
  int fd = -1;
  QSocketNotifier *notifier = nullptr;
  bool droppingLine = false;
  AudioEnvelope envelope;
  QTimer timer;
  Impl(ModulationPanel *owner) : q(owner) { clock.start(); }
  ~Impl() {
    stopAudio();
    stopSerial();
  }
  double now() const { return clock.elapsed(); }
  void sync() {
    QVector<ModMapping> rows;
    for (int r = 0; r < table->rowCount(); ++r) {
      ModMapping m;
      if (r < state.mappings.size())
        m = state.mappings[r];
      m.source = static_cast<QComboBox *>(table->cellWidget(r, 0))
                     ->currentText()
                     .trimmed();
      m.target =
          static_cast<QComboBox *>(table->cellWidget(r, 1))->currentText();
      double *fields[] = {&m.intensity, &m.offset, &m.inputMin, &m.inputMax,
                          &m.smoothingMs};
      for (int c = 0; c < 5; ++c)
        *fields[c] =
            static_cast<QDoubleSpinBox *>(table->cellWidget(r, c + 2))->value();
      if (r < state.mappings.size() &&
          (m.source != state.mappings[r].source ||
           m.target != state.mappings[r].target ||
           m.inputMin != state.mappings[r].inputMin ||
           m.inputMax != state.mappings[r].inputMax))
        m.lastMs = -1;
      rows.append(m);
    }
    state.mappings = rows;
    state.timeoutMs = timeout->value();
  }
  void addRow(const ModMapping &m = {}) {
    int r = table->rowCount();
    table->insertRow(r);
    auto *source = new QComboBox;
    source->setEditable(true);
    source->addItems({"audio.rms", "audio.low", "audio.mid", "audio.high"});
    source->addItems(state.sources.keys());
    source->setCurrentText(m.source.isEmpty() ? "audio.rms" : m.source);
    table->setCellWidget(r, 0, source);
    auto *target = new QComboBox;
    target->addItems(targets());
    target->setCurrentText(m.target.isEmpty() ? "mix" : m.target);
    table->setCellWidget(r, 1, target);
    table->setCellWidget(r, 2, spin(-1, 1, m.intensity));
    table->setCellWidget(r, 3, spin(-1, 1, m.offset));
    table->setCellWidget(r, 4, spin(-1e9, 1e9, m.inputMin));
    table->setCellWidget(r, 5, spin(-1e9, 1e9, m.inputMax));
    table->setCellWidget(r, 6, spin(0, 10000, m.smoothingMs, 0));
  }
  void consumeUDP() {
    int count = 0;
    while (udp.hasPendingDatagrams() && ++count <= 128) {
      auto packet = udp.receiveDatagram(65537);
      if (packet.data().size() <= 65536)
        state.ingest(packet.data(), now());
    }
    if (udp.hasPendingDatagrams())
      QTimer::singleShot(0, q, [this] { consumeUDP(); });
  }
  void stopSerial() {
    if (notifier) {
      delete notifier;
      notifier = nullptr;
    }
    if (fd >= 0) {
      ::close(fd);
      fd = -1;
    }
    serialBuffer.clear();
    droppingLine = false;
  }
  void startSerial() {
    stopSerial();
    QString port = ports->currentText();
    fd = ::open(port.toLocal8Bit().constData(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) {
      status->setText("Serial: cannot open " + port +
                      " (check permissions/device).");
      return;
    }
    termios t{};
    if (tcgetattr(fd, &t) != 0) {
      stopSerial();
      status->setText("Serial: not a terminal device.");
      return;
    }
    cfmakeraw(&t);
    speed_t speed = B115200;
    int n = baud->currentText().toInt();
    if (n == 9600)
      speed = B9600;
    else if (n == 57600)
      speed = B57600;
    else if (n == 230400)
      speed = B230400;
    cfsetispeed(&t, speed);
    cfsetospeed(&t, speed);
    t.c_cflag |= CLOCAL | CREAD;
    t.c_cflag &= ~CSTOPB;
    t.c_cflag &= ~CRTSCTS;
    if (tcsetattr(fd, TCSANOW, &t) != 0) {
      stopSerial();
      status->setText("Serial: cannot configure.");
      return;
    }
    notifier = new QSocketNotifier(fd, QSocketNotifier::Read, q);
    QObject::connect(notifier, &QSocketNotifier::activated, q, [this] {
      char data[4096];
      ssize_t n = -1;
      int reads = 0;
      while (reads++ < 64 && (n = ::read(fd, data, sizeof(data))) > 0) {
        for (ssize_t i = 0; i < n; ++i) {
          char c = data[i];
          if (c == '\n') {
            if (!droppingLine && !serialBuffer.isEmpty())
              state.ingest(serialBuffer, now());
            serialBuffer.clear();
            droppingLine = false;
          } else if (!droppingLine) {
            serialBuffer.append(c);
            if (serialBuffer.size() > 65536) {
              serialBuffer.clear();
              droppingLine = true;
            }
          }
        }
      }
      if (n == 0) {
        stopSerial();
        status->setText("Serial disconnected.");
      }
    });
    status->setText("Serial connected: " + port);
  }
  void stopAudio() {
    if (audio) {
      audio->stop();
      audio.reset();
    }
    pcm.clear();
    envelope.reset();
    for (const QString &s :
         {"audio.rms", "audio.low", "audio.mid", "audio.high"})
      state.sources.remove(s);
    if (audioButton)
      audioButton->setText("Start audio");
  }
  void consumeAudio(QIODevice *io) {
    if (!audio)
      return;
    pcm.append(io->readAll());
    int frameBytes = format.bytesPerFrame();
    if (frameBytes <= 0)
      return;
    const int frames = pcm.size() / frameBytes;
    if (!frames)
      return;
    sync();
    double gain = state.audioValue("audio.gain", audioGain->value(), now());
    double a = state.audioValue("audio.attack", attack->value(), now()),
           r = state.audioValue("audio.release", release->value(), now());
    QVector<float> mono(frames);
    for (int f = 0; f < frames; ++f) {
      double v = 0;
      for (int c = 0; c < format.channelCount(); ++c)
        v += format.normalizedSampleValue(pcm.constData() + f * frameBytes +
                                          c * format.bytesPerSample());
      mono[f] = float(v / format.channelCount());
    }
    envelope.feed(mono.constData(), frames, format.sampleRate(), gain, a, r);
    const QString names[] = {"audio.rms", "audio.low", "audio.mid",
                             "audio.high"};
    for (int b = 0; b < 4; ++b)
      state.setSource(names[b], envelope.values[b], now());
    pcm.remove(0, frames * frameBytes);
  }
  void startAudio() {
    const QByteArray selectedId = devices->currentData().toByteArray();
    QAudioDevice dev;
    if (selectedId.isEmpty()) {
      dev = QMediaDevices::defaultAudioInput();
      if (dev.isNull()) {
        status->setText("No default audio input available. Connect an audio "
                        "device and refresh.");
        return;
      }
    } else {
      for (const auto &candidate : QMediaDevices::audioInputs()) {
        if (candidate.id() == selectedId) {
          dev = candidate;
          break;
        }
      }
      if (dev.isNull()) {
        status->setText("Selected audio device disconnected. Refresh audio "
                        "devices and choose an available input.");
        return;
      }
    }
    format = dev.preferredFormat();
    if (!format.isValid()) {
      status->setText("Unsupported audio format.");
      return;
    }
    audio = std::make_unique<QAudioSource>(dev, format, q);
    auto *io = audio->start();
    if (!io) {
      stopAudio();
      status->setText(
          "Audio input could not start; check microphone permission.");
      return;
    }
    QObject::connect(io, &QIODevice::readyRead, q,
                     [this, io] { consumeAudio(io); });
    audioButton->setText("Stop audio");
    status->setText("Audio input: " + dev.description());
  }
  void requestAudio() {
    if (audio) {
      stopAudio();
      status->setText("Audio stopped.");
      return;
    }
#ifdef Q_OS_MACOS
    QMicrophonePermission permission;
    auto result = qApp->checkPermission(permission);
    if (result == Qt::PermissionStatus::Undetermined) {
      qApp->requestPermission(permission, q, [this](const QPermission &p) {
        if (p.status() == Qt::PermissionStatus::Granted)
          startAudio();
        else
          status->setText("Microphone denied. Enable Afterimage in System "
                          "Settings > Privacy & Security > Microphone.");
      });
      return;
    }
    if (result == Qt::PermissionStatus::Denied) {
      status->setText("Enable Afterimage in System Settings > Privacy & "
                      "Security > Microphone.");
      return;
    }
#endif
    startAudio();
  }
};
ModulationPanel::ModulationPanel(QWidget *parent)
    : QWidget(parent), d(std::make_unique<Impl>(this)) {
  setWindowTitle("Afterimage — Audio / ESP32 modulation");
  resize(1080, 640);
  auto *layout = new QVBoxLayout(this);
  auto *intro = new QLabel(
      "Additive modulation: base + (intensity × normalized input + offset) × "
      "target range. Negative intensity inverts.\nAudio uses RMS envelopes; "
      "low / mid / high are broad bands. Sensor names are arbitrary. Stale "
      "inputs return to the base controls.");
  intro->setWordWrap(true);
  layout->addWidget(intro);
  auto *audioRow = new QHBoxLayout;
  d->devices = new QComboBox;
  d->devices->setObjectName("audioInput");
  d->devices->addItem("System default input — refresh to choose device",
                      QByteArray());
  auto *refreshAudio = new QPushButton("Refresh audio devices");
  connect(refreshAudio, &QPushButton::clicked, this, [this] {
    const QByteArray selectedId = d->devices->currentData().toByteArray();
    const QString selectedName = d->devices->currentText();
    d->devices->clear();
    d->devices->addItem("System default input", QByteArray());
    for (const auto &dev : QMediaDevices::audioInputs())
      d->devices->addItem(dev.description(), dev.id());
    if (!selectedId.isEmpty()) {
      int index = d->devices->findData(selectedId);
      if (index < 0) {
        d->devices->addItem(selectedName + " (disconnected)", selectedId);
        index = d->devices->count() - 1;
      }
      d->devices->setCurrentIndex(index);
    }
  });
  d->audioButton = new QPushButton("Start audio");
  audioRow->addWidget(new QLabel("Audio input"));
  audioRow->addWidget(d->devices, 1);
  audioRow->addWidget(refreshAudio);
  audioRow->addWidget(d->audioButton);
  layout->addLayout(audioRow);
  connect(d->audioButton, &QPushButton::clicked, this,
          [this] { d->requestAudio(); });
  auto *audioParams = new QHBoxLayout;
  d->audioGain = spin(0, 20, 4);
  d->attack = spin(1, 2000, 20, 0);
  d->release = spin(1, 5000, 200, 0);
  for (auto pair : {std::make_pair(QString("Sensitivity"), d->audioGain),
                    std::make_pair(QString("Attack ms"), d->attack),
                    std::make_pair(QString("Release ms"), d->release)}) {
    audioParams->addWidget(new QLabel(pair.first));
    audioParams->addWidget(pair.second);
  }
  layout->addLayout(audioParams);
  auto *wireless = new QHBoxLayout;
  d->udpPort = new QSpinBox;
  d->udpPort->setObjectName("udpPort");
  d->udpPort->setRange(1024, 65535);
  d->udpPort->setValue(9000);
  auto *listen = new QPushButton("Listen OSC / JSON");
  auto *stopUDP = new QPushButton("Stop UDP");
  wireless->addWidget(new QLabel("Wireless UDP port"));
  wireless->addWidget(d->udpPort);
  wireless->addWidget(listen);
  wireless->addWidget(stopUDP);
  wireless->addStretch();
  layout->addLayout(wireless);
  connect(listen, &QPushButton::clicked, this, [this] {
    d->udp.close();
    bool ok = d->udp.bind(QHostAddress::AnyIPv4, d->udpPort->value());
    d->status->setText(ok ? "Listening UDP on port " +
                                QString::number(d->udpPort->value())
                          : "UDP bind failed: " + d->udp.errorString());
  });
  connect(stopUDP, &QPushButton::clicked, this, [this] {
    d->udp.close();
    d->status->setText("UDP stopped.");
  });
  connect(&d->udp, &QUdpSocket::readyRead, this, [this] { d->consumeUDP(); });
  auto *serial = new QHBoxLayout;
  d->ports = new QComboBox;
  d->ports->setEditable(true);
  d->ports->setObjectName("serialPort");
  d->baud = new QComboBox;
  d->baud->addItems({"115200", "230400", "57600", "9600"});
  auto *refresh = new QPushButton("Refresh ports");
  auto *open = new QPushButton("Connect serial");
  auto *close = new QPushButton("Disconnect");
  auto refreshPorts = [this] {
    QString selected = d->ports->currentText();
    d->ports->clear();
    QDir dev("/dev");
    for (const auto &name : dev.entryList({"ttyACM*", "ttyUSB*", "cu.*"},
                                          QDir::System | QDir::Files))
      d->ports->addItem("/dev/" + name);
    if (!selected.isEmpty())
      d->ports->setCurrentText(selected);
  };
  refreshPorts();
  serial->addWidget(new QLabel("USB ESP / ESP-NOW receiver"));
  serial->addWidget(d->ports, 1);
  serial->addWidget(d->baud);
  serial->addWidget(refresh);
  serial->addWidget(open);
  serial->addWidget(close);
  layout->addLayout(serial);
  connect(refresh, &QPushButton::clicked, this, refreshPorts);
  connect(open, &QPushButton::clicked, this, [this] { d->startSerial(); });
  connect(close, &QPushButton::clicked, this, [this] {
    d->stopSerial();
    d->status->setText("Serial stopped.");
  });
  d->table = new QTableWidget(0, 7);
  d->table->setObjectName("mappingTable");
  d->table->setHorizontalHeaderLabels({"Source", "Target", "Intensity ±",
                                       "Offset ±", "Input min", "Input max",
                                       "Smooth ms"});
  d->table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
  layout->addWidget(d->table, 1);
  auto *buttons = new QHBoxLayout;
  auto *add = new QPushButton("Add mapping");
  auto *remove = new QPushButton("Remove selected");
  auto *save = new QPushButton("Save preset");
  auto *load = new QPushButton("Load preset");
  d->timeout = spin(50, 60000, 2000, 0);
  for (auto *b : {add, remove, save, load})
    buttons->addWidget(b);
  buttons->addStretch();
  buttons->addWidget(new QLabel("Stale timeout ms"));
  buttons->addWidget(d->timeout);
  layout->addLayout(buttons);
  connect(add, &QPushButton::clicked, this, [this] { d->addRow(); });
  connect(remove, &QPushButton::clicked, this, [this] {
    int r = d->table->currentRow();
    if (r >= 0) {
      d->table->removeRow(r);
      if (r < d->state.mappings.size())
        d->state.mappings.remove(r);
    }
  });
  connect(save, &QPushButton::clicked, this, [this] {
    d->sync();
    QString path = QFileDialog::getSaveFileName(this, "Save modulation preset",
                                                {}, "JSON (*.json)");
    if (path.isEmpty())
      return;
    QJsonArray rows;
    for (auto &m : d->state.mappings)
      rows.append(QJsonObject{{"source", m.source},
                              {"target", m.target},
                              {"intensity", m.intensity},
                              {"offset", m.offset},
                              {"inputMin", m.inputMin},
                              {"inputMax", m.inputMax},
                              {"smoothingMs", m.smoothingMs}});
    QJsonObject root{{"version", 1},
                     {"mappings", rows},
                     {"timeoutMs", d->timeout->value()},
                     {"audioGain", d->audioGain->value()},
                     {"attackMs", d->attack->value()},
                     {"releaseMs", d->release->value()}};
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) ||
        file.write(QJsonDocument(root).toJson()) < 0)
      d->status->setText("Preset save failed.");
    else
      d->status->setText("Preset saved.");
  });
  connect(load, &QPushButton::clicked, this, [this] {
    QString path = QFileDialog::getOpenFileName(this, "Load modulation preset",
                                                {}, "JSON (*.json)");
    if (path.isEmpty())
      return;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 1024 * 1024) {
      d->status->setText("Invalid preset file.");
      return;
    }
    QJsonParseError error;
    auto doc = QJsonDocument::fromJson(file.readAll(), &error);
    auto obj = doc.object();
    if (error.error != QJsonParseError::NoError ||
        obj.value("version").toInt() != 1 || !obj.value("mappings").isArray() ||
        obj.value("mappings").toArray().size() > 128) {
      d->status->setText("Invalid preset schema.");
      return;
    }
    QVector<ModMapping> rows;
    for (auto item : obj.value("mappings").toArray()) {
      auto o = item.toObject();
      ModMapping m;
      m.source = o.value("source").toString();
      m.target = o.value("target").toString();
      if (!targets().contains(m.target) || m.source.isEmpty()) {
        d->status->setText("Unknown preset source or target.");
        return;
      }
      m.intensity = o.value("intensity").toDouble();
      m.offset = o.value("offset").toDouble();
      m.inputMin = o.value("inputMin").toDouble();
      m.inputMax = o.value("inputMax").toDouble(1);
      m.smoothingMs = o.value("smoothingMs").toDouble();
      if (m.inputMax <= m.inputMin) {
        d->status->setText("Input max must exceed min.");
        return;
      }
      rows.append(m);
    }
    d->table->setRowCount(0);
    d->state.mappings.clear();
    for (auto &m : rows)
      d->addRow(m);
    d->timeout->setValue(obj.value("timeoutMs").toDouble(2000));
    d->audioGain->setValue(obj.value("audioGain").toDouble(4));
    d->attack->setValue(obj.value("attackMs").toDouble(20));
    d->release->setValue(obj.value("releaseMs").toDouble(200));
    d->sync();
    d->status->setText("Preset loaded; start desired inputs explicitly.");
  });
  d->status =
      new QLabel("Inputs stopped. Start audio, UDP or serial explicitly.");
  d->live = new QLabel("Live sources: none");
  d->live->setWordWrap(true);
  layout->addWidget(d->status);
  layout->addWidget(d->live);
  d->timer.setInterval(100);
  connect(&d->timer, &QTimer::timeout, this, [this] {
    QStringList values;
    for (auto i = d->state.sources.begin(); i != d->state.sources.end();) {
      if (d->now() - i->timeMs > 60000) {
        i = d->state.sources.erase(i);
        continue;
      }
      if (values.size() < 16)
        values << i.key() + " = " + QString::number(i->value, 'f', 3) +
                      (d->now() - i->timeMs > d->state.timeoutMs ? " (stale)"
                                                                 : "");
      ++i;
    }
    d->live->setText("Live sources: " +
                     (values.isEmpty() ? QString("none") : values.join("   ")));
  });
  d->timer.start();
}
ModulationPanel::~ModulationPanel() = default;
Params ModulationPanel::apply(Params base) {
  d->sync();
  return d->state.apply(base, d->now());
}
ModulationState &ModulationPanel::state() { return d->state; }
bool ModulationPanel::ingest(const QByteArray &bytes) {
  return d->state.ingest(bytes, d->now());
}
