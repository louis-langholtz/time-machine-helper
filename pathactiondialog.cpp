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
#include <QTreeWidget>
#include <QFontDatabase>
#include <QThreadPool>

#include "directoryreader.h"
#include "pathactiondialog.h"

namespace {

constexpr auto zeroSecondsInMS = 0;
constexpr auto oneSecondsInMS = 1000;
constexpr auto twoSecondsInMS = 2000;
constexpr auto indentation = 10;
constexpr auto minimumDialogWidth = 550;

constexpr auto openMode =
    QProcess::ReadWrite|QProcess::Text|QProcess::Unbuffered;

constexpr auto processStoppedMsg = "Process stopped.";
constexpr auto noExplanationMsg = "no explanation";

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

auto findItem(QTreeWidgetItem *item,
              std::filesystem::path::iterator first,
              const std::filesystem::path::iterator& last)
    -> QTreeWidgetItem*
{
    if (!item) {
        return nullptr;
    }
    for (; first != last; ++first) {
        auto foundChild = static_cast<QTreeWidgetItem*>(nullptr);
        const auto count = item->childCount();
        for (auto i = 0; i < count; ++i) {
            const auto child = item->child(i);
            if (child && child->text(0) == first->c_str()) {
                foundChild = child;
                break;
            }
        }
        if (!foundChild) {
            break;
        }
        item = foundChild;
    }
    return (first == last) ? item : nullptr;
}

auto findItem(QTreeWidget& tree,
              const std::filesystem::path::iterator& first,
              const std::filesystem::path::iterator& last)
    -> QTreeWidgetItem*
{
    const auto count = tree.topLevelItemCount();
    for (auto i = 0; i < count; ++i) {
        const auto item = tree.topLevelItem(i);
        if (!item) {
            continue;
        }
        const auto key = item->text(0);
        const auto root = std::filesystem::path{key.toStdString()};
        const auto result = std::mismatch(first, last, root.begin(), root.end());
        if (result.second == root.end()) {
            return findItem(item, result.first, last);
        }
    }
    return nullptr;
}

}

PathActionDialog::PathActionDialog(QWidget *parent):
    QDialog{parent},
    splitter{new QSplitter{this}},
    textLabel{new QLabel{this}},
    pathsWidget{new QTreeWidget{this}},
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
    this->setObjectName("pathActionDialog");
    this->setAttribute(Qt::WA_DeleteOnClose);
    this->setWindowTitle(tr("Path Action Dialog"));
    this->setMinimumWidth(minimumDialogWidth);

    this->splitter->setOrientation(Qt::Vertical);
    this->splitter->setChildrenCollapsible(false);

    this->textLabel->setWordWrap(true);
    this->textLabel->setTextInteractionFlags(
        Qt::TextSelectableByMouse|Qt::LinksAccessibleByMouse);
    this->textLabel->setTextFormat(Qt::TextFormat::MarkdownText);
    this->textLabel->setFont([this](){
        QFont font = this->textLabel->font();
        font.setWeight(QFont::Bold);
        return font;
    }());
    this->textLabel->setObjectName("textLabel");

    this->pathsWidget->setObjectName("pathsWidget");
    this->pathsWidget->setHeaderLabels(QStringList{} << "Path");
    this->pathsWidget->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    this->pathsWidget->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    this->pathsWidget->setSizePolicy(
        QSizePolicy::Preferred, QSizePolicy::Expanding);
    this->pathsWidget->setMinimumHeight(0);
    this->pathsWidget->setIndentation(indentation);
    this->pathsWidget->setSelectionMode(QAbstractItemView::NoSelection);

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
    const auto fixedFontInfo =
        QFontInfo(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    const auto styleSheet =
        QString{"* {font-family: %1; font-size: %2px;} .stdout {color:green;} .stderr {color:red;}"}
                                .arg(fixedFontInfo.family()).arg(fixedFontInfo.pixelSize());
    this->outputWidget->document()->setDefaultStyleSheet(styleSheet);
    this->outputWidget->setMinimumHeight(0);
    this->outputWidget->setSizeAdjustPolicy(QAbstractScrollArea::AdjustIgnored);
    this->outputWidget->setSizePolicy(
        QSizePolicy::Preferred, QSizePolicy::Expanding);
    this->outputWidget->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    this->statusBar->showMessage("Awaiting confirmation of action.");

    this->processIoLayout->addWidget(this->outputWidget);
    this->processIoLayout->addWidget(this->statusBar);
    this->processIoLayout->setStretch(0, 1);
    this->processIoLayout->setStretch(0, 0);

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
                                 QSizePolicy::Expanding);
            this->splitter->addWidget(frame);
        }
        {
            auto *frame = new QFrame;
            frame->setFrameStyle(QFrame::StyledPanel);
            frame->setLayout(this->processIoLayout);
            frame->setMinimumHeight(0);
            this->splitter->addWidget(frame);
        }
        mainLayout->addWidget(this->splitter);
        return mainLayout;
    }());

    connect(this->pathsWidget, &QTreeWidget::itemExpanded,
            this, &PathActionDialog::expandPath);
    connect(this->pathsWidget, &QTreeWidget::itemCollapsed,
            this, &PathActionDialog::collapsePath);
    connect(this->pathsWidget, &QTreeWidget::itemSelectionChanged,
            this, &PathActionDialog::changePathSelection);
    connect(this->withAdminCheckBox, &QCheckBox::stateChanged,
            this, &PathActionDialog::changeAsRoot);
    connect(this->withAskPassCheckBox, &QCheckBox::stateChanged,
            this, &PathActionDialog::changeAskPass);
    connect(this->yesButton, &QPushButton::clicked,
            this, &PathActionDialog::startAction);
    connect(this->noButton, &QPushButton::clicked,
            this, &PathActionDialog::close);
    connect(this->stopButton, &QPushButton::clicked,
            this, &PathActionDialog::stopAction);
    connect(this->dismissButton, &QPushButton::clicked,
            this, &PathActionDialog::close);
}

