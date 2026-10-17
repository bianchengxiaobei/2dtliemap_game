#pragma once

#include <chrono>

#include <RmlUi/Core/SystemInterface.h>

namespace zl::ui {

class RmlSystemInterface : public Rml::SystemInterface {
public:
    RmlSystemInterface();

    double GetElapsedTime() override;
    bool LogMessage(Rml::Log::Type type, const Rml::String& message) override;
    void JoinPath(Rml::String& translated_path, const Rml::String& document_path,
                  const Rml::String& path) override;

private:
    std::chrono::steady_clock::time_point m_start;
};

}  // namespace zl::ui
