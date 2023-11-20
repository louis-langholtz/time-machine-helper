#include <unistd.h> // for isatty, setsid

#include <csignal>

#include <QCloseEvent>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QTextEdit>
#include <QPushButton>
#include <QProcess>
#include <QStatusBar>
#include <QSplitter>
#include <QScrollBar>
#include <QTimer>

#include "pathactiondialog.h"

namespace {

constexpr auto openMode =
    QProcess::ReadWrite|QProcess::Text|QProcess::Unbuffered;
constexpr auto noExplanationMsg =
    "no explanation";

QString toHtmlList(const QStringList& strings)
{
    QString result;
    result.append("<pre>");
    for (const auto& string: strings) {
        result.append(string);
        result.append('\n');
    }
    result.append("</pre>");
    return result;
}

}

PathActionDialog::PathActionDialog(QWidget *parent):
    QDialog{parent},
    splitter{new QSplitter{this}},
    textLabel{new QLabel{this}},
    pathsWidget{new QTextEdit{this}},
    yesButton{new QPushButton{"Yes", this}},
    noButton{new QPushButton{"No", this}},
    stopButton{new QPushButton{"Stop", this}},
    dismissButton{new QPushButton{"Dismiss", this}},
    outputWidget{new QTextEdit{this}},
    statusBar{new QStatusBar{this}},
    env{QProcessEnvironment::InheritFromParent},
    stopSig{SIGINT} // tmutil handles SIGINT most gracefully.
{
    this->setAttribute(Qt::WA_DeleteOnClose);
    this->setWindowTitle(tr("Path Action Dialog"));

    this->splitter->setOrientation(Qt::Vertical);
    this->splitter->setChildrenCollapsible(false);

    this->textLabel->setFont([this](){
        QFont font = this->textLabel->font();
        font.setWeight(QFont::Bold);
        return font;
    }());
    this->textLabel->setObjectName("textLabel");

    this->pathsWidget->setObjectName("pathsWidget");
    this->pathsWidget->setReadOnly(true);
    this->pathsWidget->setLineWrapMode(QTextEdit::NoWrap);
    this->pathsWidget->setSizePolicy(
        QSizePolicy::Preferred, QSizePolicy::Maximum);
    this->pathsWidget->viewport()->setSizePolicy(
        QSizePolicy::Preferred, QSizePolicy::Maximum);

    this->yesButton->setEnabled(false);

    this->noButton->setEnabled(false);
    this->noButton->setDefault(true);

    this->stopButton->setEnabled(false);

    this->dismissButton->setEnabled(false);

    this->outputWidget->setObjectName("outputWidget");
    this->outputWidget->setLineWrapMode(QTextEdit::NoWrap);
    this->outputWidget->setReadOnly(true);
    this->outputWidget->document()->setDefaultStyleSheet(
        "* {font-family: \"Andale Mono\";} .stdout {color:green;} .stderr {color:red;}");
    this->outputWidget->setMinimumHeight(0);
    this->outputWidget->setSizePolicy(
        QSizePolicy::Preferred, QSizePolicy::Expanding);
    this->outputWidget->viewport()->setMinimumHeight(0);
    this->outputWidget->viewport()->setSizePolicy(
        QSizePolicy::Preferred, QSizePolicy::Expanding);

    this->statusBar->showMessage("Awaiting confirmation of action.");

    this->setLayout([this](){
        auto *mainLayout = new QVBoxLayout;
        mainLayout->setObjectName("mainLayout");
        {
            auto *frame = new QFrame;
            frame->setFrameStyle(QFrame::StyledPanel);
            frame->setLayout([this]() -> QLayout* {
                auto *frameLayout = new QVBoxLayout;
                frameLayout->addWidget(this->textLabel);
                frameLayout->addWidget(this->pathsWidget);
                frameLayout->addLayout([this](){
                    auto *layout = new QHBoxLayout;
                    layout->setObjectName("choiceLayout");
                    layout->addWidget(this->yesButton);
                    layout->addWidget(this->noButton);
                    layout->addWidget(this->stopButton);
                    layout->addWidget(this->dismissButton);
                    layout->setAlignment(Qt::AlignCenter);
                    return layout;
                }());
                return frameLayout;
            }());
            frame->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
            this->splitter->addWidget(frame);
        }
        {
            auto *frame = new QFrame;
            frame->setFrameStyle(QFrame::StyledPanel);
            frame->setLayout([this]() -> QLayout* {
                auto *frameLayout = new QVBoxLayout;
                frameLayout->addWidget(this->outputWidget);
                frameLayout->addWidget(this->statusBar);
                return frameLayout;
            }());
            frame->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
            this->splitter->addWidget(frame);
        }
        mainLayout->addWidget(this->splitter);
        return mainLayout;
    }());

    connect(this->noButton, &QPushButton::clicked,
            this, &PathActionDialog::close);
    connect(this->stopButton, &QPushButton::clicked,
            this, &PathActionDialog::stopAction);
    connect(this->dismissButton, &QPushButton::clicked,
            this, &PathActionDialog::close);
}

