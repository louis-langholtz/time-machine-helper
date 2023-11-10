#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QTextEdit>
#include <QPushButton>
#include <QProcess>
#include <QStatusBar>

#include "pathactiondialog.h"

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

PathActionDialog::PathActionDialog(QWidget *parent):
    QDialog{parent},
    textLabel{new QLabel{this}},
    pathsWidget{new QTextEdit{this}},
    yesButton{new QPushButton{"Yes", this}},
    noButton{new QPushButton{"No", this}},
    outputWidget{new QTextEdit{this}},
    statusBar{new QStatusBar{this}},
    env{QProcessEnvironment::InheritFromParent}
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

    this->noButton->setDefault(true);

    this->outputWidget->setObjectName("outputWidget");
    this->outputWidget->setEnabled(false);
    this->outputWidget->setReadOnly(true);

    this->statusBar->showMessage("Awaiting confirmation of action.");

    this->setLayout([this](){
        auto *mainLayout = new QVBoxLayout;
        mainLayout->setObjectName("mainLayout");
        mainLayout->addWidget(this->textLabel);
        mainLayout->addWidget(this->pathsWidget);
        mainLayout->addLayout([this](){
            auto *choicesLayout = new QHBoxLayout;
            choicesLayout->setObjectName("choicesLayout");
            choicesLayout->addWidget(this->yesButton);
            choicesLayout->addWidget(this->noButton);
            choicesLayout->setAlignment(Qt::AlignCenter);
            return choicesLayout;
        }());
        mainLayout->addWidget(this->outputWidget);
        mainLayout->addWidget(this->statusBar);
        return mainLayout;
    }());

    connect(this->noButton, &QPushButton::clicked,
            this, &PathActionDialog::close);
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

bool PathActionDialog::asRoot() const
{
    return this->withAdmin;
}

QProcessEnvironment PathActionDialog::environment() const
{
    return this->env;
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
        disconnect(this->yesButton, &QPushButton::clicked,
                   this, &PathActionDialog::startAction);
    }
    else {
        this->yesButton->setEnabled(true);
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

void PathActionDialog::startAction()
{
    this->yesButton->setEnabled(false);
    this->noButton->setEnabled(false);
    this->outputWidget->setEnabled(true);

    const auto program = QString((this->withAdmin)? "sudo": this->tmuPath);

    auto argList = QStringList();
    if (this->withAdmin) {
        if (this->askPass) {
            argList << "--askpass";
        }
        argList << this->tmuPath;
    }
    argList << this->verb;
    for (const auto& path: pathList) {
        argList << "-p" << path;
    }
    qInfo() << "About to run:" << program << argList.join(' ');

    this->process = new QProcess(this);
    connect(this->process, &QProcess::errorOccurred,
            this, &PathActionDialog::setErrorOccurred);
    connect(this->process, &QProcess::started,
            this, &PathActionDialog::setProcessStarted);
    connect(this->process, &QProcess::finished,
            this, &PathActionDialog::setProcessFinished);
    connect(this->process, &QProcess::readyReadStandardOutput,
            this, &PathActionDialog::readProcessOuput);
    connect(this->process, &QProcess::readyReadStandardError,
            this, &PathActionDialog::readProcessError);
    this->statusBar->showMessage("Starting process");
    this->process->setProcessEnvironment(this->env);
    this->process->start(program, argList, QIODeviceBase::ReadOnly);
}

void PathActionDialog::readProcessOuput()
{
    const auto data = this->process->readAllStandardOutput();
    if (!data.isEmpty()) {
        this->outputWidget->append(
            QString("<small><tt>%1</tt><br/></small>").arg(data));
    }
}

void PathActionDialog::readProcessError()
{
    const auto data = this->process->readAllStandardError();
    if (!data.isEmpty()) {
        this->outputWidget->append(
            QString("<font color='red'><small><tt>%1</tt></small></font><br/>").arg(data));
    }
}

void PathActionDialog::setProcessStarted()
{
    qInfo() << "process started";
    this->statusBar->showMessage("Process started.");
    this->readProcessOuput();
    this->readProcessError();
}

void PathActionDialog::setProcessFinished(int code, int status)
{
    this->readProcessOuput();
    this->readProcessError();
    switch (code) {
    case EXIT_SUCCESS:
        this->statusBar->showMessage("Process succeeded.");
        break;
    default:
        this->statusBar->showMessage(
            QString("Process failed (exit code %1).").arg(code));
        break;
    }
}

void PathActionDialog::setErrorOccurred(int error)
{
    this->statusBar->showMessage(QString("Process error occurred (%1).").arg(error));
}
