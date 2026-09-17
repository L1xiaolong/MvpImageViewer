#pragma once
#include <QString>
#include <memory>
namespace ispview::diagnostics {
void captureFatalMessage();
class CrashCapture {
public:
    CrashCapture();
    ~CrashCapture();
    QString start(const QString& directory, const QString& executableDirectory,
                  const QString& session);
    // False once a previously started helper process has exited, so the service can report the
    // lost crash capture instead of silently claiming it is still active.
    [[nodiscard]] bool alive() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