PathActionDialog::~PathActionDialog()
{
    qDebug() << "~PathActionDialog called";
}

void PathActionDialog::closeEvent(QCloseEvent *event)
{
    if (event && this->process) {
        qDebug() << "PathActionDialog::closeEvent ignoring";
        event->ignore();
    }
}

void PathActionDialog::reject()
{
    if (this->process) {
        qDebug() << "PathActionDialog rejecting reject on account of process";
        return;
    }
    this->close();
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

QString PathActionDialog::pathPrefix() const
{
    return this->pathPre;
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
#if 1
    const auto* doc = this->pathsWidget->document();
    const auto* sb = this->pathsWidget->horizontalScrollBar();
    const auto fm = QFontMetrics(this->pathsWidget->currentFont());
    const auto margins = this->pathsWidget->contentsMargins();
    const auto h = (doc->size().toSize().height()) +
                   ((doc->documentMargin() + this->pathsWidget->frameWidth()) * 2) +
                   margins.top() + margins.bottom() + (sb? sb->height(): 0);
    qDebug() << "setPaths setting max h:" << h;
    this->pathsWidget->setMaximumHeight(h);
    this->pathsWidget->viewport()->setMaximumHeight(h);
#endif
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

void PathActionDialog::setPathPrefix(const QString &prefix)
{
    this->pathPre = prefix;
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
        if (this->askPass) {
            argList << "--askpass";
        }
        //argList << "-s";
        //argList << "-w";
        argList << this->tmuPath;
    }
    argList << this->verb;
    for (const auto& path: pathList) {
        if (!this->pathPre.isEmpty()) {
            argList << this->pathPre;
        }
        argList << path;
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
    this->dismissButton->setEnabled(false);
    this->statusBar->showMessage("Starting process");
    this->process->setProcessEnvironment(this->env);
    this->process->start(program, argList, openMode);
}

void PathActionDialog::stopAction()
{
    qDebug() << "stopAction called for:" << this->verb;
    if (this->process && this->process->state() == QProcess::Running) {
        qDebug() << "stopAction kill with:"
                 << this->stopSig
                 << this->verb;
        const auto res = ::kill(this->process->processId(), this->stopSig);
        if (res == -1) {
            qWarning() << "kill failed:" << strerror(errno);
        }
        QTimer::singleShot(1000, this->process, &QProcess::terminate);
        QTimer::singleShot(2000, this->process, &QProcess::kill);
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
    this->dismissButton->setEnabled(true);
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
    this->process->deleteLater();
    this->process = nullptr;
}

void PathActionDialog::setErrorOccurred(int error)
{
    this->statusBar->showMessage(
        QString("Process error occurred (%1): %2.")
            .arg(error).arg(errorString(noExplanationMsg)));
}
