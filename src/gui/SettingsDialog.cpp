#include "SettingsDialog.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTabBar>
#include <QTabWidget>
#include <QVBoxLayout>

#include "ThemeQt.hpp"
#include "ThemeWidgets.hpp"

#include <algorithm>
#include <string>
#include <vector>

namespace scribble::gui {

namespace {

QString toQString(const scribble::fs::path &p) {
    return QString::fromStdWString(p.wstring());
}

scribble::fs::path toPath(const QString &s) {
    return scribble::fs::path(s.toStdWString());
}

QString joinLines(const std::vector<std::string> &values) {
    QStringList lines;
    for (const auto &v : values) {
        lines << QString::fromStdString(v);
    }
    return lines.join(QLatin1Char('\n'));
}

std::vector<std::string> splitLines(const QString &text) {
    std::vector<std::string> out;
    const QStringList lines = text.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        const QString trimmed = line.trimmed();
        if (!trimmed.isEmpty()) {
            out.push_back(trimmed.toStdString());
        }
    }
    return out;
}

const QStringList kKnownFormats = {QStringLiteral("srt"), QStringLiteral("vtt"),
                                   QStringLiteral("txt"), QStringLiteral("md"),
                                   QStringLiteral("json"), QStringLiteral("tsv")};

}  // namespace

