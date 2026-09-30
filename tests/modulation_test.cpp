#include "modulation.hpp"
#include <QApplication>
#include <QComboBox>
#include <QElapsedTimer>
#include <QPushButton>
#include <QSpinBox>
#include <QUdpSocket>
#include <QtEndian>
#include <cmath>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <stdexcept>
#include <unistd.h>
#include <vector>
static void check(bool b, const char *s) {
  if (!b)
    throw std::runtime_error(s);
}
static void pump() {
  QElapsedTimer t;
  t.start();
  while (t.elapsed() < 100)
    QCoreApplication::processEvents();
}
static void click(ModulationPanel &panel, const QString &text) {
  for (auto *b : panel.findChildren<QPushButton *>())
    if (b->text() == text) {
      b->click();
      return;
    }
  throw std::runtime_error("Missing button");
}
static QByteArray osc() {
  QByteArray b("/sensor/knob\0\0\0\0", 16);
  b.append(",f\0\0", 4);
  b.append("\x3f\x00\x00\x00", 4);
  return b;
}
int main(int argc, char **argv) {
  QApplication app(argc, argv);
  ModulationState m;
  Params p;
  p.delayMs = 500;
  ModMapping row;
  row.source = "pot";
  row.target = "delayMs";
  row.intensity = .1;
  m.mappings.append(row);
  m.setSource("pot", .5, 0);
  check(std::abs(m.apply(p, 0).delayMs - 1000) < .01,
        "additive normalized mapping");
  p.maxDelayMs = 20000;
  p.delayMs = 0;
  m.mappings[0].intensity = .5;
  m.setSource("pot", 1, 0);
  check(std::abs(m.apply(p, 0).delayMs - 10000) < .01,
        "delay modulation uses configured maximum");
  p.maxDelayMs = 10000;
  p.delayMs = 500;
  m.setSource("pot", .5, 0);
  m.mappings[0].intensity = -.1;
  check(m.apply(p, 0).delayMs == 0, "attenuverter and clamp");
  check(m.apply(p, 3000).delayMs == 500, "stale restores base");
  check(m.ingest("{\"pot\":0.25,\"bad\":\"x\"}", 3100), "JSON scalar sources");
  check(m.sources["pot"].value == .25, "JSON exact value");
  auto packet = osc();
  check(m.ingest(packet, 3100), "OSC float");
  check(m.sources["knob"].value == .5, "OSC value/name");
  for (int i = 0; i < packet.size(); ++i)
    check(!m.ingest(packet.left(i), 3200), "truncated OSC rejected");
  QByteArray bundle("#bundle\0", 8);
  bundle.append(QByteArray(8, 0));
  quint32 size = qToBigEndian(quint32(packet.size()));
  bundle.append(reinterpret_cast<const char *>(&size), 4);
  bundle.append(packet);
  check(m.ingest(bundle, 3200), "OSC bundles");
  check(!m.ingest(bundle + QByteArray("x"), 3200),
        "malformed bundle rejected atomically");
  m.setSource("pot", NAN, 3300);
  check(m.sources["pot"].value == .25, "reject nan");
  m.mappings[0].target = "audio.gain";
  m.mappings[0].intensity = .5;
  check(std::abs(m.audioValue("audio.gain", 1, 3300) - 3.5) < .001,
        "audio targets sensor mapping");
  m.mappings[0].target = "mix";
  m.mappings[0].intensity = 1;
  m.mappings[0].smoothingMs = 100;
  m.mappings[0].lastMs = -1;
  p.mix = 0;
  m.setSource("pot", 0, 3400);
  check(m.apply(p, 3400).mix == 0, "smoothing initialized");
  m.setSource("pot", 1, 3500);
  check(std::abs(m.apply(p, 3500).mix - (1 - std::exp(-1.))) < .001,
        "exponential smoothing");
  m.mappings[0].inputMax = m.mappings[0].inputMin;
  check(m.apply(p, 3500).mix == 0, "invalid range ignored");
  for (int i = 0; i < 500; ++i)
    m.setSource(QString::number(i), .1, 3500);
  check(m.sources.size() == 256, "source count bounded");
  AudioEnvelope e;
  std::vector<float> signal(4800, .25f);
  e.feed(signal.data(), 4800, 48000, 2, 1, 1);
  check(std::abs(e.values[0] - .5) < .001, "audio RMS sensitivity");
  check(e.values[1] > .49 && e.values[3] < .02, "audio DC in low band");
  e.reset();
  check(e.values[0] == 0, "audio reset");
  e.feed(signal.data(), 4800, 48000, 2, 100, 200);
  check(std::abs(e.values[0] - .5 * (1 - std::exp(-1.))) < .001,
        "audio attack envelope");
  std::fill(signal.begin(), signal.end(), 0);
  double previous = e.values[0];
  e.feed(signal.data(), 4800, 48000, 2, 100, 200);
  check(std::abs(e.values[0] - previous * std::exp(-.5)) < .001,
        "audio release envelope");
  if (argc > 1 && std::strcmp(argv[1], "--pure") == 0) {
    std::cout << "Pure modulation and audio checks passed\n";
    return 0;
  }
  ModulationPanel panel;
  check(panel.state().sources.isEmpty(), "inputs do not start by default");
  // Exercise real UDP delivery using a dynamically reserved local port.
  QUdpSocket reserve;
  check(reserve.bind(QHostAddress::LocalHost, 0), "reserve UDP port");
  int port = reserve.localPort();
  reserve.close();
  panel.findChild<QSpinBox *>("udpPort")->setValue(port);
  click(panel, "Listen OSC / JSON");
  QUdpSocket sender;
  sender.writeDatagram(packet, QHostAddress::LocalHost, port);
  pump();
  check(panel.state().sources.value("knob").value == .5,
        "UDP actual source delivery");
  click(panel, "Stop UDP");
  // Exercise split USB serial lines through a POSIX pseudo-terminal.
  int master = posix_openpt(O_RDWR | O_NOCTTY | O_NONBLOCK);
  check(master >= 0 && grantpt(master) == 0 && unlockpt(master) == 0,
        "serial pseudo-terminal");
  panel.findChild<QComboBox *>("serialPort")
      ->setCurrentText(QString::fromLocal8Bit(ptsname(master)));
  click(panel, "Connect serial");
  const QByteArray first = "{\"tilt\":";
  check(::write(master, first.constData(), first.size()) == first.size(),
        "write serial fragment");
  pump();
  check(!panel.state().sources.contains("tilt"),
        "split line waits for newline");
  const QByteArray last = "0.75}\n";
  ::write(master, last.constData(), last.size());
  pump();
  check(panel.state().sources.value("tilt").value == .75,
        "serial split JSON delivered");
  const QByteArray invalid = "{bad}\n{\"tilt\":0.5}\n";
  ::write(master, invalid.constData(), invalid.size());
  pump();
  check(panel.state().sources.value("tilt").value == .5,
        "malformed line recovers");
  click(panel, "Disconnect");
  ::close(master);
  std::cout << "Modulation checks passed (OSC, JSON, mapping, smoothing, UDP, "
               "USB serial)\n";
}
