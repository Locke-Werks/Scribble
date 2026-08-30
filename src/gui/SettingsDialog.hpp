#pragma once

#include <QDialog>
#include <QHash>

#include "config.hpp"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLineEdit;
class QPlainTextEdit;
class QSpinBox;

namespace scribble::gui {

/// Edits the whole Config. Fields are grouped into tabs and carry tooltips taken
/// from the explanations already written against each setting in config.hpp.
class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(const scribble::Config &config, QWidget *parent = nullptr);

    scribble::Config config() const;

private:
    QWidget *buildPathsTab();
    QWidget *buildTranscriptionTab();
    QWidget *buildDiarizationTab();
    QWidget *buildIdentityTab();
    QWidget *buildOutputTab();
    QWidget *buildPerformanceTab();

    QWidget *browseRow(QLineEdit *edit, bool directory);

    scribble::Config base_;  ///< preserves fields the dialog does not surface

    // Paths
    QLineEdit *outDir_ = nullptr;
    QLineEdit *workDir_ = nullptr;
    QLineEdit *dbPath_ = nullptr;
    QLineEdit *modelDir_ = nullptr;
    QCheckBox *mirrorTree_ = nullptr;
    QCheckBox *keepWork_ = nullptr;

    // Transcription
    QComboBox *backend_ = nullptr;
    QLineEdit *model_ = nullptr;
    QComboBox *asrAccel_ = nullptr;
    QLineEdit *language_ = nullptr;
    QCheckBox *translate_ = nullptr;
    QSpinBox *beamSize_ = nullptr;
    QCheckBox *conditionPrevious_ = nullptr;
    QCheckBox *temperatureFallback_ = nullptr;
    QDoubleSpinBox *entropyThreshold_ = nullptr;
    QDoubleSpinBox *logprobThreshold_ = nullptr;
    QDoubleSpinBox *noSpeechThreshold_ = nullptr;
    QCheckBox *wordTimestamps_ = nullptr;
    QCheckBox *suppressNonSpeech_ = nullptr;
    QPlainTextEdit *hotwords_ = nullptr;
    QPlainTextEdit *initialPrompt_ = nullptr;
    QComboBox *isolate_ = nullptr;
    QComboBox *isolateAccel_ = nullptr;
    QLineEdit *isolateModel_ = nullptr;
    QDoubleSpinBox *isolateThreshold_ = nullptr;

    // Diarization
    QCheckBox *diarize_ = nullptr;
    QComboBox *onnxAccel_ = nullptr;
    QComboBox *gpuRuntime_ = nullptr;
    QLineEdit *segmentationModel_ = nullptr;
    QLineEdit *embeddingModel_ = nullptr;
    QSpinBox *numSpeakers_ = nullptr;
    QSpinBox *minSpeakers_ = nullptr;
    QSpinBox *maxSpeakers_ = nullptr;
    QDoubleSpinBox *diarClusterThreshold_ = nullptr;
    QCheckBox *splitChannels_ = nullptr;
    QSpinBox *maxSplitChannels_ = nullptr;
    QDoubleSpinBox *channelIndependence_ = nullptr;

    // Identity
    QDoubleSpinBox *embedMinSegment_ = nullptr;
    QSpinBox *embedMaxSegments_ = nullptr;
    QDoubleSpinBox *matchThreshold_ = nullptr;
    QDoubleSpinBox *reviewThreshold_ = nullptr;
    QDoubleSpinBox *clusterThreshold_ = nullptr;

    // Output
    QHash<QString, QCheckBox *> formats_;
    QCheckBox *overwrite_ = nullptr;

    // Performance
    QCheckBox *recursive_ = nullptr;
    QSpinBox *decodeLookahead_ = nullptr;
    QSpinBox *nThreads_ = nullptr;
};

}  // namespace scribble::gui
