#include <unistd.h> // for isatty

#include <csignal>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QTextEdit>
#include <QPushButton>
#include <QProcess>
#include <QStatusBar>

#include "pathactiondialog.h"

namespace {

constexpr auto openMode =
    QProcess::ReadWrite|QProcess::Text|QProcess::Unbuffered;
constexpr auto noExplanationMsg =
    "no explanation";

QString toHtmlList(const QStringList& strings)
{
    QString result;
    result.append("<small><pre>");
    for (const auto& string: strings) {
        result.append(string);
        result.append('\n');
    }
    result.append("</pre></small>");
    return result;
}

}

PathActionDialog::PathActionDialog(QWidget *parent):
    QDialog{parent},
    textLabel{new QLabel{this}},
    pathsWidget{new QTextEdit{this}},
    yesButton{new QPushButton{"Yes", this}},
    noButton{new QPushButton{"No", this}},
    stopButton{new QPushButton{"Stop", this}},
    outputWidget{new QTextEdit{this}},
    statusBar{new QStatusBar{this}},
    env{QProcessEnvironment::InheritFromParent},
    stopSig{SIGINT} // tmutil handles SIGINT most gracefully.
{
    this->setWindowTitle(tr("Path Action Dialog"));

    this->textLabel->setFont([this](){
        QFont font = this->textLabel->font();
        font.setWeight(QFont::Bold);
        return font;
    }());
    this->textLabel->setObjectName("textLabel");

    this->pathsWidget->setObjectName("pathsWidget");
    this->pathsWidget->setReadOnly(true);

    this->yesButton->setEnabled(false);

    this->noButton->setEnabled(false);
    this->noButton->setDefault(true);

    this->stopButton->setEnabled(false);

    this->outputWidget->setObjectName("outputWidget");
    this->outputWidget->setEnabled(false);
    this->outputWidget->setReadOnly(true);
    this->outputWidget->document()->setDefaultStyleSheet(
        "* {font-family: \"Andale Mono\";} .stdout {color:green;} .stderr {color:red;}");

    this->statusBar->showMessage("Awaiting confirmation of action.");

    this->setLayout([this](){
        auto *mainLayout = new QVBoxLayout;
        mainLayout->setObjectName("mainLayout");
        mainLayout->addWidget(this->textLabel);
        mainLayout->addWidget(this->pathsWidget);
        mainLayout->addLayout([this](){
            auto *choiceLayout = new QHBoxLayout;
            choiceLayout->setObjectName("choiceLayout");
            choiceLayout->addWidget(this->yesButton);
            choiceLayout->addWidget(this->noButton);
            choiceLayout->addWidget(this->stopButton);
            choiceLayout->setAlignment(Qt::AlignCenter);
            return choiceLayout;
        }());
        mainLayout->addWidget(this->outputWidget);
        mainLayout->addWidget(this->statusBar);
        return mainLayout;
    }());

    connect(this->noButton, &QPushButton::clicked,
            this, &PathActionDialog::close);
    connect(this->stopButton, &QPushButton::clicked,
            this, &PathActionDialog::stopAction);
}

QString PathActionDialog::errorString(
    const QString& fallback) const
{
    return this->process
               ? this->process->errorString()
               : fallback;
}

QString PathActionDialog::text() const
{
    return this->textLabel->text();
}

QStringList PathActionDialog::paths() const
{
    return this->pathList;
}

QString PathActionDialog::action() const
{
    return this->verb;
}

bool PathActionDialog::asRoot() const noexcept
{
    return this->withAdmin;
}

QProcessEnvironment PathActionDialog::environment() const
{
    return this->env;
}

QString PathActionDialog::tmutilPath() const
{
    return this->tmuPath;
}

int PathActionDialog::stopSignal() const noexcept
{
    return this->stopSig;
}

void PathActionDialog::setText(const QString &text)
{
    this->textLabel->setText(text);
}

void PathActionDialog::setPaths(const QStringList &paths)
{
    this->pathList = paths;
    this->pathsWidget->setHtml(toHtmlList(paths));
}

