#include "app_test_support.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QUrl>

#include <algorithm>

using namespace vox::app;
using namespace vox::app::test;

TEST_CASE("The neural voice is offered only in builds that have it", "[app][neural]") {
    TestApp t;
    auto& ctx = t.context();
    auto* neural = ctx.neural();
    REQUIRE(neural != nullptr);
    const bool inRegistry = ctx.registry().find("neural") != nullptr;
    CHECK(inRegistry == NeuralController::available());
    const QVariantList palette = ctx.designer()->palette();
    const bool inPalette = std::ranges::any_of(palette, [](const QVariant& e) {
        return e.toMap().value(QStringLiteral("id")).toString() == QStringLiteral("neural");
    });
    CHECK(inPalette == NeuralController::available());
    if (!NeuralController::available()) {
        CHECK(neural->loadModel(QUrl::fromLocalFile(QStringLiteral("/tmp/x.onnx")))
                  .contains(QStringLiteral("does not include")));
    }
}

#if VOX_ENABLE_ML
TEST_CASE("A chosen model is loaded, saved, and loaded again at start", "[app][neural]") {
    const QString identity = QStringLiteral(VOX_ML_TEST_MODELS "/identity_48k.onnx");
    QString saved;
    {
        TestApp t;
        auto* neural = t.context().neural();
        CHECK_FALSE(neural->loaded());
        REQUIRE(neural->loadModel(QUrl::fromLocalFile(identity)).isEmpty());
        CHECK(neural->loaded());
        CHECK(neural->modelName() == QStringLiteral("Identity"));
        CHECK(neural->details().contains(QStringLiteral("48 kHz")));
        CHECK(t.context().settings().neuralModelPath == identity);
        // A voice with the neural block builds and plays.
        auto* designer = t.context().designer();
        designer->newVoice();
        REQUIRE(designer->addBlock(QStringLiteral("neural")).isEmpty());
        CHECK(designer->latencyMs() > 180.0);
        REQUIRE(designer->save().isEmpty());
        REQUIRE(t.context().saveNow());
        QFile file(t.settingsPath());
        REQUIRE(file.open(QIODevice::ReadOnly));
        saved = QString::fromUtf8(file.readAll());
    }
    TestApp again({.cable = true}, saved);
    CHECK(again.context().neural()->loaded());
}

TEST_CASE("An unusable model is refused with the reason", "[app][neural][errors]") {
    TestApp t;
    auto* neural = t.context().neural();
    const QString problem = neural->loadModel(
        QUrl::fromLocalFile(QStringLiteral(VOX_ML_TEST_MODELS "/wrong_names.onnx")));
    CHECK(problem.contains(QStringLiteral("\"x\"")));
    CHECK_FALSE(neural->loaded());
    CHECK(t.context().settings().neuralModelPath.isEmpty());
}
#endif
