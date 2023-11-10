#ifndef PATHACTIONDIALOG_H
#define PATHACTIONDIALOG_H

#include <QDialog>
#include <QStringList>
#include <QProcessEnvironment>

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
    bool asRoot() const;
    QProcessEnvironment environment() const;
    QString tmutilPath() const;

    void setText(const QString& text);
    void setPaths(const QStringList& paths);
    void setAction(const QString& action);
    void setAsRoot(bool asRoot);
    void setEnvironment(
        const QProcessEnvironment& environment);
    void setTmutilPath(const QString& path);

public slots:
    void startAction();
    void readProcessOuput();
    void readProcessError();
    void setProcessStarted();
    void setProcessFinished(int code, int status);
    void setErrorOccurred(int error);

signals:

private:
    QLabel* textLabel{};
    QTextEdit* pathsWidget{};
    QPushButton* yesButton{};
    QPushButton* noButton{};
    QTextEdit* outputWidget{};
    QProcess* process{};
    QStatusBar* statusBar{};
    QProcessEnvironment env;
    QStringList pathList;
    QString tmuPath{"tmutil"};
    QString verb;
    bool withAdmin{};
    bool askPass{};
};

#endif // PATHACTIONDIALOG_H
