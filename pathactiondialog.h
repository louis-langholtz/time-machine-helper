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

    QString errorString(
        const QString& fallback = {}) const;

    QString text() const;
    QStringList paths() const;
    QString action() const;
    bool asRoot() const noexcept;
    QProcessEnvironment environment() const;
    QString tmutilPath() const;
    int stopSignal() const noexcept;

    void setText(const QString& text);
    void setPaths(const QStringList& paths);
    void setAction(const QString& action);
    void setAsRoot(bool asRoot);
    void setEnvironment(
        const QProcessEnvironment& environment);
    void setTmutilPath(const QString& path);
    void setStopSignal(int sig);

public slots:
    void startAction();
    void stopAction();
    void readProcessOutput();
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
    QPushButton* stopButton{};
    QTextEdit* outputWidget{};
    QProcess* process{};
    QStatusBar* statusBar{};
    QProcessEnvironment env;
    QStringList pathList;
    QString tmuPath{"tmutil"};
    QString sudoPath{"sudo"};
    QString verb;
    int stopSig{};
    bool withAdmin{};
    bool askPass{};
};

#endif // PATHACTIONDIALOG_H
