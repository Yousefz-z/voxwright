#pragma once

#include "notification_model.hpp"
#include "settings.hpp"

#include <vox/plugins/registry.hpp>

#include <QObject>
#include <QString>
#include <QUrl>
#include <QtQml/qqmlregistration.h>

#include <memory>

namespace vox::ml {
class ModelHost;
}

namespace vox::app {

/// The neural voice model (experimental). In builds without VOX_ENABLE_ML
/// it reports itself unavailable and the Settings card stays hidden.
class NeuralController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by the application")
    Q_PROPERTY(bool available READ available CONSTANT)
    Q_PROPERTY(bool loaded READ loaded NOTIFY modelChanged)
    Q_PROPERTY(QString modelName READ modelName NOTIFY modelChanged)
    Q_PROPERTY(QString details READ details NOTIFY modelChanged)

public:
    NeuralController(AppSettings& settings, NotificationModel& notifications,
                     QObject* parent = nullptr);
    NeuralController(const NeuralController&) = delete;
    NeuralController& operator=(const NeuralController&) = delete;
    NeuralController(NeuralController&&) = delete;
    NeuralController& operator=(NeuralController&&) = delete;
    ~NeuralController() override;

    /// Adds the "neural" effect to `registry` (ML builds only).
    void registerEffect(plugins::EffectRegistry& registry);
    /// Loads the model saved in the settings, if any.
    void initialize();

    /// Returns "" or why the model cannot be used.
    Q_INVOKABLE QString loadModel(const QUrl& file);
    Q_INVOKABLE void unloadModel();

    [[nodiscard]] static bool available();
    [[nodiscard]] bool loaded() const { return !name_.isEmpty(); }
    [[nodiscard]] QString modelName() const { return name_; }
    [[nodiscard]] QString details() const { return details_; }

signals:
    void modelChanged();
    void settingsChanged();

private:
    [[nodiscard]] QString load(const QString& path);

    AppSettings& settings_;
    NotificationModel& notifications_;
    std::shared_ptr<ml::ModelHost> host_;
    QString name_;
    QString details_;
};

} // namespace vox::app
