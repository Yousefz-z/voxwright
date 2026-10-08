#include "device_list_model.hpp"
#include "notification_model.hpp"
#include "voice_models.hpp"

#include <vox/plugins/voice_library.hpp>

#include <catch2/catch_test_macros.hpp>

#include <QSignalSpy>

using namespace vox::app;
using vox::devices::DeviceInfo;
using vox::devices::DeviceKind;

namespace {

const std::vector<vox::plugins::VoicePreset>& builtinVoices() {
    static const auto kVoices =
        vox::plugins::loadBuiltinVoices(vox::plugins::EffectRegistry::builtin()).value();
    return kVoices;
}

} // namespace

TEST_CASE("Voice filter searches names, descriptions, and tags", "[app][models]") {
    VoiceListModel list;
    list.setVoices(&builtinVoices());
    VoiceFilterModel filter;
    filter.setSourceModel(&list);
    CHECK(filter.count() == static_cast<int>(builtinVoices().size()));

    const QSignalSpy counted(&filter, &VoiceFilterModel::countChanged);
    filter.setSearchText(QStringLiteral("telephone"));
    CHECK(filter.count() >= 1);
    CHECK(counted.count() >= 1);
    for (int i = 0; i < filter.count(); ++i) {
        const QModelIndex idx = filter.index(i, 0);
        const QString haystack = idx.data(VoiceListModel::NameRole).toString() + " " +
                                 idx.data(VoiceListModel::DescriptionRole).toString() + " " +
                                 idx.data(VoiceListModel::TagsRole).toString() + " " +
                                 idx.data(VoiceListModel::CategoryRole).toString();
        CHECK(haystack.contains(QStringLiteral("telephone"), Qt::CaseInsensitive));
    }
    filter.setSearchText(QStringLiteral("zzzz no such voice"));
    CHECK(filter.count() == 0);
}

TEST_CASE("Voice filter by category and favorites", "[app][models]") {
    VoiceListModel list;
    list.setVoices(&builtinVoices());
    VoiceFilterModel filter;
    filter.setSourceModel(&list);
    const auto machines = std::count_if(builtinVoices().begin(), builtinVoices().end(),
                                        [](const auto& v) { return v.category == "Machine"; });
    filter.setCategory(QStringLiteral("Machine"));
    CHECK(filter.count() == machines);
    filter.setCategory(VoiceFilterModel::favoritesCategory());
    CHECK(filter.count() == 0);
    list.setFavorites({QStringLiteral("cathedral"), QStringLiteral("megaphone")});
    CHECK(filter.count() == 2);
    list.setActive(QStringLiteral("cathedral"));
    bool sawActive = false;
    for (int i = 0; i < filter.count(); ++i) {
        sawActive = sawActive || filter.index(i, 0).data(VoiceListModel::ActiveRole).toBool();
    }
    CHECK(sawActive);
}

TEST_CASE("Device pickers list defaults, none, and cables first", "[app][models]") {
    const std::vector<DeviceInfo> playback{
        {"spk", "Speakers", DeviceKind::Playback, true, 48000, 2, false},
        {"cable", "CABLE Input (VB-Audio Virtual Cable)", DeviceKind::Playback, false, 48000, 2,
         true}};
    DeviceListModel virtualMic(
        {.systemDefaultEntry = false, .noneEntry = true, .virtualCablesFirst = true});
    virtualMic.setDevices(playback);
    REQUIRE(virtualMic.count() == 3);
    CHECK(virtualMic.idAt(0) == DeviceListModel::noneId());
    CHECK(virtualMic.idAt(1) == QStringLiteral("cable"));
    CHECK(virtualMic.index(1, 0).data(DeviceListModel::IsVirtualCableRole).toBool());
    REQUIRE(virtualMic.firstVirtualCable() != nullptr);
    CHECK(virtualMic.firstVirtualCable()->id == "cable");

    DeviceListModel monitor(
        {.systemDefaultEntry = true, .noneEntry = false, .virtualCablesFirst = false});
    monitor.setDevices(playback);
    CHECK(monitor.idAt(0).isEmpty());
    CHECK(monitor.index(0, 0).data(DeviceListModel::NameRole).toString() ==
          QStringLiteral("System default (Speakers)"));
    CHECK(monitor.indexOf(QStringLiteral("spk")) == 1);
    CHECK(monitor.indexOf(QStringLiteral("unplugged")) == -1);
    CHECK(monitor.nameOf(QStringLiteral("cable")).startsWith(QStringLiteral("CABLE Input")));
}

TEST_CASE("Notifications replace by key and dismiss", "[app][models]") {
    NotificationModel model;
    const QSignalSpy counted(&model, &NotificationModel::countChanged);
    model.post(QStringLiteral("device-0"), NotificationModel::Level::Warning,
               QStringLiteral("Microphone disconnected"), QStringLiteral("first"));
    model.post(QStringLiteral("device-0"), NotificationModel::Level::Warning,
               QStringLiteral("Microphone disconnected"), QStringLiteral("second"));
    model.post(QStringLiteral("engine"), NotificationModel::Level::Error,
               QStringLiteral("Audio stopped"), QStringLiteral("reason"),
               QStringLiteral("Try again"), QStringLiteral("restart-audio"));
    CHECK(model.count() == 2);
    CHECK(model.messageFor(QStringLiteral("device-0")) == QStringLiteral("second"));
    CHECK(model.index(0, 0).data(NotificationModel::ActionRole).toString() ==
          QStringLiteral("restart-audio")); // newest first
    model.dismiss(QStringLiteral("device-0"));
    CHECK(model.count() == 1);
    CHECK_FALSE(model.contains(QStringLiteral("device-0")));
    CHECK(counted.count() == 3);
}
