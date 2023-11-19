#ifndef SETTINGSDIALOG_H
#define SETTINGSDIALOG_H

#include <QDialog>
#include <QString>

class QLabel;
class QLineEdit;
class QPushButton;
class ExecutableValidator;

class SettingsDialog : public QDialog
{
    Q_OBJECT
public:
    static QString tmutilPath();

    explicit SettingsDialog(QWidget *parent = nullptr);

    void closeEvent(QCloseEvent *event) override;
    void reject() override;

    bool allAcceptable() const;
    bool anyChanged() const;

signals:
    void tmutilPathChanged(const QString& path);

private slots:
    void openTmutilPathDialog();
    void handleTmutilPathFinished();
    void handleTmutilPathChanged(const QString& value);
    void save();

private:
    QString originalEditorStyleSheet;
    QPushButton *saveButton{};
    QPushButton *closeButton{};
    QLabel *tmutilPathLabel{};
    QLineEdit *tmutilPathEditor{};
    QPushButton *tmutilPathButton{};
    ExecutableValidator *tmutilPathValidator{};
};

#endif // SETTINGSDIALOG_H
