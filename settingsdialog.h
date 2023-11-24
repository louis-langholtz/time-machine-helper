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

    explicit SettingsDialog(QWidget *parent = nullptr);

    void closeEvent(QCloseEvent *event) override;
    void reject() override;

    [[nodiscard]] auto allAcceptable() const -> bool;
    [[nodiscard]] auto anyChanged() const -> bool;

signals:
    void tmutilPathChanged(const QString& path);
    void tmutilStatusIntervalChanged(int newMsecs);
    void tmutilDestinationsIntervalChanged(int newMsecs);

private:
    void openTmutilPathDialog();
    void handleTmutilPathFinished();
    void handleTmutilPathChanged(const QString& value);
    void handleStatTimeChanged(int value);
    void handleDestTimeChanged(int value);
    void save();

    QPushButton *saveButton{};
    QPushButton *closeButton{};

    QString origPathStyle;
    QLabel *tmutilPathLbl{};
    QLineEdit *tmutilPathEdit{};
    QPushButton *tmutilPathBtn{};

    QString origStatTimeStyle;
    QLabel *tmutilStatTimeLbl{};
    QSpinBox *tmutilStatTimeEdit{};

    QString origDestTimeStyle;
    QLabel *tmutilDestTimeLbl{};
    QSpinBox *tmutilDestTimeEdit{};

    ExecutableValidator *tmutilPathValidator{};
};

#endif // SETTINGSDIALOG_H