SettingsDialog::SettingsDialog(const scribble::Config &config, QWidget *parent)
    : QDialog(parent), base_(config) {
    setWindowTitle(QStringLiteral("Settings"));
    resize(620, 560);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(18, 14, 18, 16);
    layout->setSpacing(12);
    layout->addWidget(eyebrow(QStringLiteral("// Settings"), 15, this));

    auto *tabs = new QTabWidget(this);
    tabs->tabBar()->setFont(theme::tracked(11, QFont::DemiBold, 0.14));
    tabs->addTab(buildTranscriptionTab(), QStringLiteral("Transcription"));
    tabs->addTab(buildDiarizationTab(), QStringLiteral("Diarization"));
    tabs->addTab(buildIdentityTab(), QStringLiteral("Speaker identity"));
    tabs->addTab(buildOutputTab(), QStringLiteral("Output"));
    tabs->addTab(buildPathsTab(), QStringLiteral("Paths"));
    tabs->addTab(buildPerformanceTab(), QStringLiteral("Performance"));
    layout->addWidget(tabs, 1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    styleButtonBox(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

QWidget *SettingsDialog::browseRow(QLineEdit *edit, bool directory) {
    auto *row = new QWidget(this);
    auto *hbox = new QHBoxLayout(row);
    hbox->setContentsMargins(0, 0, 0, 0);
    hbox->addWidget(edit, 1);
    auto *browse = new QPushButton(QStringLiteral("Browse..."), row);
    styleButton(browse);
    hbox->addWidget(browse);
    connect(browse, &QPushButton::clicked, this, [this, edit, directory] {
        QString chosen;
        if (directory) {
            chosen = QFileDialog::getExistingDirectory(this, QStringLiteral("Choose folder"),
                                                       edit->text());
        } else {
            chosen = QFileDialog::getSaveFileName(this, QStringLiteral("Choose file"), edit->text());
        }
        if (!chosen.isEmpty()) {
            edit->setText(chosen);
        }
    });
    return row;
}

QWidget *SettingsDialog::buildPathsTab() {
    auto *tab = new QWidget(this);
    auto *form = new QFormLayout(tab);

    outDir_ = new QLineEdit(toQString(base_.out_dir), tab);
    workDir_ = new QLineEdit(toQString(base_.work_dir), tab);
    dbPath_ = new QLineEdit(toQString(base_.db_path), tab);
    modelDir_ = new QLineEdit(toQString(base_.model_dir), tab);
    modelDir_->setToolTip(
        QStringLiteral("Empty resolves to %LOCALAPPDATA%\\Scribble\\models."));
    modelDir_->setPlaceholderText(QStringLiteral("Default model location"));

    mirrorTree_ = new QCheckBox(QStringLiteral("Reproduce the input folder structure under the "
                                               "output folder"),
                                tab);
    mirrorTree_->setChecked(base_.mirror_tree);
    keepWork_ = new QCheckBox(QStringLiteral("Keep decoded wav files after a file completes"), tab);
    keepWork_->setChecked(base_.keep_work);

    form->addRow(QStringLiteral("Output folder"), browseRow(outDir_, true));
    form->addRow(QStringLiteral("Work folder"), browseRow(workDir_, true));
    form->addRow(QStringLiteral("Database"), browseRow(dbPath_, false));
    form->addRow(QStringLiteral("Model folder"), browseRow(modelDir_, true));
    form->addRow(QString(), mirrorTree_);
    form->addRow(QString(), keepWork_);
    return tab;
}

QWidget *SettingsDialog::buildTranscriptionTab() {
    auto *tab = new QWidget(this);
    auto *form = new QFormLayout(tab);

    backend_ = new QComboBox(tab);
    backend_->addItems({QStringLiteral("Whisper"), QStringLiteral("Parakeet")});
    backend_->setCurrentIndex(static_cast<int>(base_.backend));

    model_ = new QLineEdit(QString::fromStdString(base_.model), tab);

    asrAccel_ = new QComboBox(tab);
    asrAccel_->addItems({QStringLiteral("Auto"), QStringLiteral("CPU"), QStringLiteral("CUDA")});
    asrAccel_->setCurrentIndex(static_cast<int>(base_.asr_accel));
    asrAccel_->setToolTip(
        QStringLiteral("Where inference runs. Whisper and ONNX are configured separately because "
                       "the diarization models are small enough that CPU is often the right call "
                       "while the GPU stays saturated with Whisper."));

    language_ = new QLineEdit(QString::fromStdString(base_.language), tab);
    language_->setPlaceholderText(QStringLiteral("Auto-detect"));
    language_->setToolTip(QStringLiteral("Empty auto-detects the language."));

    translate_ = new QCheckBox(QStringLiteral("Translate to English"), tab);
    translate_->setChecked(base_.translate);

    beamSize_ = new QSpinBox(tab);
    beamSize_->setRange(1, 20);
    beamSize_->setValue(base_.beam_size);

    conditionPrevious_ = new QCheckBox(QStringLiteral("Condition on previous text"), tab);
    conditionPrevious_->setChecked(base_.condition_on_previous_text);
    conditionPrevious_->setToolTip(
        QStringLiteral("Whisper's repetition failure mode. Off is the right default across a large "
                       "heterogeneous corpus: slightly less context carried between windows, no "
                       "runaway loops on silence and music."));

    temperatureFallback_ = new QCheckBox(QStringLiteral("Temperature fallback"), tab);
    temperatureFallback_->setChecked(base_.temperature_fallback);

    entropyThreshold_ = new QDoubleSpinBox(tab);
    entropyThreshold_->setRange(0.0, 10.0);
    entropyThreshold_->setSingleStep(0.1);
    entropyThreshold_->setValue(base_.entropy_threshold);

    logprobThreshold_ = new QDoubleSpinBox(tab);
    logprobThreshold_->setRange(-10.0, 0.0);
    logprobThreshold_->setSingleStep(0.1);
    logprobThreshold_->setValue(base_.logprob_threshold);

    noSpeechThreshold_ = new QDoubleSpinBox(tab);
    noSpeechThreshold_->setRange(0.0, 1.0);
    noSpeechThreshold_->setSingleStep(0.05);
    noSpeechThreshold_->setValue(base_.no_speech_threshold);

    wordTimestamps_ = new QCheckBox(QStringLiteral("Word-level timestamps"), tab);
    wordTimestamps_->setChecked(base_.word_timestamps);

    suppressNonSpeech_ = new QCheckBox(QStringLiteral("Suppress non-speech tokens"), tab);
    suppressNonSpeech_->setChecked(base_.suppress_non_speech);

    hotwords_ = new QPlainTextEdit(joinLines(base_.hotwords), tab);
    hotwords_->setPlaceholderText(QStringLiteral("One hotword per line"));
    hotwords_->setToolTip(
        QStringLiteral("Proper nouns, jargon and product names. The cheapest large accuracy gain "
                       "available on exactly the words that matter."));
    hotwords_->setFixedHeight(70);

    initialPrompt_ = new QPlainTextEdit(QString::fromStdString(base_.initial_prompt), tab);
    initialPrompt_->setPlaceholderText(QStringLiteral("Optional priming prompt"));
    initialPrompt_->setFixedHeight(60);

    form->addRow(QStringLiteral("Backend"), backend_);
    form->addRow(QStringLiteral("Model"), model_);
    form->addRow(QStringLiteral("Acceleration"), asrAccel_);
    form->addRow(QStringLiteral("Language"), language_);
    form->addRow(QString(), translate_);
    form->addRow(QStringLiteral("Beam size"), beamSize_);
    form->addRow(QStringLiteral("Entropy threshold"), entropyThreshold_);
    form->addRow(QStringLiteral("Log-prob threshold"), logprobThreshold_);
    form->addRow(QStringLiteral("No-speech threshold"), noSpeechThreshold_);
    form->addRow(QString(), conditionPrevious_);
    form->addRow(QString(), temperatureFallback_);
    form->addRow(QString(), wordTimestamps_);
    form->addRow(QString(), suppressNonSpeech_);
    form->addRow(QStringLiteral("Hotwords"), hotwords_);
    form->addRow(QStringLiteral("Initial prompt"), initialPrompt_);

    auto *isolationBox = new QGroupBox(QStringLiteral("SOURCE SEPARATION"), tab);
    auto *isoForm = new QFormLayout(isolationBox);
    isolate_ = new QComboBox(isolationBox);
    isolate_->addItems({QStringLiteral("Never"), QStringLiteral("Auto"), QStringLiteral("Always")});
    isolate_->setCurrentIndex(static_cast<int>(base_.isolate));
    isolate_->setToolTip(
        QStringLiteral("When to run the source-separation front end. Isolation is the largest "
                       "single accuracy win on broadcast, field and music-bedded audio, and a "
                       "small loss on already-clean speech, so it is gated rather than always on."));
    isolateAccel_ = new QComboBox(isolationBox);
    isolateAccel_->addItems(
        {QStringLiteral("Auto"), QStringLiteral("CPU"), QStringLiteral("CUDA")});
    isolateAccel_->setCurrentIndex(static_cast<int>(base_.isolate_accel));
    isolateAccel_->setToolTip(
        QStringLiteral("Accelerator for vocal isolation. This is one large model over the whole "
                       "recording and it is the stage where the GPU wins regardless of the CPU: "
                       "on a 16-core Ryzen 9 7950X with an RTX 4090, isolation took 38s against "
                       "113s on CPU. Auto is the default and recommended. CUDA needs the "
                       "downloadable GPU runtime (Tools > GPU acceleration)."));
    isolateModel_ = new QLineEdit(QString::fromStdString(base_.isolate_model), isolationBox);
    isolateThreshold_ = new QDoubleSpinBox(isolationBox);
    isolateThreshold_->setRange(0.0, 1.0);
    isolateThreshold_->setSingleStep(0.01);
    isolateThreshold_->setValue(base_.isolate_auto_threshold);
    isolateThreshold_->setToolTip(
        QStringLiteral("Noise-floor ratio above which Auto decides a file needs isolating."));
    isoForm->addRow(QStringLiteral("Mode"), isolate_);
    isoForm->addRow(QStringLiteral("Accelerator"), isolateAccel_);
    isoForm->addRow(QStringLiteral("Model"), isolateModel_);
    isoForm->addRow(QStringLiteral("Auto threshold"), isolateThreshold_);
    form->addRow(isolationBox);

    return tab;
}

QWidget *SettingsDialog::buildDiarizationTab() {
    auto *tab = new QWidget(this);
    auto *form = new QFormLayout(tab);

    diarize_ = new QCheckBox(QStringLiteral("Diarize speakers"), tab);
    diarize_->setChecked(base_.diarize);

    onnxAccel_ = new QComboBox(tab);
    onnxAccel_->addItems({QStringLiteral("Auto"), QStringLiteral("CPU"), QStringLiteral("CUDA")});
    onnxAccel_->setCurrentIndex(static_cast<int>(base_.onnx_accel));
    onnxAccel_->setToolTip(
        QStringLiteral("Accelerator for diarization and voiceprints. Auto is the default and the "
                       "recommended setting: these small models run over many short windows and "
                       "are bound by per-launch overhead, so the best choice depends on the CPU "
                       "present. GPU acceleration helps most on machines without a lot of cores; "
                       "Auto decides per stage from the hardware it finds. CUDA needs the "
                       "downloadable GPU runtime (Tools > GPU acceleration)."));

    gpuRuntime_ = new QComboBox(tab);
    gpuRuntime_->addItems(
        {QStringLiteral("Prompt"), QStringLiteral("Auto"), QStringLiteral("Never")});
    gpuRuntime_->setCurrentIndex(static_cast<int>(base_.gpu_runtime));
    gpuRuntime_->setToolTip(
        QStringLiteral("Diarization, voiceprints, isolation and Parakeet reach the GPU through "
                       "onnxruntime, which needs cuDNN. NVIDIA's licence does not allow shipping "
                       "it, so it is fetched on request (about 1.2 GB). Prompt asks before "
                       "downloading and runs on CPU if declined; Auto downloads without asking; "
                       "Never stays on CPU. Whisper is unaffected either way."));

    segmentationModel_ = new QLineEdit(QString::fromStdString(base_.segmentation_model), tab);
    embeddingModel_ = new QLineEdit(QString::fromStdString(base_.embedding_model), tab);

    numSpeakers_ = new QSpinBox(tab);
    numSpeakers_->setRange(-1, 100);
    numSpeakers_->setSpecialValueText(QStringLiteral("auto"));
    numSpeakers_->setValue(base_.num_speakers);
    numSpeakers_->setToolTip(QStringLiteral("-1 lets clustering decide the speaker count."));

    minSpeakers_ = new QSpinBox(tab);
    minSpeakers_->setRange(-1, 100);
    minSpeakers_->setSpecialValueText(QStringLiteral("auto"));
    minSpeakers_->setValue(base_.min_speakers);

    maxSpeakers_ = new QSpinBox(tab);
    maxSpeakers_->setRange(-1, 100);
    maxSpeakers_->setSpecialValueText(QStringLiteral("auto"));
    maxSpeakers_->setValue(base_.max_speakers);

    diarClusterThreshold_ = new QDoubleSpinBox(tab);
    diarClusterThreshold_->setRange(0.0, 1.0);
    diarClusterThreshold_->setSingleStep(0.05);
    diarClusterThreshold_->setValue(base_.diar_cluster_threshold);

    form->addRow(QString(), diarize_);
    form->addRow(QStringLiteral("Accelerator"), onnxAccel_);
    form->addRow(QStringLiteral("GPU runtime download"), gpuRuntime_);
    form->addRow(QStringLiteral("Segmentation model"), segmentationModel_);
    form->addRow(QStringLiteral("Embedding model"), embeddingModel_);
    form->addRow(QStringLiteral("Speakers"), numSpeakers_);
    form->addRow(QStringLiteral("Min speakers"), minSpeakers_);
    form->addRow(QStringLiteral("Max speakers"), maxSpeakers_);
    form->addRow(QStringLiteral("Cluster threshold"), diarClusterThreshold_);

    auto *trackBox = new QGroupBox(QStringLiteral("MULTI-TRACK"), tab);
    auto *trackForm = new QFormLayout(trackBox);
    splitChannels_ = new QCheckBox(QStringLiteral("Split channels into separate tracks"), trackBox);
    splitChannels_->setChecked(base_.split_channels);
    splitChannels_->setToolTip(
        QStringLiteral("For recordings where each channel is a separate microphone on a "
                       "separate person. An ordinary stereo mix is not that: both channels "
                       "carry the same sources, and splitting one produces two half "
                       "transcripts of the same conversation."));
    maxSplitChannels_ = new QSpinBox(trackBox);
    maxSplitChannels_->setRange(1, 64);
    maxSplitChannels_->setValue(base_.max_split_channels);
    channelIndependence_ = new QDoubleSpinBox(trackBox);
    channelIndependence_->setRange(0.0, 1.0);
    channelIndependence_->setSingleStep(0.01);
    channelIndependence_->setValue(base_.channel_independence);
    channelIndependence_->setToolTip(
        QStringLiteral("How unlike each other two channels must be before they count as "
                       "separate microphones. Stereo content sits high because both channels "
                       "carry the same sources. Independent microphones sit low, since each "
                       "is dominated by whoever is closest to it."));
    trackForm->addRow(QString(), splitChannels_);
    trackForm->addRow(QStringLiteral("Max split channels"), maxSplitChannels_);
    trackForm->addRow(QStringLiteral("Channel independence"), channelIndependence_);
    form->addRow(trackBox);

    return tab;
}

QWidget *SettingsDialog::buildIdentityTab() {
    auto *tab = new QWidget(this);
    auto *form = new QFormLayout(tab);

    embedMinSegment_ = new QDoubleSpinBox(tab);
    embedMinSegment_->setRange(0.0, 30.0);
    embedMinSegment_->setSingleStep(0.5);
    embedMinSegment_->setSuffix(QStringLiteral(" s"));
    embedMinSegment_->setValue(base_.embed_min_segment);
    embedMinSegment_->setToolTip(
        QStringLiteral("Shortest turn allowed to contribute to a speaker's voiceprint. Brief "
                       "interjections carry too little signal and poison the centroid."));

    embedMaxSegments_ = new QSpinBox(tab);
    embedMaxSegments_->setRange(1, 64);
    embedMaxSegments_->setValue(base_.embed_max_segments);

    minSpeakerSpeech_ = new QDoubleSpinBox(tab);
    minSpeakerSpeech_->setRange(0.0, 120.0);
    minSpeakerSpeech_->setSingleStep(1.0);
    minSpeakerSpeech_->setSuffix(QStringLiteral(" s"));
    minSpeakerSpeech_->setSpecialValueText(QStringLiteral("off"));
    minSpeakerSpeech_->setValue(base_.min_speaker_speech);
    minSpeakerSpeech_->setToolTip(QStringLiteral(
        "Total speech a voice must carry in a file before it is allowed to become a "
        "speaker of its own.\n\n"
        "Diarization has no notion of whether a cluster is worth having, and hands back a "
        "speaker for a two-word interjection off-mic as readily as for the person running "
        "the meeting. Since two speakers in one file may not share an identity, every one "
        "of those blips is forced to mint a new one, which is how a three-person recording "
        "ends up with thirty-seven speakers.\n\n"
        "This gates naming, not matching. A brief speaker can still join somebody who "
        "already exists, which is what recovers the two-second tail of a long speaker that "
        "diarization split. It just cannot invent anybody. Its words stay in the "
        "transcript either way, under the file-local label.\n\n"
        "Set to off to name every voice however brief."));

    matchThreshold_ = new QDoubleSpinBox(tab);
    matchThreshold_->setRange(0.0, 1.0);
    matchThreshold_->setSingleStep(0.01);
    matchThreshold_->setValue(base_.match_threshold);
    matchThreshold_->setToolTip(
        QStringLiteral("Cosine above which a file-local speaker joins an existing global one."));

    reviewThreshold_ = new QDoubleSpinBox(tab);
    reviewThreshold_->setRange(0.0, 1.0);
    reviewThreshold_->setSingleStep(0.01);
    reviewThreshold_->setValue(base_.review_threshold);
    reviewThreshold_->setToolTip(
        QStringLiteral("Pairs landing between review and match get reported for a human to merge "
                       "rather than silently split or silently joined."));

    clusterThreshold_ = new QDoubleSpinBox(tab);
    clusterThreshold_->setRange(0.0, 1.0);
    clusterThreshold_->setSingleStep(0.01);
    clusterThreshold_->setValue(base_.cluster_threshold);
    clusterThreshold_->setToolTip(
        QStringLiteral("Distance threshold for the offline reclustering pass."));

    reclusterAfterBatch_ =
        new QCheckBox(QStringLiteral("Recluster and re-render when a batch finishes"), tab);
    reclusterAfterBatch_->setChecked(base_.recluster_after_batch);
    reclusterAfterBatch_->setToolTip(
        QStringLiteral("Matching happens in arrival order while a batch runs, so whoever "
                       "appears first defines each identity and the grouping ends up "
                       "arbitrary. Reclustering reconciles it, keeping every name you have "
                       "entered.\n\n"
                       "The re-render is part of the same step because reclustering can move "
                       "a speaker to a different identity, which makes transcripts already "
                       "written to disk stale. Only files whose speakers actually changed "
                       "trigger a rewrite."));

    form->addRow(QStringLiteral("Min embed segment"), embedMinSegment_);
    form->addRow(QStringLiteral("Max embed segments"), embedMaxSegments_);
    form->addRow(QStringLiteral("Min speech per speaker"), minSpeakerSpeech_);
    form->addRow(QStringLiteral("Match threshold"), matchThreshold_);
    form->addRow(QStringLiteral("Review threshold"), reviewThreshold_);
    form->addRow(QStringLiteral("Cluster threshold"), clusterThreshold_);
    form->addRow(QString(), reclusterAfterBatch_);
    return tab;
}

QWidget *SettingsDialog::buildOutputTab() {
    auto *tab = new QWidget(this);
    auto *form = new QFormLayout(tab);

    std::vector<std::string> current = base_.formats;
    auto *formatsWidget = new QWidget(tab);
    auto *formatsLayout = new QHBoxLayout(formatsWidget);
    formatsLayout->setContentsMargins(0, 0, 0, 0);
    for (const QString &fmt : kKnownFormats) {
        auto *box = new QCheckBox(fmt, formatsWidget);
        const bool on = std::find(current.begin(), current.end(), fmt.toStdString()) != current.end();
        box->setChecked(on);
        formats_.insert(fmt, box);
        formatsLayout->addWidget(box);
    }
    formatsLayout->addStretch(1);

    overwrite_ = new QCheckBox(QStringLiteral("Overwrite files that are already transcribed"), tab);
    overwrite_->setChecked(base_.overwrite);

    form->addRow(QStringLiteral("Formats"), formatsWidget);
    form->addRow(QString(), overwrite_);
    return tab;
}

QWidget *SettingsDialog::buildPerformanceTab() {
    auto *tab = new QWidget(this);
    auto *form = new QFormLayout(tab);

    nThreads_ = new QSpinBox(tab);
    nThreads_->setRange(0, 256);
    nThreads_->setSpecialValueText(QStringLiteral("auto"));
    nThreads_->setValue(base_.n_threads);
    nThreads_->setToolTip(QStringLiteral("0 picks the hardware concurrency of the machine."));

    decodeLookahead_ = new QSpinBox(tab);
    decodeLookahead_->setRange(0, 16);
    decodeLookahead_->setValue(base_.decode_lookahead);
    decodeLookahead_->setToolTip(QStringLiteral("Files to decode ahead of the GPU stage."));

    recursive_ = new QCheckBox(QStringLiteral("Descend into subfolders when a folder is added"),
                               tab);
    recursive_->setChecked(base_.recursive);

    form->addRow(QStringLiteral("Threads"), nThreads_);
    form->addRow(QStringLiteral("Decode lookahead"), decodeLookahead_);
    form->addRow(QString(), recursive_);
    return tab;
}

scribble::Config SettingsDialog::config() const {
    scribble::Config c = base_;

    c.out_dir = toPath(outDir_->text());
    c.work_dir = toPath(workDir_->text());
    c.db_path = toPath(dbPath_->text());
    c.model_dir = toPath(modelDir_->text());
    c.mirror_tree = mirrorTree_->isChecked();
    c.keep_work = keepWork_->isChecked();

    c.backend = static_cast<scribble::AsrBackend>(backend_->currentIndex());
    c.model = model_->text().toStdString();
    c.asr_accel = static_cast<scribble::Accel>(asrAccel_->currentIndex());
    c.language = language_->text().toStdString();
    c.translate = translate_->isChecked();
    c.beam_size = beamSize_->value();
    c.condition_on_previous_text = conditionPrevious_->isChecked();
    c.temperature_fallback = temperatureFallback_->isChecked();
    c.entropy_threshold = static_cast<float>(entropyThreshold_->value());
    c.logprob_threshold = static_cast<float>(logprobThreshold_->value());
    c.no_speech_threshold = static_cast<float>(noSpeechThreshold_->value());
    c.word_timestamps = wordTimestamps_->isChecked();
    c.suppress_non_speech = suppressNonSpeech_->isChecked();
    c.hotwords = splitLines(hotwords_->toPlainText());
    c.initial_prompt = initialPrompt_->toPlainText().toStdString();
    c.isolate = static_cast<scribble::IsolateMode>(isolate_->currentIndex());
    c.isolate_accel = static_cast<scribble::Accel>(isolateAccel_->currentIndex());
    c.isolate_model = isolateModel_->text().toStdString();
    c.isolate_auto_threshold = static_cast<float>(isolateThreshold_->value());

    c.diarize = diarize_->isChecked();
    c.onnx_accel = static_cast<scribble::Accel>(onnxAccel_->currentIndex());
    c.gpu_runtime = static_cast<scribble::GpuRuntimeMode>(gpuRuntime_->currentIndex());
    c.segmentation_model = segmentationModel_->text().toStdString();
    c.embedding_model = embeddingModel_->text().toStdString();
    c.num_speakers = numSpeakers_->value();
    c.min_speakers = minSpeakers_->value();
    c.max_speakers = maxSpeakers_->value();
    c.diar_cluster_threshold = static_cast<float>(diarClusterThreshold_->value());
    c.split_channels = splitChannels_->isChecked();
    c.max_split_channels = maxSplitChannels_->value();
    c.channel_independence = static_cast<float>(channelIndependence_->value());

    c.embed_min_segment = embedMinSegment_->value();
    c.min_speaker_speech = minSpeakerSpeech_->value();
    c.embed_max_segments = embedMaxSegments_->value();
    c.match_threshold = static_cast<float>(matchThreshold_->value());
    c.review_threshold = static_cast<float>(reviewThreshold_->value());
    c.cluster_threshold = static_cast<float>(clusterThreshold_->value());
    c.recluster_after_batch = reclusterAfterBatch_->isChecked();

    // Rebuild formats from the checkboxes, preserving any format the dialog does
    // not surface so an unusual configured value is not silently dropped.
    std::vector<std::string> formats;
    for (const QString &fmt : kKnownFormats) {
        const auto it = formats_.constFind(fmt);
        if (it != formats_.constEnd() && it.value()->isChecked()) {
            formats.push_back(fmt.toStdString());
        }
    }
    for (const std::string &existing : base_.formats) {
        if (!kKnownFormats.contains(QString::fromStdString(existing))) {
            formats.push_back(existing);
        }
    }
    c.formats = formats;
    c.overwrite = overwrite_->isChecked();

    c.recursive = recursive_->isChecked();
    c.decode_lookahead = decodeLookahead_->value();
    c.n_threads = nThreads_->value();

    return c;
}

}  // namespace scribble::gui
