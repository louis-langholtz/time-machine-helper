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
    statusBar{new QStatusBar{this}}
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

    setLayout([this](){
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

void PathActionDialog::startAction()
{
    this->yesButton->setEnabled(false);
    this->noButton->setEnabled(false);
    this->outputWidget->setEnabled(true);

    auto argList = QStringList();
    argList << this->verb;
    for (const auto& path: pathList) {
        argList << "-p" << path;
    }
    this->process = new QProcess(this);
    connect(this->process, &QProcess::started,
            this, &PathActionDialog::setProcessStarted);
    connect(this->process, &QProcess::finished,
            this, &PathActionDialog::setProcessFinished);
    connect(this->process, &QProcess::readyReadStandardOutput,
            this, &PathActionDialog::readProcessOuput);
    connect(this->process, &QProcess::readyReadStandardError,
            this, &PathActionDialog::readProcessError);
    this->statusBar->showMessage("Starting process");
    this->process->start("tmutil", argList, QIODeviceBase::ReadOnly);
}

void PathActionDialog::readProcessOuput()
{
    const auto data = this->process->readAllStandardOutput();
    this->outputWidget->append(QString(data));
}

void PathActionDialog::readProcessError()
{
    const auto data = this->process->readAllStandardError();
    this->outputWidget->append(QString(data));
}

void PathActionDialog::setProcessStarted()
{
    qInfo() << "process started";
    this->statusBar->showMessage("Process started.");
}

void PathActionDialog::setProcessFinished(int code, int status)
{
    qInfo() << "process finished";
    this->outputWidget->setEnabled(false);
    this->statusBar->showMessage("Process finished.");
}
