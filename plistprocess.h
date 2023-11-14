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
    void gotError(const QString& text);
    void started();
    void finished();

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
