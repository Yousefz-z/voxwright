#include "autostart.hpp"

#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QXmlStreamWriter>

#include <algorithm>

namespace vox::app {
namespace {

constexpr auto kEntryName = "Voxwright";
constexpr auto kAgentLabel = "io.github.yousefz-z.voxwright";

template <class Quote>
QString commandLine(const QString& program, const QStringList& arguments, Quote quote) {
    QStringList parts{quote(program)};
    for (const QString& a : arguments) {
        parts.append(quote(a));
    }
    return parts.join(QLatin1Char(' '));
}

Status writeFile(const QString& path, const QByteArray& content) {
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        return makeError(ErrorCode::AutostartFailed,
                         "Start at sign-in could not be turned on: the folder " +
                             QFileInfo(path).absolutePath().toStdString() +
                             " could not be created.");
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(content) != content.size() ||
        !file.commit()) {
        return makeError(ErrorCode::AutostartFailed,
                         "Start at sign-in could not be turned on: " + path.toStdString() +
                             " could not be written.",
                         file.errorString().toStdString());
    }
    return {};
}

Status removeFile(const QString& path) {
    if (QFile::exists(path) && !QFile::remove(path)) {
        return makeError(ErrorCode::AutostartFailed,
                         "Start at sign-in could not be turned off: " + path.toStdString() +
                             " could not be deleted. Delete it by hand.");
    }
    return {};
}

class RunKeyAutostart final : public Autostart {
public:
    RunKeyAutostart(const QString& settings, bool registry)
        : settings_(settings, registry ? QSettings::NativeFormat : QSettings::IniFormat) {}

    [[nodiscard]] bool isEnabled() const override {
        return settings_.contains(QString::fromLatin1(kEntryName));
    }

    [[nodiscard]] Status setEnabled(bool enabled, const QString& program,
                                    const QStringList& arguments) override {
        if (enabled) {
            settings_.setValue(
                QString::fromLatin1(kEntryName),
                commandLine(QDir::toNativeSeparators(program), arguments, windowsArgument));
        } else {
            settings_.remove(QString::fromLatin1(kEntryName));
        }
        settings_.sync();
        if (settings_.status() != QSettings::NoError) {
            return makeError(ErrorCode::AutostartFailed,
                             "Windows did not let Voxwright change its sign-in entry in the "
                             "registry. Check that no policy blocks startup apps, or use Settings, "
                             "Apps, Startup.");
        }
        return {};
    }

    [[nodiscard]] QString location() const override { return settings_.fileName(); }

private:
    QSettings settings_;
};

class LaunchAgentAutostart final : public Autostart {
public:
    explicit LaunchAgentAutostart(const QString& directory)
        : path_(directory + QLatin1Char('/') + QString::fromLatin1(kAgentLabel) +
                QStringLiteral(".plist")) {}

    [[nodiscard]] bool isEnabled() const override { return QFile::exists(path_); }

