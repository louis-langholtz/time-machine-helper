#include <unistd.h> // for isatty, setsid

#include <csignal>
#include <system_error>

#include <QCheckBox>
#include <QCloseEvent>
#include <QGroupBox>
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
#include <QLineEdit>

#include "pathactiondialog.h"

namespace {

constexpr auto oneSecondsInMS = 1000;
constexpr auto twoSecondsInMS = 2000;

constexpr auto openMode =
    QProcess::ReadWrite|QProcess::Text|QProcess::Unbuffered;
constexpr auto noExplanationMsg =
    "no explanation";

auto toHtmlList(const QStringList &strings) -> QString
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
    withAdminCheckBox{new QCheckBox{"As Admin", this}},
    withAskPassCheckBox{new QCheckBox{this}},
    yesButton{new QPushButton{"Yes", this}},
    noButton{new QPushButton{"No", this}},
    stopButton{new QPushButton{"Stop", this}},
    dismissButton{new QPushButton{"Dismiss", this}},
    processIoLayout{new QVBoxLayout{}},
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

    this->withAdminCheckBox->setChecked(this->withAdmin);
    this->withAdminCheckBox->setToolTip(
        tr("Check this box to run the action with administrator "
           "privileges (using \"sudo\")"));

    this->withAskPassCheckBox->setText(
        tr("External Password Prompter"));
    this->withAskPassCheckBox->setToolTip(
        "Check this box to use an external password prompting "
        "application (that supports sudo's \"--askpass\" option)."
        " Otherwise, this application will prompt you itself if "
        "required.");
    this->withAskPassCheckBox->setChecked(this->withAskPass);
    this->withAskPassCheckBox->setEnabled(this->withAdmin);

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

    this->processIoLayout->addWidget(this->outputWidget);
    this->processIoLayout->addWidget(this->statusBar);

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
                const auto options = new QGroupBox{"With these options?", this};
                options->setFont(this->textLabel->font());
                options->setFlat(true);
                options->setLayout([this](){
                    auto *layout = new QHBoxLayout;
                    layout->setAlignment(Qt::AlignLeft);
                    layout->addWidget(this->withAdminCheckBox);
                    layout->addWidget(this->withAskPassCheckBox);
                    return layout;
                }());
                frameLayout->addWidget(options);
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
            frame->setSizePolicy(QSizePolicy::Preferred,
                                 QSizePolicy::Maximum);
            this->splitter->addWidget(frame);
        }
        {
            auto *frame = new QFrame;
            frame->setFrameStyle(QFrame::StyledPanel);
            frame->setLayout(this->processIoLayout);
            frame->setSizePolicy(QSizePolicy::Preferred,
                                 QSizePolicy::Minimum);
            this->splitter->addWidget(frame);
        }
        mainLayout->addWidget(this->splitter);
        return mainLayout;
    }());

    connect(this->withAdminCheckBox, &QCheckBox::stateChanged,
            this, &PathActionDialog::changeAsRoot);
    connect(this->withAskPassCheckBox, &QCheckBox::stateChanged,
            this, &PathActionDialog::changeAskPass);
    connect(this->noButton, &QPushButton::clicked,
            this, &PathActionDialog::close);
    connect(this->stopButton, &QPushButton::clicked,
            this, &PathActionDialog::stopAction);
    connect(this->dismissButton, &QPushButton::clicked,
            this, &PathActionDialog::close);
}

