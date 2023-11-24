#include <QVBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QValidator>
#include <QFileInfo>
#include <QFileDialog>
#include <QPushButton>
#include <QDir>
#include <QCloseEvent>
#include <QSpinBox>

#include "settings.h"
#include "settingsdialog.h"

using namespace Settings;

namespace {

constexpr auto minimumTimeMsecs = 250;
constexpr auto maximumTimeMsecs = 60000;
constexpr auto timeStepMsecs = 250;

constexpr auto badValueStyle = "background-color: rgb(255, 170, 170);";
constexpr auto goodValueStyle = "background-color: rgb(170, 255, 170);";

constexpr auto filters = QDir::Executable|
                         QDir::AllEntries|
                         QDir::CaseSensitive|
                         QDir::Hidden|
                         QDir::NoDotAndDotDot;

}

class ExecutableValidator: public QValidator {
public:
    ExecutableValidator(QObject *parent = nullptr);
    auto validate(QString &, int &) const -> State override;
};

ExecutableValidator::ExecutableValidator(
    QObject *parent) : QValidator(parent)
{}

auto ExecutableValidator::validate(QString &input, int &pos) const
    -> QValidator::State
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
    while (true) {
        const auto dir = QDir(base);
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
    }
    return QValidator::Invalid;
}

SettingsDialog::SettingsDialog(QWidget *parent):
    QDialog{parent},
    saveButton{new QPushButton{this}},
    closeButton{new QPushButton{this}},
    tmutilPathLbl{new QLabel{this}},
    tmutilPathEdit{new QLineEdit{this}},
    tmutilPathBtn{new QPushButton{this}},
    tmutilStatTimeLbl{new QLabel{this}},
    tmutilStatTimeEdit{new QSpinBox{this}},
    tmutilDestTimeLbl{new QLabel{this}},
    tmutilDestTimeEdit{new QSpinBox{this}},
    tmutilPathValidator{new ExecutableValidator{this}}
{
    this->saveButton->setObjectName("saveButton");
    this->saveButton->setText(tr("Save"));
    this->closeButton->setObjectName("closeButton");
    this->closeButton->setText(tr("Close"));
    this->origPathStyle = this->tmutilPathEdit->styleSheet();
    this->setAttribute(Qt::WA_DeleteOnClose);
    this->setWindowTitle(tr("Preferences"));
    this->tmutilPathLbl->setText(tr("Time Machine utility"));
    this->tmutilPathEdit->setValidator(
        this->tmutilPathValidator);
    this->tmutilPathBtn->setText(tr("Choose..."));
    this->tmutilPathBtn->setDefault(false);
    this->tmutilPathBtn->setAutoDefault(false);

    this->tmutilStatTimeLbl->setText(tr("Backup Status Interval"));
    this->tmutilStatTimeEdit->setRange(minimumTimeMsecs, maximumTimeMsecs);
    this->tmutilStatTimeEdit->setSingleStep(timeStepMsecs);
    this->tmutilStatTimeEdit->setSuffix(" ms");
    this->tmutilStatTimeEdit->setAlignment(Qt::AlignRight);
    this->origStatTimeStyle = this->tmutilStatTimeEdit->styleSheet();

    this->tmutilDestTimeLbl->setText(tr("Destinations Interval"));
    this->tmutilDestTimeEdit->setRange(minimumTimeMsecs, maximumTimeMsecs);
    this->tmutilDestTimeEdit->setSingleStep(timeStepMsecs);
    this->tmutilDestTimeEdit->setSuffix(" ms");
    this->tmutilDestTimeEdit->setAlignment(Qt::AlignRight);
    this->origDestTimeStyle = this->tmutilDestTimeEdit->styleSheet();

    this->setLayout([this]() {
        auto *mainLayout = new QVBoxLayout;
        mainLayout->addLayout([this]() {
            auto layout = new QGridLayout;
            layout->setColumnStretch(1, 1);
            layout->addWidget(this->tmutilPathLbl, 0, 0);
            layout->addWidget(this->tmutilPathEdit, 0, 1);
            layout->addWidget(this->tmutilPathBtn, 0, 2);
            layout->addWidget(this->tmutilStatTimeLbl, 1, 0);
            layout->addWidget(this->tmutilStatTimeEdit, 1, 1);
            layout->addWidget(this->tmutilDestTimeLbl, 2, 0);
            layout->addWidget(this->tmutilDestTimeEdit, 2, 1);
            return layout;
        }());
        mainLayout->addLayout([this]() {
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

    connect(this->tmutilPathEdit, &QLineEdit::editingFinished,
            this, &SettingsDialog::handleTmutilPathFinished);
    connect(this->tmutilPathEdit, &QLineEdit::textChanged,
            this, &SettingsDialog::handleTmutilPathChanged);
    connect(this->tmutilPathBtn, &QPushButton::clicked,
            this, &SettingsDialog::openTmutilPathDialog);

    connect(this->tmutilStatTimeEdit, &QSpinBox::valueChanged,
            this, &SettingsDialog::handleStatTimeChanged);
    connect(this->tmutilDestTimeEdit, &QSpinBox::valueChanged,
            this, &SettingsDialog::handleDestTimeChanged);

    this->tmutilPathEdit->setText(tmutilPath());
    this->tmutilStatTimeEdit->setValue(tmutilStatInterval());
    this->tmutilDestTimeEdit->setValue(tmutilDestInterval());

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
    if (!this->tmutilPathEdit->hasAcceptableInput()) {
        qDebug() << "SettingsDialog rejecting reject on account of bad input";
        return;
    }
    this->close();
}

auto SettingsDialog::allAcceptable() const -> bool
{
    if (!this->tmutilPathEdit->hasAcceptableInput()) {
        return false;
    }
    if (!this->tmutilStatTimeEdit->hasAcceptableInput()) {
        return false;
    }
    if (!this->tmutilDestTimeEdit->hasAcceptableInput()) {
        return false;
    }
    return true;
}

auto SettingsDialog::anyChanged() const -> bool
{
    if (tmutilPath() != this->tmutilPathEdit->text()) {
        return true;
    }
    if (tmutilStatInterval() != this->tmutilStatTimeEdit->value()) {
        return true;
    }
    if (tmutilDestInterval() != this->tmutilDestTimeEdit->value()) {
        return true;
    }
    return false;
}

void SettingsDialog::handleTmutilPathFinished()
{
    const auto newValue = this->tmutilPathEdit->text();
    if (this->tmutilPathEdit->hasAcceptableInput()) {
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

    if (this->tmutilPathEdit->hasAcceptableInput()) {
        qDebug() << "handleTmutilPathChanged acceptable:" << value;
        const auto changed = tmutilPath() != value;
        const auto styleSheet = changed
                ? QString(goodValueStyle)
                                    : this->origPathStyle;
        this->tmutilPathEdit->setStyleSheet(styleSheet);
    }
    else {
        qDebug() << "handleTmutilPathChanged unacceptable:" << value;
        this->tmutilPathEdit->setStyleSheet(badValueStyle);
    }
}

void SettingsDialog::handleStatTimeChanged(int value)
{
    qDebug() << "SettingsDialog::handleStatTimeChanged called"
             << value;
    this->closeButton->setEnabled(allAcceptable() && !anyChanged());
    this->saveButton->setEnabled(allAcceptable() && anyChanged());
    const auto changed = tmutilStatInterval() != value;
    const auto styleSheet = changed
                                ? QString(goodValueStyle)
                                : this->origStatTimeStyle;
    this->tmutilStatTimeEdit->setStyleSheet(styleSheet);
}

void SettingsDialog::handleDestTimeChanged(int value)
{
    qDebug() << "SettingsDialog::handleDestTimeChanged called"
             << value;
    this->closeButton->setEnabled(allAcceptable() && !anyChanged());
    this->saveButton->setEnabled(allAcceptable() && anyChanged());
    const auto changed = tmutilDestInterval() != value;
    const auto styleSheet = changed
                                ? QString(goodValueStyle)
                                : this->origDestTimeStyle;
    this->tmutilDestTimeEdit->setStyleSheet(styleSheet);
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
        const auto newValue = this->tmutilPathEdit->text();
        this->tmutilPathEdit->setStyleSheet(this->origPathStyle);
        if (oldValue != newValue) {
            setTmutilPath(newValue);
            emit tmutilPathChanged(newValue);
        }
    }
    {
        const auto oldValue = tmutilStatInterval();
        const auto newValue = this->tmutilStatTimeEdit->value();
        this->tmutilStatTimeEdit->setStyleSheet(this->origStatTimeStyle);
        if (oldValue != newValue) {
            setTmutilStatInterval(newValue);
            emit tmutilStatusIntervalChanged(newValue);
        }
    }
    {
        const auto oldValue = tmutilDestInterval();
        const auto newValue = this->tmutilDestTimeEdit->value();
        this->tmutilDestTimeEdit->setStyleSheet(this->origDestTimeStyle);
        if (oldValue != newValue) {
            setTmutilDestInterval(newValue);
            emit tmutilDestinationsIntervalChanged(newValue);
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
            this->tmutilPathEdit->setText(files.first());
        }
    }
}
