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
    void failedToStartQuery(const QString& text);
    void wrongQueryInfo(const QString& detail);
    void gotDestinations(int count);

private slots:
    void updateUI(const plist_object &plist);
    void handleReaderError(int lineNumber, int error, const QString& text);
    void handleErrorOccurred(int error, const QString& text);
    void handleQueryFinished(int code, int status);

private:
    QTableWidgetItem *createdItem(
        int row, int column, Qt::Alignment textAlign = Qt::AlignCenter);
    void update(const std::vector<plist_dict>& destinations);
    void update(const plist_array &plist);
    void update(const plist_dict &plist);
    int findRowWithMountPoint(const QString& key) const;

    QString tmuPath{"tmutil"};
    QBrush saveBg;
    plist_dict lastStatus;
};

#endif // DESTINATIONSWIDGET_H
