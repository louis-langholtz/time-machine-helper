#ifndef PATHACTIONDIALOG_H
#define PATHACTIONDIALOG_H

#include <QDialog>
#include <QStringList>

class QTextEdit;
class QLabel;
class QPushButton;

class PathActionDialog : public QDialog
{
    Q_OBJECT
public:
    explicit PathActionDialog(QWidget *parent = nullptr);

    QString text() const;
    QStringList paths() const;

    void setText(const QString& text);
    void setPaths(const QStringList& paths);

signals:

private:
    QLabel* textLabel{};
    QTextEdit* pathsWidget{};
    QPushButton* yesButton{};
    QPushButton* noButton{};
    QTextEdit* outputWidget{};

    QStringList pathList;
};

#endif // PATHACTIONDIALOG_H