    [[nodiscard]] Status setEnabled(bool enabled, const QString& program,
                                    const QStringList& arguments) override {
        if (!enabled) {
            return removeFile(path_);
        }
        QByteArray content;
        QXmlStreamWriter xml(&content);
        xml.setAutoFormatting(true);
        xml.writeStartDocument();
        xml.writeDTD(QStringLiteral("<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
                                    "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">"));
        xml.writeStartElement(QStringLiteral("plist"));
        xml.writeAttribute(QStringLiteral("version"), QStringLiteral("1.0"));
        xml.writeStartElement(QStringLiteral("dict"));
        xml.writeTextElement(QStringLiteral("key"), QStringLiteral("Label"));
        xml.writeTextElement(QStringLiteral("string"), QString::fromLatin1(kAgentLabel));
        xml.writeTextElement(QStringLiteral("key"), QStringLiteral("ProgramArguments"));
        xml.writeStartElement(QStringLiteral("array"));
        xml.writeTextElement(QStringLiteral("string"), program);
        for (const QString& a : arguments) {
            xml.writeTextElement(QStringLiteral("string"), a);
        }
        xml.writeEndElement();
        xml.writeTextElement(QStringLiteral("key"), QStringLiteral("RunAtLoad"));
        xml.writeEmptyElement(QStringLiteral("true"));
        xml.writeTextElement(QStringLiteral("key"), QStringLiteral("ProcessType"));
        xml.writeTextElement(QStringLiteral("string"), QStringLiteral("Interactive"));
        xml.writeEndElement();
        xml.writeEndElement();
        xml.writeEndDocument();
        return writeFile(path_, content);
    }

    [[nodiscard]] QString location() const override { return path_; }

private:
    QString path_;
};

class XdgAutostart final : public Autostart {
public:
    explicit XdgAutostart(const QString& directory)
        : path_(directory + QStringLiteral("/voxwright.desktop")) {}

    [[nodiscard]] bool isEnabled() const override { return QFile::exists(path_); }

    [[nodiscard]] Status setEnabled(bool enabled, const QString& program,
                                    const QStringList& arguments) override {
        if (!enabled) {
            return removeFile(path_);
        }
        const QString content = QStringLiteral("[Desktop Entry]\n"
                                               "Type=Application\n"
                                               "Name=Voxwright\n"
                                               "Comment=Real-time voice changer and soundboard\n"
                                               "Exec=%1\n"
                                               "Terminal=false\n"
                                               "X-GNOME-Autostart-enabled=true\n")
                                    .arg(commandLine(program, arguments, desktopEntryArgument));
        return writeFile(path_, content.toUtf8());
    }

    [[nodiscard]] QString location() const override { return path_; }

private:
    QString path_;
};

} // namespace

QString windowsArgument(const QString& argument) {
    const bool plain = !argument.isEmpty() && !argument.contains(QLatin1Char(' ')) &&
                       !argument.contains(QLatin1Char('\t')) &&
                       !argument.contains(QLatin1Char('"'));
    if (plain) {
        return argument;
    }
    // Backslashes are literal except before a quote, where each one must be
    // doubled; a quote itself is escaped with a backslash.
    QString out = QStringLiteral("\"");
    qsizetype backslashes = 0;
    for (const QChar c : argument) {
        if (c == QLatin1Char('\\')) {
            ++backslashes;
            continue;
        }
        if (c == QLatin1Char('"')) {
            out += QString(backslashes * 2 + 1, QLatin1Char('\\'));
            out += c;
        } else {
            out += QString(backslashes, QLatin1Char('\\'));
            out += c;
        }
        backslashes = 0;
    }
    out += QString(backslashes * 2, QLatin1Char('\\'));
    out += QLatin1Char('"');
    return out;
}

QString desktopEntryArgument(const QString& argument) {
    static const QString kReserved = QStringLiteral(" \t\n\"'\\><~|&;$*?#()`");
    const bool plain =
        !argument.isEmpty() && std::none_of(argument.begin(), argument.end(),
                                            [](QChar c) { return kReserved.contains(c); });
    QString out = argument;
    out.replace(QLatin1Char('%'), QStringLiteral("%%")); // not a field code
    if (!plain) {
        QString quoted;
        for (const QChar c : out) {
            if (c == QLatin1Char('"') || c == QLatin1Char('`') || c == QLatin1Char('$') ||
                c == QLatin1Char('\\')) {
                quoted += QLatin1Char('\\');
            }
            quoted += c;
        }
        out = QLatin1Char('"') + quoted + QLatin1Char('"');
    }
    // The Exec value is itself a string, in which a backslash is written twice.
    out.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    return out;
}

std::unique_ptr<Autostart> makeRunKeyAutostart(const QString& settings, bool registry) {
    return std::make_unique<RunKeyAutostart>(settings, registry);
}

std::unique_ptr<Autostart> makeLaunchAgentAutostart(const QString& directory) {
    return std::make_unique<LaunchAgentAutostart>(directory);
}

std::unique_ptr<Autostart> makeXdgAutostart(const QString& directory) {
    return std::make_unique<XdgAutostart>(directory);
}

std::unique_ptr<Autostart> makePlatformAutostart() {
#if defined(Q_OS_WIN)
    return makeRunKeyAutostart(
        QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run"),
        true);
#elif defined(Q_OS_MACOS)
    return makeLaunchAgentAutostart(QDir::homePath() + QStringLiteral("/Library/LaunchAgents"));
#else
    return makeXdgAutostart(QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) +
                            QStringLiteral("/autostart"));
#endif
}

} // namespace vox::app
