#ifndef PLISTPROCESS_H
#define PLISTPROCESS_H

#include <QObject>
#include <QString>
#include <QStringList>

#include "coroutine.h"
#include "plist_object.h"

class QProcess;
class QXmlStreamReader;

class PlistProcess : public QObject
{
    Q_OBJECT
public:
    explicit PlistProcess(QObject *parent = nullptr);

    bool done() const noexcept;

    void start(const QString& program,
               const QStringList& args = {});

signals:
    void gotPlist(const plist_object& plist);

    void gotInfo(const QString& text);

    /// @brief Error occurred.
    /// @note Emitted for any <code>errorOccurred</code> signals from
    ///   the underlying <code>QProcess</code> along with the value of
    ///   <code>QProcess::errorString()</code>.
    /// @param error A <code>QProcess::ProcessError</code> value.
    /// @param text Error string from underlying <code>QProcess</code>.
    void errorOccurred(int error, const QString& text);

    /// @brief Got error from plist reader.
    void gotReaderError(int lineNumber, const QString& text);

    void started();

    void finished(int exitCode, int exitStatus);

private slots:
    void handleStarted();
    void handleErrorOccurred(int error);
    void handleFinished(int exitCode, int exitStatus);
    void readMore();

private:
    QProcess *process{};
    QXmlStreamReader *reader{};
    await_handle<plist_variant> awaiting_handle;
    coroutine_task<plist_object> task;
    QString currentText;
};

#endif // PLISTPROCESS_H
