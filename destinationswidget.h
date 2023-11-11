#ifndef DESTINATIONSWIDGET_H
#define DESTINATIONSWIDGET_H

#include <map>
#include <string>
#include <vector>

#include <QTableWidget>

#include "coroutine.h"
#include "plist_object.h"

class QProcess;
class QXmlStreamReader;

class DestinationsWidget : public QTableWidget
{
    Q_OBJECT
public:
    explicit DestinationsWidget(
        QWidget *parent = nullptr);

public slots:
    void readMore();
    void queryDestinations();
    void processFinished(int exitCode, int exitStatus);
    void updateUI(const plist_object &plist);

signals:
    void gotPaths(const std::vector<std::string>& paths);
    void gotStatus(const QString& status);
    void gotDestinationsPlist(const plist_object& plist);

private:
    QXmlStreamReader *reader{};
    QProcess *process{};
    QString tmuPath{"tmutil"};
    await_handle<plist_variant> awaiting_handle;
    coroutine_task<plist_object> task;
    QString currentText;
};

#endif // DESTINATIONSWIDGET_H
