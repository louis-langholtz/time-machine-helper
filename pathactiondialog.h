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
    // NOLINTBEGIN
    Q_OBJECT
    // NOLINTEND

public:
    explicit PathActionDialog(QWidget *parent = nullptr);
    ~PathActionDialog() override;

    void closeEvent(QCloseEvent *event) override;
    void reject() override;

    [[nodiscard]] auto errorString(
        const QString &fallback = {}) const -> QString;
    [[nodiscard]] auto text() const -> QString;
    [[nodiscard]] auto firstArgs() const -> QStringList;
    [[nodiscard]] auto paths() const -> QStringList;
    [[nodiscard]] auto lastArgs() const -> QStringList;
    [[nodiscard]] auto action() const -> QString;
    [[nodiscard]] auto asRoot() const noexcept -> bool;
    [[nodiscard]] auto environment() const -> QProcessEnvironment;
    [[nodiscard]] auto tmutilPath() const -> QString;
    [[nodiscard]] auto sudoPath() const -> QString;
    [[nodiscard]] auto pathPrefix() const -> QString;
    [[nodiscard]] auto stopSignal() const noexcept -> int;

    void setText(const QString& text);
    void setFirstArgs(const QStringList& args);
    void setPaths(const QStringList& paths);
    void setLastArgs(const QStringList& args);
    void setAction(const QString& action);
    void setAsRoot(bool asRoot);
    void setEnvironment(
        const QProcessEnvironment& environment);
    void setTmutilPath(const QString& path);
    void setSudoPath(const QString& path);
    void setPathPrefix(const QString& prefix);
    void setStopSignal(int sig);

    void startAction();
    void stopAction();
    void readProcessOutput();
    void readProcessError();
    void setProcessStarted();
    void setProcessFinished(int code, int status);
    void setErrorOccurred(int error);

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
    QStringList beginList;
    QStringList pathList;
    QStringList endList;
    QString tmuPath{"tmutil"};
    QString pathPre{};
    QString suPath{"sudo"};
    QString verb;
    int stopSig{};
    bool withAdmin{};
    bool askPass{};
};

#endif // PATHACTIONDIALOG_H
