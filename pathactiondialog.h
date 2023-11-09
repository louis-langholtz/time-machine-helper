#ifndef PATHACTIONDIALOG_H
#define PATHACTIONDIALOG_H

#include <QDialog>
#include <QStringList>

class QTextEdit;
class QLabel;
class QPushButton;
class QProcess;
class QStatusBar;

class PathActionDialog : public QDialog
{
    Q_OBJECT
public:
    explicit PathActionDialog(QWidget *parent = nullptr);

    QString text() const;
    QStringList paths() const;
    QString action() const;

    void setText(const QString& text);
    void setPaths(const QStringList& paths);
    void setAction(const QString& action);

public slots:
    void startAction();
    void readProcessOuput();
    void readProcessError();
    void setProcessStarted();
    void setProcessFinished(int code, int status);

signals:

private:
    QLabel* textLabel{};
    QTextEdit* pathsWidget{};
    QPushButton* yesButton{};
    QPushButton* noButton{};
    QTextEdit* outputWidget{};
    QProcess* process{};
    QStatusBar* statusBar{};

    QStringList pathList;
    QString verb;
    bool withAdmin{};
};

#endif // PATHACTIONDIALOG_H