PathActionDialog::~PathActionDialog()
{
    const auto proc = this->process;
    if (!proc) {
        return;
    }

    qDebug() << "~PathActionDialog called when process state is" << proc->state();
    for (auto stopAttempt = 0; proc->state() != QProcess::NotRunning; ++stopAttempt) {
        switch (stopAttempt) {
        case 0:
            this->stop();
            proc->waitForFinished(oneSecondsInMS);
            break;
        case 1:
            this->terminate();
            proc->waitForFinished(twoSecondsInMS);
            break;
        default:
            this->kill();
            proc->waitForFinished();
            return;
        }
    }
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

auto PathActionDialog::selectable() const -> bool
{
    return this->pathsWidget->selectionMode() !=
           QAbstractItemView::NoSelection;
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
    const auto fixedFont =
        QFontDatabase::systemFont(QFontDatabase::FixedFont);
    this->pathList = paths;
    this->pathsWidget->clear();
    using QTreeWidgetItem::ChildIndicatorPolicy::ShowIndicator;
    using QTreeWidgetItem::ChildIndicatorPolicy::DontShowIndicator;
    const auto policy = this->selectable()
                            ? ShowIndicator
                            : DontShowIndicator;
    for (const auto& path: paths) {
        const auto item = new QTreeWidgetItem{QTreeWidgetItem::UserType};
        item->setFont(0, fixedFont);
        item->setText(0, path);
        item->setData(0, Qt::UserRole,
                      QVariant::fromValue(std::filesystem::path{path.toStdString()}));
        item->setChildIndicatorPolicy(policy);
        item->setSelected(true);
        this->pathsWidget->addTopLevelItem(item);
        item->setSelected(true);
    }
    this->pathsWidget->resizeColumnToContents(0);
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
    }
    else {
        this->yesButton->setEnabled(true);
        this->noButton->setEnabled(true);
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

void PathActionDialog::setSelectable(bool value)
{
    this->pathsWidget->setSelectionMode(value
        ? QAbstractItemView::MultiSelection
        : QAbstractItemView::NoSelection);
}

void PathActionDialog::startAction()
{
    this->pathsWidget->setEnabled(false);
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
            this, &PathActionDialog::handleErrorOccurred);
    connect(this->process, &QProcess::started,
            this, &PathActionDialog::handleProcessStarted);
    connect(this->process, &QProcess::finished,
            this, &PathActionDialog::handleProcessFinished);
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
    const auto proc = this->process;
    if (proc && proc->state() == QProcess::Running) {
        this->userRequestedStop = true;
        this->stop();
        QTimer::singleShot(oneSecondsInMS, this, &PathActionDialog::terminate);
        QTimer::singleShot(twoSecondsInMS, this, &PathActionDialog::kill);
    }
}

void sendSignal(qint64 pid, int sig)
{
    qDebug() << "sendSignal" << sig << "to" << pid;
    const auto res = ::kill(static_cast<pid_t>(pid), sig);
    if (res == -1) {
        qWarning() << "kill(" << pid << "," << sig << ") failed:"
                   << std::generic_category().message(errno);
    }
}

void PathActionDialog::stop()
{
    const auto proc = this->process;
    const auto pid = proc ? proc->processId() : qint64(0);
    if (pid <= 0) {
        return;
    }
    qDebug() << "PathActionDialog::stop"
             << proc->program()
             << proc->arguments()
             << ", pid"
             << pid
             << "with signal"
             << this->stopSig;
    sendSignal(pid, this->stopSig);
}

void PathActionDialog::terminate()
{
    this->stopSig = SIGTERM;
    this->stop();
}

void PathActionDialog::kill()
{
    this->stopSig = SIGKILL;
    this->stop();
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
            QString(
                R"(<span class="stdout" title="From the process's standard output channel.">%1</span>)")
                .arg(text));
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
            QString(
                R"(<span class="stderr" title="From the process's standard error channel.">%1</span>)")
                .arg(text));
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

void PathActionDialog::changePathSelection()
{
    auto newList = QStringList{};
    for (const auto item: this->pathsWidget->selectedItems()) {
        if (!item) {
            continue;
        }
        const auto path = item->data(0, Qt::UserRole)
                              .value<std::filesystem::path>();
        newList << path.c_str();
    }
    this->pathList = newList;
    emit selectedPathsChanged(this, newList);
}

void PathActionDialog::handleReaderEntry(
    const std::filesystem::path &path,
    const std::filesystem::file_status &status,
    const QMap<QString, QByteArray> &)
{
    using QTreeWidgetItem::ChildIndicatorPolicy::ShowIndicator;
    using QTreeWidgetItem::ChildIndicatorPolicy::DontShowIndicator;
    const auto parent = ::findItem(*(this->pathsWidget),
                                   path.begin(), --path.end());
    if (!parent) {
        qDebug() << "PathActionDialog::updateDirEntry parent not found";
        return;
    }
    const auto filename = path.filename().string();
    qDebug() << "PathActionDialog::updateDirEntry for" << filename;
    const auto fixedFont =
        QFontDatabase::systemFont(QFontDatabase::FixedFont);
    const auto item = new QTreeWidgetItem{QTreeWidgetItem::UserType};
    item->setFont(0, fixedFont);
    item->setText(0, QString::fromStdString(filename));
    item->setData(0, Qt::UserRole, QVariant::fromValue(path));
    const auto policy = (status.type() == std::filesystem::file_type::directory)
                            ? ShowIndicator
                            : DontShowIndicator;
    item->setChildIndicatorPolicy(policy);
    parent->addChild(item);
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

void PathActionDialog::handleProcessStarted()
{
    qInfo() << "process started";
    this->statusBar->showMessage("Process running.");
}

auto PathActionDialog::messageForFinish(int code, int status) const
    -> QString
{
    switch (QProcess::ExitStatus(status)) {
    case QProcess::NormalExit:
        if (code == EXIT_SUCCESS) {
            return "Process finished successfully.";
        }
        if (this->userRequestedStop) {
            return processStoppedMsg;
        }
        return QString("Process failed (%1): %2.")
            .arg(code).arg(errorString(noExplanationMsg));
    case QProcess::CrashExit:
        break;
    }
    if (this->userRequestedStop) {
        return processStoppedMsg;
    }
    return "Process exited abnormally.";
}

void PathActionDialog::handleProcessFinished(int code, int status)
{
    qInfo() << "PathActionDialog::handleProcessFinished"
            << "code:" << code
            << "status:" << status;
    this->stopButton->setEnabled(false);
    this->dismissButton->setEnabled(true);
    this->statusBar->showMessage(messageForFinish(code, status));
    this->process->deleteLater();
    this->process = nullptr;
    if (this->pwdLineEdit) {
        disconnect(this->pwdLineEdit, &QLineEdit::returnPressed,
                   this, &PathActionDialog::writePasswordToProcess);
    }
}

void PathActionDialog::handleErrorOccurred(int error)
{
    this->statusBar->showMessage(
        QString("Process error occurred (%1): %2.")
            .arg(error).arg(errorString(noExplanationMsg)));
}

void PathActionDialog::expandPath(QTreeWidgetItem *item)
{
    const auto path = item->data(0, Qt::UserRole).value<std::filesystem::path>();
    qDebug() << "PathActionDialog::expandPath" << path;
    auto *reader = new DirectoryReader(path, this);
    reader->setAutoDelete(true);
    reader->setReadAttributes(false);
    reader->setFilter({QDir::AllEntries});
    connect(reader, &DirectoryReader::entry,
            this, &PathActionDialog::handleReaderEntry);
    connect(this, &PathActionDialog::destroyed,
            reader, &DirectoryReader::requestInterruption);
    QThreadPool::globalInstance()->start(reader);
}

void PathActionDialog::collapsePath( // NOLINT(readability-convert-member-functions-to-static)
    QTreeWidgetItem *item)
{
    qDebug() << "PathActionDialog::collapsePath" << item->text(0);
    for (const auto* child: item->takeChildren()) {
        delete child;
    }
}
