#ifndef DESTINATIONSWIDGET_H
#define DESTINATIONSWIDGET_H

#include <string>
#include <vector>

#include <QTableWidget>

#include "plist_object.h"

class DestinationsWidget : public QTableWidget
{
    Q_OBJECT
public:
    explicit DestinationsWidget(
        QWidget *parent = nullptr);

    QString tmutilPath() const;

    void setTmutilPath(const QString& path);

public slots:
    void queryDestinations();
    void handleStatus(const plist_object &plist);

signals:
    void gotPaths(const std::vector<std::string>& paths);
    void gotError(const QString& status);
    void queryFailedToStart(const QString& text);

private slots:
    void updateUI(const plist_object &plist);
    void handleReaderError(int lineNumber, int error, const QString& text);
    void handleErrorOccurred(int error, const QString& text);
    void handleQueryFinished(int exitCode, int exitStatus);

private:
    QTableWidgetItem *createdItem(int row, int column);

    QString tmuPath{"tmutil"};
    QBrush saveBg;
};

#endif // DESTINATIONSWIDGET_H
