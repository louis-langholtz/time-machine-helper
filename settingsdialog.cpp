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
    constexpr auto filters = QDir::Executable|
                             QDir::AllEntries|
                             QDir::CaseSensitive|
                             QDir::Hidden|
                             QDir::NoDotAndDotDot;
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
    tmutilPathLabel{new QLabel{this}},
    tmutilPathEditor{new QLineEdit{this}},
    tmutilPathButton{new QPushButton{this}},
    tmutilPathValidator{new ExecutableValidator{this}}
{
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
        return mainLayout;
    }());
    connect(this->tmutilPathEditor, &QLineEdit::editingFinished,
            this, &SettingsDialog::handleTmutilPathFinished);
    connect(this->tmutilPathEditor, &QLineEdit::textChanged,
            this, &SettingsDialog::handleTmutilPathChanged);
    connect(this->tmutilPathButton, &QPushButton::clicked,
            this, &SettingsDialog::openFileDialog);
    this->tmutilPathEditor->setText(tmutilPath());
}

void SettingsDialog::closeEvent(QCloseEvent *event)
{
    if (!event) {
        return;
    }
    if (this->tmutilPathEditor->hasAcceptableInput()) {
        return;
    }
    const auto text = this->tmutilPathEditor->text();
    qDebug() << "SettingsDialog::closeEvent ignoring, bad text:" << text;
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

void SettingsDialog::handleTmutilPathFinished()
{
    const auto text = this->tmutilPathEditor->text();
    if (this->tmutilPathEditor->hasAcceptableInput()) {
        qDebug() << "handleTmutilPathFinished good!";
        settings().setValue(tmutilSettingsKey, text);
        this->tmutilPathEditor->setStyleSheet(this->originalEditorStyleSheet);
        emit tmutilPathChanged(text);
        return;
    }
    qDebug() << "handleTmutilPathFinished bad"
             << text;
}

void SettingsDialog::handleTmutilPathChanged(const QString &value)
{
    if (this->tmutilPathEditor->hasAcceptableInput()) {
        qDebug() << "handleTmutilPathChanged acceptable:" << value;
        const auto styleSheet = (value == tmutilPath())
                                    ? this->originalEditorStyleSheet
                                    : QString("background-color: rgb(170, 255, 170);");
        this->tmutilPathEditor->setStyleSheet(styleSheet);
    }
    else {
        qDebug() << "handleTmutilPathChanged unacceptable:" << value;
        this->tmutilPathEditor->setStyleSheet("background-color: rgb(255, 170, 170);");
    }
}

void SettingsDialog::openFileDialog()
{
    const auto fileName = QFileDialog::getOpenFileName(
        this,
        tr("Open File"),
        "/",
        {},
        nullptr,
        QFileDialog::DontUseNativeDialog);
    qDebug() << "openFileDialog:" << fileName;
}
