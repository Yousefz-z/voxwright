#include <vox/core/result.hpp>

#include <catch2/catch_test_macros.hpp>

#include <string>

namespace {

vox::Result<int> parsePositive(int v) {
    if (v <= 0) {
        return vox::makeError(vox::ErrorCode::InvalidArgument, "Value must be positive.");
    }
    return v;
}

} // namespace

TEST_CASE("Result holds a value or an error", "[core][result]") {
    const auto ok = parsePositive(3);
    REQUIRE(ok.hasValue());
    CHECK(ok.value() == 3);

    const auto bad = parsePositive(-1);
    REQUIRE_FALSE(bad.hasValue());
    CHECK(bad.error().code == vox::ErrorCode::InvalidArgument);
    CHECK(bad.error().message == "Value must be positive.");
    CHECK(bad.valueOr(7) == 7);
}

TEST_CASE("Status reports success and failure", "[core][result]") {
    const vox::Status success;
    CHECK(success.hasValue());
    const vox::Status failure = vox::makeError(vox::ErrorCode::FileNotFound, "missing", "detail");
    REQUIRE_FALSE(failure.hasValue());
    CHECK(failure.error().detail == "detail");
}

TEST_CASE("Every error code has a stable name", "[core][error]") {
    CHECK(vox::toString(vox::ErrorCode::DeviceNotFound) == "DeviceNotFound");
    CHECK(vox::toString(vox::ErrorCode::BufferUnderrun) == "BufferUnderrun");
    CHECK(vox::toString(vox::ErrorCode::UnsupportedSampleRate) == "UnsupportedSampleRate");
}
