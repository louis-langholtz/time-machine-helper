#ifndef PLISTPROCESS_H
#define PLISTPROCESS_H

#include <optional>

#include <QObject>
#include <QString>
#include <QStringList>

#include "coroutine.h"
#include "plist_object.h"

class QProcess;
class QXmlStreamReader;

class PlistProcess : public QObject
{
    // NOLINTBEGIN
    Q_OBJECT
    // NOLINTEND

public:
    explicit PlistProcess(QObject *parent = nullptr);

    [[nodiscard]] auto plist() const -> std::optional<plist_object>;

    /// @brief Start specified program with given arguments.
    /// @post <code>errorOccurred(int, const QString&)</code> will
    ///   be emitted with the first argument of
    ///   <code>QProcess::FailedToStart</code>, or
    ///   <code>started</code> will be emitted.
    void start(const QString& program,
               const QStringList& args = {});

signals:
    /// @brief Got the "plist".
    /// @note Emitted when the reader has finished parsing a "plist".
    /// @post Finally, <code>finished</code> will be emitted.
    void gotPlist(const plist_object& plist);

    /// @brief Got no "plist".
    /// @note Emitted when the reader has finished without a "plist".
    /// @post Finally, @c finished will be emitted.
    void gotNoPlist();

    /// @brief Error occurred.
    /// @note Emitted for any @c errorOccurred signals from the
    ///   underlying @c QProcess along with the value of
    ///   <code>QProcess::errorString()</code>.
    /// @param error A <code>QProcess::ProcessError</code> value.
    /// @param text Error string from underlying process.
    void errorOccurred(int error, const QString& text);

    /// @brief Got error from plist reader.
    /// @note Only emitted if reader got to end of input and
    ///   detected an error.
    /// @param lineNumber number of the line of the error.
    /// @param error A <code>QXmlStreamReader::Error</code> value.
    /// @param text Error string from underlying XML reader.
    void gotReaderError(qint64 lineNumber,
                        int error,
                        const QString& text);

    /// @brief Started.
    /// @note Emitted when underlying process has actually started.
    /// @post @c errorOccurred or @c gotReaderError may be emitted.
    ///   @c gotPlist or @c gotNoPlist will be emitted. Finally,
    ///   @c finished is emitted.
    void started();

    /// @brief Finished.
    /// @note Emitted after @c started had been emitted and the
    ///   underlying process has finished.
    /// @param code Code that program returned on normal exit.
    /// @param status A @c QProcess::ExitStatus value.
    void finished(int code, int status);

private:
    void handleStarted();
    void handleErrorOccurred(int error);
    void handleProcessFinished(int code, int status);
    void readMore();

    std::optional<plist_object> data;
    QProcess *process{};
    QXmlStreamReader *reader{};
    await_handle<plist_variant> awaiting_handle;
    coroutine_task<plist_object> task;
    QString currentText;
};

#endif // PLISTPROCESS_H
