#include <QSettings>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QValidator>
#include <QFileInfo>
#include <QFileDialog>
#include <QPushButton>
#include <QDir>
#include <QCloseEvent>

#include "settingsdialog.h"

namespace {

constexpr auto tmutilSettingsKey = "tmutil_path";
constexpr auto defaultTmutilPath = "/usr/bin/tmutil";

constexpr auto filters = QDir::Executable|
                         QDir::AllEntries|
                         QDir::CaseSensitive|
                         QDir::Hidden|
                         QDir::NoDotAndDotDot;

QSettings &settings()
{
    static QSettings the;
    return the;
}

}

class ExecutableValidator: public QValidator {
public:
    ExecutableValidator(QObject *parent = nullptr);
    State validate(QString &, int &) const;
};

ExecutableValidator::ExecutableValidator(
    QObject *parent) : QValidator(parent)
{}

QValidator::State ExecutableValidator::validate(
    QString &input, int &pos) const
{
    const auto info = QFileInfo(input);
    if (info.isFile() && info.isExecutable()) {
        return QValidator::Acceptable;
    }

    qDebug() << "not acceptable, pos:" << pos;

    const auto separatorChar = QDir::separator();
    auto base = input;
    const auto index = base.lastIndexOf(separatorChar);
    auto pre = QString{};
    if (index >= 0) {
        pre = base.sliced(index + 1);
        base.truncate(index + 1);
    }
    else {
        pre = base;
        base = "./";
    }
    qDebug() << "base is:" << base;
    qDebug() << "pre is:" << pre;
    do {
        auto dir = QDir(base);
        const auto nameFilter = QString("%1*").arg(pre);
        qDebug() << "nameFilter:" << nameFilter;
        const auto choices = dir.entryList(QStringList{nameFilter},
                                           filters);
        if (!choices.empty()) {
            qDebug() << "choices:" << choices;
            return QValidator::Intermediate;
        }
        return QValidator::Invalid;
        if (pre.isEmpty()) {
            break;
        }
        pre.chop(1);
    } while (true);
    return QValidator::Invalid;
}

QString SettingsDialog::tmutilPath()
{
    return settings().value(tmutilSettingsKey,
                            QString(defaultTmutilPath)).toString();
}

SettingsDialog::SettingsDialog(QWidget *parent):
    QDialog{parent},
    saveButton{new QPushButton{this}},
    closeButton{new QPushButton{this}},
    tmutilPathLabel{new QLabel{this}},
    tmutilPathEditor{new QLineEdit{this}},
    tmutilPathButton{new QPushButton{this}},
    tmutilPathValidator{new ExecutableValidator{this}}
{
    this->saveButton->setObjectName("saveButton");
    this->saveButton->setText(tr("Save"));
    this->closeButton->setObjectName("closeButton");
    this->closeButton->setText(tr("Close"));
    this->originalEditorStyleSheet = this->tmutilPathEditor->styleSheet();
    this->setAttribute(Qt::WA_DeleteOnClose);
    this->setWindowTitle(tr("Preferences"));
    this->tmutilPathLabel->setText(tr("Time Machine utility"));
    this->tmutilPathEditor->setValidator(
        this->tmutilPathValidator);
    this->tmutilPathButton->setText(tr("Choose..."));
    this->tmutilPathButton->setDefault(false);
    this->tmutilPathButton->setAutoDefault(false);
    this->setLayout([this]() -> QLayout* {
        auto mainLayout = new QVBoxLayout;
        mainLayout->addLayout([this]() -> QLayout*{
            auto layout = new QHBoxLayout;
            layout->addWidget(this->tmutilPathLabel);
            layout->addWidget(this->tmutilPathEditor);
            layout->addWidget(this->tmutilPathButton);
            return layout;
        }());
        mainLayout->addLayout([this]() -> QLayout*{
            auto layout = new QHBoxLayout;
            layout->addWidget(this->saveButton);
            layout->addWidget(this->closeButton);
            layout->setAlignment(Qt::AlignCenter);
            return layout;
        }());
        return mainLayout;
    }());

    connect(this->saveButton, &QPushButton::clicked,
            this, &SettingsDialog::save);
    connect(this->closeButton, &QPushButton::clicked,
            this, &SettingsDialog::close);
    connect(this->tmutilPathEditor, &QLineEdit::editingFinished,
            this, &SettingsDialog::handleTmutilPathFinished);
    connect(this->tmutilPathEditor, &QLineEdit::textChanged,
            this, &SettingsDialog::handleTmutilPathChanged);
    connect(this->tmutilPathButton, &QPushButton::clicked,
            this, &SettingsDialog::openTmutilPathDialog);

    this->tmutilPathEditor->setText(tmutilPath());
    this->saveButton->setEnabled(false);
    this->closeButton->setEnabled(allAcceptable());
}

