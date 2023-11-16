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
class QSplitter;

class PathActionDialog : public QDialog
{
    Q_OBJECT
public:
    explicit PathActionDialog(QWidget *parent = nullptr);
    ~PathActionDialog() override;

    void closeEvent(QCloseEvent *event) override;
    void reject() override;

    QString errorString(
        const QString& fallback = {}) const;

    QString text() const;
    QStringList paths() const;
    QString action() const;
    bool asRoot() const noexcept;
    QProcessEnvironment environment() const;
    QString tmutilPath() const;
    QString pathPrefix() const;
    int stopSignal() const noexcept;

    void setText(const QString& text);
    void setPaths(const QStringList& paths);
    void setAction(const QString& action);
    void setAsRoot(bool asRoot);
    void setEnvironment(
        const QProcessEnvironment& environment);
    void setTmutilPath(const QString& path);
    void setPathPrefix(const QString& prefix);
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
    QSplitter* splitter{};
    QLabel* textLabel{};
    QTextEdit* pathsWidget{};
    QPushButton* yesButton{};
    QPushButton* noButton{};
    QPushButton* stopButton{};
    QPushButton* dismissButton{};
    QTextEdit* outputWidget{};
    QStatusBar* statusBar{};
    QProcess* process{};
    QProcessEnvironment env;
    QStringList pathList;
    QString tmuPath{"tmutil"};
    QString pathPre{};
    QString sudoPath{"sudo"};
    QString verb;
    int stopSig{};
    bool withAdmin{};
    bool askPass{};
};

#endif // PATHACTIONDIALOG_H
