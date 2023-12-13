#ifndef SETTINGSDIALOG_H
#define SETTINGSDIALOG_H

#include <QDialog>
#include <QString>

class QLabel;
class QLineEdit;
class QSpinBox;
class QPushButton;
class ExecutableValidator;

class SettingsDialog : public QDialog
{
    // NOLINTBEGIN
    Q_OBJECT
    // NOLINTEND

public:
    [[nodiscard]] static auto anyNonDefault() -> bool;

    explicit SettingsDialog(QWidget *parent = nullptr);

    void closeEvent(QCloseEvent *event) override;
    void reject() override;

    [[nodiscard]] auto allAcceptable() const -> bool;
    [[nodiscard]] auto anyChanged() const -> bool;

signals:
    void tmutilPathChanged(const QString& path);
    void sudoPathChanged(const QString& path);
    void tmutilStatusIntervalChanged(int newMsecs);
    void tmutilDestinationsIntervalChanged(int newMsecs);
    void pathInfoIntervalChanged(int newMsecs);
    void allReset();

private:
    void openTmutilPathDialog();
    void openSudoPathDialog();
    void handleTmutilPathFinished();
    void handleSudoPathFinished();
    void handleTmutilPathChanged(const QString& value);
    void handleSudoPathChanged(const QString& value);
    void handleStatTimeChanged(int value);
    void handleDestTimeChanged(int value);
    void handlePathInfoTimeChanged(int value);
    void save();
    void reset();

    QPushButton *saveButton{};
    QPushButton *resetButton{};
    QPushButton *closeButton{};

    QString tmutilPathStyle;
    QLabel *tmutilPathLbl{};
    QLineEdit *tmutilPathEdit{};
    QPushButton *tmutilPathBtn{};

    QString origStatTimeStyle;
    QLabel *tmutilStatTimeLbl{};
    QSpinBox *tmutilStatTimeEdit{};

    QString origDestTimeStyle;
    QLabel *tmutilDestTimeLbl{};
    QSpinBox *tmutilDestTimeEdit{};

    QString sudoPathStyle;
    QLabel *sudoPathLbl{};
    QLineEdit *sudoPathEdit{};
    QPushButton *sudoPathBtn{};

    QString origPathInfoTimeStyle;
    QLabel *pathInfoTimeLbl{};
    QSpinBox *pathInfoTimeEdit{};

    ExecutableValidator *exePathValidator{};
};

#endif // SETTINGSDIALOG_H