PathActionDialog::~PathActionDialog()
{
    qDebug() << "~PathActionDialog called while"
             << (this->process? "processing": "quiet");
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

auto PathActionDialog::errorString(const QString &fallback) const
    -> QString
{
    return this->process
               ? this->process->errorString()
               : fallback;
}

auto PathActionDialog::text() const -> QString
{
    return this->textLabel->text();
}

auto PathActionDialog::paths() const -> QStringList
{
    return this->pathList;
}

auto PathActionDialog::firstArgs() const -> QStringList
{
    return this->beginList;
}

auto PathActionDialog::lastArgs() const -> QStringList
{
    return this->endList;
}

auto PathActionDialog::action() const -> QString
{
    return this->verb;
}

auto PathActionDialog::asRoot() const noexcept -> bool
{
    return this->withAdmin;
}

auto PathActionDialog::environment() const -> QProcessEnvironment
{
    return this->env;
}

auto PathActionDialog::tmutilPath() const -> QString
{
    return this->tmuPath;
}

auto PathActionDialog::sudoPath() const -> QString
{
    return this->suPath;
}

auto PathActionDialog::pathPrefix() const -> QString
{
    return this->pathPre;
}

auto PathActionDialog::stopSignal() const noexcept -> int
{
    return this->stopSig;
}

void PathActionDialog::setText(const QString &text)
{
    this->textLabel->setText(text);
}

void PathActionDialog::setFirstArgs(const QStringList &args)
{
    this->beginList = args;
}

void PathActionDialog::setPaths(const QStringList &paths)
{
    this->pathList = paths;
    this->pathsWidget->setHtml(toHtmlList(paths));

    // todo: get this to work!
    const auto* doc = this->pathsWidget->document();
    const auto* sb = this->pathsWidget->horizontalScrollBar();
    //const auto fm = QFontMetrics(this->pathsWidget->currentFont());
    const auto margins = this->pathsWidget->contentsMargins();
    const auto h = (doc->size().toSize().height()) +
                   (this->pathsWidget->frameWidth() * 2) +
                   margins.top() + margins.bottom() +
                   (sb? sb->height(): 0);
    qDebug() << "setPaths setting max h:" << h;
    this->pathsWidget->setMaximumHeight(h);
    this->pathsWidget->viewport()->setMaximumHeight(h);
}

void PathActionDialog::setLastArgs(const QStringList &args)
{
    this->endList = args;
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

void PathActionDialog::setAsRoot(bool value)
{
    this->withAdmin = value;
    this->withAdminCheckBox->setChecked(value);
    this->withAskPassCheckBox->setEnabled(value);
}

void PathActionDialog::setAskPass(bool value)
{
    this->withAskPass = value;
    this->withAskPassCheckBox->setChecked(value);
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

void PathActionDialog::setSudoPath(const QString &path)
{
    this->suPath = path;
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
    this->withAdminCheckBox->setEnabled(false);
    this->withAskPassCheckBox->setEnabled(false);

    this->yesButton->setEnabled(false);
    this->noButton->setEnabled(false);

    this->outputWidget->setEnabled(true);

    const auto program = QString((this->withAdmin)
        ? QString(this->suPath): this->tmuPath);

    auto argList = QStringList();
    if (this->withAdmin) {
        if (this->withAskPass) {
            argList << "--askpass";
        }
        else {
            argList << "--stdin";
        }
        argList << this->tmuPath;
    }
    argList << this->verb;
    argList << this->beginList;
    for (const auto& path: this->pathList) {
        if (!this->pathPre.isEmpty()) {
            argList << this->pathPre;
        }
        argList << path;
    }
    argList << this->endList;
    qInfo() << "startAction about to run:"
            << program
            << argList.join(' ');

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
        const auto res =
            ::kill(static_cast<pid_t>(this->process->processId()),
                   this->stopSig);
        if (res == -1) {
            qWarning() << "kill failed:"
                       << std::generic_category().message(errno);
        }
        QTimer::singleShot(oneSecondsInMS, this->process,
                           &QProcess::terminate);
        QTimer::singleShot(twoSecondsInMS, this->process, &QProcess::kill);
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
        if (this->withAdmin && text == "Password:") {
            this->promptForPassword();
            return;
        }
        text.replace('\n', QString("<br>"));
        this->outputWidget->insertHtml(
            QString("<span class='stderr'>%1</span>").arg(text));
    }
}

void PathActionDialog::promptForPassword()
{
    qInfo() << "promptForPassword called";
    if (!this->pwdPromptLabel) {
        this->pwdPromptLabel = new QLabel{this};
        this->pwdPromptLabel->setObjectName("pwdPromptLabel");
        this->pwdPromptLabel->setText(tr("Password:"));
        this->pwdPromptLabel->setToolTip(
            "Your local system login password is being"
            " requested by the running sub-process.");
    }
    if (!this->pwdLineEdit) {
        this->pwdLineEdit = new QLineEdit{this};
        this->pwdLineEdit->setObjectName("pwdLineEdit");
        this->pwdLineEdit->setEchoMode(QLineEdit::Password);
        connect(this->pwdLineEdit, &QLineEdit::returnPressed,
                this, &PathActionDialog::disablePwdLineEdit);
        connect(this->pwdLineEdit, &QLineEdit::returnPressed,
                this, &PathActionDialog::writePasswordToProcess);
        this->processIoLayout->insertLayout(0, [this](){
            auto *layout = new QHBoxLayout;
            layout->setObjectName("passwordLayout");
            layout->addWidget(this->pwdPromptLabel);
            layout->addWidget(this->pwdLineEdit);
            return layout;
        }());
    }
    this->pwdLineEdit->activateWindow();
    this->pwdLineEdit->setEnabled(true);
    this->pwdLineEdit->setFocus();
}

void PathActionDialog::disablePwdLineEdit()
{
    if (const auto widget = this->pwdLineEdit) {
        widget->setEnabled(false);
    }
}

void PathActionDialog::changeAsRoot(int state)
{
    qDebug() << "PathActionDialog::changeAsRoot called for" << state;
    this->setAsRoot(state != Qt::Unchecked);
}

void PathActionDialog::changeAskPass(int state)
{
    qDebug() << "PathActionDialog::changeAskPass called for" << state;
    this->setAskPass(state != Qt::Unchecked);
}

void PathActionDialog::writePasswordToProcess()
{
    if (!this->process || !this->pwdLineEdit) {
        qInfo() << "writePasswordToProcess called when not ready";
        return;
    }
    qDebug() << "writePasswordToProcess called";
    const auto saveChannel = this->process->currentWriteChannel();
    this->process->setCurrentWriteChannel(0);
    auto password = this->pwdLineEdit->text();
    password.append('\n');
    const auto status = this->process->write(password.toUtf8());
    if (status == -1) {
        qDebug() << "writePasswordToProcess errored";
    }
    this->process->setCurrentWriteChannel(saveChannel);
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
    if (this->pwdLineEdit) {
        disconnect(this->pwdLineEdit, &QLineEdit::returnPressed,
                   this, &PathActionDialog::writePasswordToProcess);
    }
}

void PathActionDialog::setErrorOccurred(int error)
{
    this->statusBar->showMessage(
        QString("Process error occurred (%1): %2.")
            .arg(error).arg(errorString(noExplanationMsg)));
}
