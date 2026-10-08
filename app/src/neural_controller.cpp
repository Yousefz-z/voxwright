#include "neural_controller.hpp"

#if VOX_ENABLE_ML
#include <vox/ml/neural_model.hpp>
#include <vox/ml/neural_voice.hpp>
#endif

#include <QFileInfo>

namespace vox::app {

NeuralController::NeuralController(AppSettings& settings, NotificationModel& notifications,
                                   QObject* parent)
    : QObject(parent)
    , settings_(settings)
    , notifications_(notifications) {
#if VOX_ENABLE_ML
    host_ = std::make_shared<ml::ModelHost>();
#endif
}

NeuralController::~NeuralController() = default;

bool NeuralController::available() {
#if VOX_ENABLE_ML
    return true;
#else
    return false;
#endif
}

// Uses the model host in VOX_ENABLE_ML builds; static only without them.
// NOLINTNEXTLINE(readability-convert-member-functions-to-static)
void NeuralController::registerEffect(plugins::EffectRegistry& registry) {
#if VOX_ENABLE_ML
    ml::registerNeuralEffect(registry, host_);
#else
    static_cast<void>(registry);
#endif
}

void NeuralController::initialize() {
    if (!available() || settings_.neuralModelPath.isEmpty()) {
        return;
    }
    if (const QString problem = load(settings_.neuralModelPath); !problem.isEmpty()) {
        notifications_.post(QStringLiteral("neural"), NotificationModel::Level::Warning,
                            tr("Neural voice model not loaded"), problem);
    }
}

QString NeuralController::loadModel(const QUrl& file) {
    const QString path = file.isLocalFile() ? file.toLocalFile() : file.toString();
    QString problem = load(path);
    if (problem.isEmpty()) {
        settings_.neuralModelPath = path;
        notifications_.dismiss(QStringLiteral("neural"));
        emit settingsChanged();
    }
    return problem;
}

void NeuralController::unloadModel() {
#if VOX_ENABLE_ML
    host_->set(nullptr);
#endif
    name_.clear();
    details_.clear();
    settings_.neuralModelPath.clear();
    emit modelChanged();
    emit settingsChanged();
}

// Uses the model host in VOX_ENABLE_ML builds; static only without them.
// NOLINTNEXTLINE(readability-convert-member-functions-to-static)
QString NeuralController::load(const QString& path) {
#if VOX_ENABLE_ML
    auto model = ml::NeuralModel::load(path.toStdString(), 2);
    if (!model) {
        return QString::fromStdString(model.error().message);
    }
    const ml::ModelInfo& info = model.value()->info();
    name_ = QString::fromStdString(info.name);
    details_ = tr("%1, %2 kHz in, %3 kHz out%4")
                   .arg(QFileInfo(path).fileName())
                   .arg(info.inputRate / 1000.0)
                   .arg(info.outputRate / 1000.0)
                   .arg(info.takesPitch ? tr(", follows the Pitch setting") : QString{});
    host_->set(std::shared_ptr<ml::NeuralModel>(std::move(model).value()));
    emit modelChanged();
    return {};
#else
    static_cast<void>(path);
    return tr("This build of Voxwright does not include neural voices.");
#endif
}

} // namespace vox::app