void SettingsDialog::closeEvent(QCloseEvent *event)
{
    if (!event || !anyChanged()) {
        return;
    }
    qDebug() << "SettingsDialog::closeEvent ignoring";
    event->ignore();
}

void SettingsDialog::reject()
{
    if (!this->tmutilPathEditor->hasAcceptableInput()) {
        qDebug() << "SettingsDialog rejecting reject on account of bad input";
        return;
    }
    this->close();
}

bool SettingsDialog::allAcceptable() const
{
    if (!this->tmutilPathEditor->hasAcceptableInput()) {
        return false;
    }
    return true;
}

bool SettingsDialog::anyChanged() const
{
    if (tmutilPath() != this->tmutilPathEditor->text()) {
        return true;
    }
    return false;
}

void SettingsDialog::handleTmutilPathFinished()
{
    const auto newValue = this->tmutilPathEditor->text();
    if (this->tmutilPathEditor->hasAcceptableInput()) {
        qDebug() << "handleTmutilPathFinished good!";
        const auto oldValue = tmutilPath();
        if (oldValue != newValue) {
            this->saveButton->setEnabled(true);
        }
        return;
    }
    qDebug() << "handleTmutilPathFinished bad"
             << newValue;
}

void SettingsDialog::handleTmutilPathChanged(const QString &value)
{
    this->closeButton->setEnabled(allAcceptable() && !anyChanged());
    this->saveButton->setEnabled(allAcceptable() && anyChanged());

    if (this->tmutilPathEditor->hasAcceptableInput()) {
        qDebug() << "handleTmutilPathChanged acceptable:" << value;
        const auto changed = tmutilPath() != value;
        const auto styleSheet = changed
                                    ? QString("background-color: rgb(170, 255, 170);")
                                    : this->originalEditorStyleSheet;
        this->tmutilPathEditor->setStyleSheet(styleSheet);
    }
    else {
        qDebug() << "handleTmutilPathChanged unacceptable:" << value;
        this->tmutilPathEditor->setStyleSheet("background-color: rgb(255, 170, 170);");
    }
}

void SettingsDialog::save()
{
    if (!allAcceptable()) {
        return;
    }
    if (!anyChanged()) {
        return;
    }
    {
        const auto oldValue = tmutilPath();
        const auto newValue = this->tmutilPathEditor->text();
        this->tmutilPathEditor->setStyleSheet(this->originalEditorStyleSheet);
        if (oldValue != newValue) {
            settings().setValue(tmutilSettingsKey, newValue);
            emit tmutilPathChanged(newValue);
        }
    }
    this->saveButton->setEnabled(false);
    this->accept();
}

void SettingsDialog::openTmutilPathDialog()
{
    QFileDialog dialog{this};
    dialog.setWindowTitle(tr("Executable File"));
    dialog.setDirectory("/");
    dialog.setFileMode(QFileDialog::ExistingFile);
    //dialog.setOptions(QFileDialog::DontUseNativeDialog);
    dialog.setFilter(QDir::Hidden|QDir::AllEntries);
    dialog.setNameFilter("*");
    if (dialog.exec()) {
        const auto files = dialog.selectedFiles();
        qDebug() << "openFileDialog:" << files;
        if (!files.isEmpty()) {
            this->tmutilPathEditor->setText(files.first());
        }
    }
}