void PathActionDialog::setAction(const QString &action)
{
    this->verb = action;
    if (action.isEmpty()) {
        this->yesButton->setEnabled(false);
        this->noButton->setEnabled(false);
        disconnect(this->yesButton, &QPushButton::clicked,
                   this, &PathActionDialog::startAction);
    }
    else {
        this->yesButton->setEnabled(true);
        this->noButton->setEnabled(true);
        connect(this->yesButton, &QPushButton::clicked,
                this, &PathActionDialog::startAction);
    }
}

void PathActionDialog::setAsRoot(bool asRoot)
{
    this->withAdmin = asRoot;
}

void PathActionDialog::setEnvironment(
    const QProcessEnvironment &environment)
{
    this->env = environment;
}

void PathActionDialog::setTmutilPath(const QString &path)
{
    this->tmuPath = path;
}

void PathActionDialog::setStopSignal(int sig)
{
    this->stopSig = sig;
}

void PathActionDialog::startAction()
{
    this->yesButton->setEnabled(false);
    this->noButton->setEnabled(false);
    this->outputWidget->setEnabled(true);

    const auto program = QString((this->withAdmin)
        ? QString(this->sudoPath): this->tmuPath);

    auto argList = QStringList();
    if (this->withAdmin) {
        //argList << "--preserve-env";
        if (this->askPass) {
            argList << "--askpass";
        }
        //argList << "-s";
        //argList << "-w";
        argList << this->tmuPath;
    }
    argList << this->verb;
    for (const auto& path: pathList) {
        argList << "-p" << path;
    }
    qInfo() << "startAction about to run:" << program << argList.join(' ');

    this->process = new QProcess(this);
    connect(this->process, &QProcess::errorOccurred,
            this, &PathActionDialog::setErrorOccurred);
    connect(this->process, &QProcess::started,
            this, &PathActionDialog::setProcessStarted);
    connect(this->process, &QProcess::finished,
            this, &PathActionDialog::setProcessFinished);
    connect(this->process, &QProcess::readyReadStandardOutput,
            this, &PathActionDialog::readProcessOutput);
    connect(this->process, &QProcess::readyReadStandardError,
            this, &PathActionDialog::readProcessError);
    this->stopButton->setEnabled(true);
    this->statusBar->showMessage("Starting process");
    this->process->setProcessEnvironment(this->env);
    this->process->start(program, argList, openMode);
}

void PathActionDialog::stopAction()
{
    qDebug() << "stopAction called for:" << this->verb;
    if (this->process && this->process->state() == QProcess::Running) {
        qDebug() << "stopAction kill for:" << this->verb;
        const auto res = ::kill(this->process->processId(), this->stopSig);
        if (res == -1) {
            qWarning() << "kill failed:" << strerror(errno);
        }
    }
}

void PathActionDialog::readProcessOutput()
{
    if (!this->process) {
        return;
    }
    auto text = QString(this->process->readAllStandardOutput());
    qDebug() << "readProcessOutput called for:" << text;
    if (!text.isEmpty()) {
        text.replace('\n', QString("<br/>"));
        this->outputWidget->insertHtml(
            QString("<span class='stdout'>%1</span>").arg(text));
    }
}

void PathActionDialog::readProcessError()
{
    if (!this->process) {
        return;
    }
    auto text = QString(this->process->readAllStandardError());
    qDebug() << "readProcessError called for:" << text;
    if (!text.isEmpty()) {
        text.replace('\n', QString("<br>"));
        this->outputWidget->insertHtml(
            QString("<span class='stderr'>%1</span>").arg(text));
    }
}

void PathActionDialog::setProcessStarted()
{
    qInfo() << "process started";
    this->statusBar->showMessage("Process running.");
}

void PathActionDialog::setProcessFinished(int code, int status)
{
    qInfo() << "process setProcessFinished"
            << "code:" << code
            << "status:" << status;
    this->stopButton->setEnabled(false);
    switch (code) {
    case EXIT_SUCCESS:
        this->statusBar->showMessage("Process finished.");
        break;
    default:
        this->statusBar->showMessage(
            QString("Process failed (%1): %2.")
                .arg(code).arg(errorString(noExplanationMsg)));
        break;
    }
}

void PathActionDialog::setErrorOccurred(int error)
{
    this->statusBar->showMessage(
        QString("Process error occurred (%1): %2.")
            .arg(error).arg(errorString(noExplanationMsg)));
}
