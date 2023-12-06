#ifndef DESTINATIONSWIDGET_H
#define DESTINATIONSWIDGET_H

#include <map>
#include <string>
#include <vector>

#include <QTableWidget>

#include "plist_object.h"

class DestinationsWidget : public QTableWidget
{
    // NOLINTBEGIN
    Q_OBJECT
    // NOLINTEND

public:
    explicit DestinationsWidget(
        QWidget *parent = nullptr);

    [[nodiscard]] auto tmutilPath() const -> QString;

    void setTmutilPath(const QString& path);

    // Public slots...
    void queryDestinations();
    void handleStatus(const plist_object &plist);

signals:
    void gotPaths(const std::map<std::string, plist_dict>& paths);
    void gotError(const QString& status);
    void failedToStartQuery(const QString& text);
    void wrongQueryInfo(const QString& detail);
    void gotDestinations(int count);

private:
    // Private slots...
    void updateUI(const plist_object &plist);
    void handleReaderError(qint64 lineNumber, int error,
                           const QString& text);
    void handleErrorOccurred(int error, const QString& text);
    void handleQueryFinished(int code, int status);

    // Private regular functions...
    auto createdItem(int row, int column,
                     Qt::Alignment textAlign = Qt::AlignCenter)
        -> QTableWidgetItem *;
    void update(const std::vector<plist_dict>& destinations);
    void update(const plist_array &plist);
    void update(const plist_dict &plist);
    [[nodiscard]] auto findRowWithMountPoint(const QString &key) const
        -> int;

    QString tmuPath{"tmutil"};
    plist_dict lastStatus;
};

#endif // DESTINATIONSWIDGET_H
