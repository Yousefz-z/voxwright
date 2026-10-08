#pragma once

#include "custom_voice_store.hpp"
#include "notification_model.hpp"

#include <vox/plugins/registry.hpp>
#include <vox/plugins/voice_preset.hpp>

#include <QList>
#include <QObject>
#include <QRandomGenerator>
#include <QStringList>
#include <QUrl>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

namespace vox::engine {
class AudioEngine;
}

namespace vox::app {

class VoiceController;

/// The voice designer: edits a draft voice (effect blocks, their settings,
/// and quick sliders) that plays live while it is edited, then saves it as
/// one of the user's voices. Built-in voices are edited as copies.
class DesignerController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by the application")
    Q_PROPERTY(bool editing READ editing NOTIFY editingChanged)
    Q_PROPERTY(bool isNew READ isNew NOTIFY editingChanged)
    Q_PROPERTY(bool dirty READ dirty NOTIFY dirtyChanged)
    /// False after another voice was chosen while editing; preview() resumes.
    Q_PROPERTY(bool previewing READ previewing NOTIFY previewingChanged)
    Q_PROPERTY(QString name READ name WRITE setName NOTIFY detailsChanged)
    Q_PROPERTY(QString category READ category WRITE setCategory NOTIFY detailsChanged)
    Q_PROPERTY(QString description READ description WRITE setDescription NOTIFY detailsChanged)
    Q_PROPERTY(QString icon READ icon WRITE setIcon NOTIFY detailsChanged)
    Q_PROPERTY(QString color READ color WRITE setColor NOTIFY detailsChanged)
    Q_PROPERTY(QVariantList blocks READ blocks NOTIFY blocksChanged)
    Q_PROPERTY(QVariantList macros READ macros NOTIFY macrosChanged)
    Q_PROPERTY(double latencyMs READ latencyMs NOTIFY blocksChanged)
    Q_PROPERTY(bool canAddBlock READ canAddBlock NOTIFY blocksChanged)
    Q_PROPERTY(bool canAddMacro READ canAddMacro NOTIFY macrosChanged)
    Q_PROPERTY(QVariantList palette READ palette CONSTANT)
    Q_PROPERTY(QStringList categories READ categories CONSTANT)
    Q_PROPERTY(QStringList icons READ icons CONSTANT)
    Q_PROPERTY(QStringList colors READ colors CONSTANT)
    Q_PROPERTY(QVariantList customVoices READ customVoices NOTIFY customVoicesChanged)

public:
    DesignerController(engine::AudioEngine& engine, const plugins::EffectRegistry& registry,
                       VoiceController& voices, CustomVoiceStore& store,
                       NotificationModel& notifications, QObject* parent = nullptr);

    /// Starts a new voice with one pitch block.
    Q_INVOKABLE void newVoice();
    /// Edits a voice of the user's, or a copy of a built-in voice (with the
    /// quick sliders where the user has them now).
    Q_INVOKABLE bool editVoice(const QString& id);

    /// Each of these returns "" or a message saying why not.
    Q_INVOKABLE QString addBlock(const QString& effectId);
    Q_INVOKABLE QString removeBlock(int block);
    Q_INVOKABLE QString moveBlock(int from, int to);
    Q_INVOKABLE void setBypassed(int block, bool bypassed);
    /// Stores and plays a setting; returns the value kept (clamped).
    Q_INVOKABLE double setParameter(int block, int param, double value);
    /// Slider position (0..1) to value and back, following the setting's scale.
    Q_INVOKABLE double valueAt(int block, int param, double position) const;
    Q_INVOKABLE double positionOf(int block, int param, double value) const;
    Q_INVOKABLE QString formatValue(int block, int param, double value) const;
    Q_INVOKABLE QString exposeParameter(int block, int param);
    Q_INVOKABLE void removeMacro(int index);
    Q_INVOKABLE void renameMacro(int index, const QString& name);
    /// Where the quick slider starts; plays the change.
    Q_INVOKABLE void setMacroPosition(int index, double position);

    /// Plays the draft again after another voice was chosen.
    Q_INVOKABLE void preview();
    /// Saves the draft and makes it the active voice. Returns "" or why not.
    Q_INVOKABLE QString save();
    /// Drops the draft and goes back to the active voice.
    Q_INVOKABLE void discard();

    Q_INVOKABLE QString deleteVoice(const QString& id);
    Q_INVOKABLE QString exportVoice(const QString& id, const QUrl& file);
    /// Imports voice files as new voices of the user's; problems are posted
    /// as one notification. Returns how many were imported.
    Q_INVOKABLE int importVoices(const QList<QUrl>& files);

    [[nodiscard]] bool editing() const { return editing_; }
    [[nodiscard]] bool isNew() const { return isNew_; }
    [[nodiscard]] bool dirty() const { return dirty_; }
    [[nodiscard]] bool previewing() const { return previewing_; }
    [[nodiscard]] QString name() const { return QString::fromStdString(draft_.name); }
    [[nodiscard]] QString category() const { return QString::fromStdString(draft_.category); }
    [[nodiscard]] QString description() const { return QString::fromStdString(draft_.description); }
    [[nodiscard]] QString icon() const { return QString::fromStdString(draft_.icon); }
    [[nodiscard]] QString color() const { return QString::fromStdString(draft_.color); }
    void setName(const QString& name);
    void setCategory(const QString& category);
    void setDescription(const QString& description);
    void setIcon(const QString& icon);
    void setColor(const QString& color);

    [[nodiscard]] QVariantList blocks() const;
    [[nodiscard]] QVariantList macros() const;
    [[nodiscard]] double latencyMs() const { return latencyMs_; }
    [[nodiscard]] bool canAddBlock() const;
    [[nodiscard]] bool canAddMacro() const;
    [[nodiscard]] QVariantList palette() const;
    [[nodiscard]] static QStringList categories();
    [[nodiscard]] static QStringList icons();
    [[nodiscard]] static QStringList colors();
    [[nodiscard]] QVariantList customVoices() const;

    [[nodiscard]] const plugins::VoicePreset& draft() const { return draft_; }

signals:
    void editingChanged();
    void dirtyChanged();
    void previewingChanged();
    void detailsChanged();
    void blocksChanged();
    void macrosChanged();
    void customVoicesChanged();

private:
    [[nodiscard]] QString newId();
    [[nodiscard]] QString uniqueName(const QString& base) const;
    [[nodiscard]] const plugins::ParamSpec* spec(int block, int param) const;
    void begin(plugins::VoicePreset draft, bool isNew);
    void setDirty();
    void setPreviewing(bool previewing);
    /// Rebuilds the playing voice from the draft (after structural edits).
    void applyPreview();
    void structureChanged();
    void updateLatency();
    void onVoiceSelected();

    engine::AudioEngine& engine_;
    const plugins::EffectRegistry& registry_;
    VoiceController& voices_;
    CustomVoiceStore& store_;
    NotificationModel& notifications_;
    plugins::VoicePreset draft_;
    bool editing_ = false;
    bool isNew_ = false;
    bool dirty_ = false;
    bool previewing_ = false;
    /// Set while this controller switches voices itself.
    bool selecting_ = false;
    double latencyMs_ = 0.0;
    QRandomGenerator random_{QRandomGenerator::securelySeeded()};
};

} // namespace vox::app
