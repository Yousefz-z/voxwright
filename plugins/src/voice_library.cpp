#include "vox/plugins/voice_library.hpp"

#include <string>

namespace vox::plugins {

Result<std::vector<VoicePreset>> loadBuiltinVoices(const EffectRegistry& registry) {
    std::vector<VoicePreset> voices;
    for (const EmbeddedText& source : builtinVoiceSources()) {
        auto preset = parseVoicePreset(source.text, registry);
        if (!preset) {
            Error error = preset.error();
            error.message = std::string(source.name) + ": " + error.message;
            return error;
        }
        VoicePreset voice = std::move(preset).value();
        voice.builtIn = true;
        voices.push_back(std::move(voice));
    }
    return voices;
}

} // namespace vox::plugins
