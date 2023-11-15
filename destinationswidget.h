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

public slots:
    void queryDestinations();
    void handleStatus(const plist_object &plist);

signals:
    void gotPaths(const std::vector<std::string>& paths);
    void gotError(const QString& status);

private slots:
    void updateUI(const plist_object &plist);
    void handleError(const QString& text);

private:
    QString tmuPath{"tmutil"};
};

#endif // DESTINATIONSWIDGET_H
