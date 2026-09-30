#pragma once
#include "engine.hpp"
#include <QByteArray>
#include <QMap>
#include <QString>
#include <QVector>
#include <QWidget>
#include <array>
#include <memory>
struct ModMapping {
  QString source, target;
  double intensity = 0, offset = 0, inputMin = 0, inputMax = 1, smoothingMs = 0;
  bool enabled = true;
  double smoothed = 0, lastMs = -1;
};
struct ModSource {
  double value = 0, timeMs = 0;
};
class ModulationState {
public:
  QMap<QString, ModSource> sources;
  QVector<ModMapping> mappings;
  double timeoutMs = 2000;
  void setSource(const QString &, double, double nowMs);
  bool ingest(const QByteArray &, double nowMs);
  Params apply(Params, double nowMs);
  double audioValue(const QString &target, double base, double nowMs);

private:
  double modulate(const QString &, double base, double lo, double hi,
                  double nowMs);
};
class AudioEnvelope {
public:
  std::array<double, 4> values{};
  void feed(const float *mono, int frames, int sampleRate, double gain,
            double attackMs, double releaseMs);
  void reset();

private:
  double low = 0, highLow = 0;
};
class ModulationPanel : public QWidget {
public:
  explicit ModulationPanel(QWidget *parent = nullptr);
  ~ModulationPanel() override;
  Params apply(Params base);
  ModulationState &state();
  bool ingest(const QByteArray &);

private:
  struct Impl;
  std::unique_ptr<Impl> d;
};
