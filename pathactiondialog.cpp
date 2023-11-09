#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QTextEdit>
#include <QPushButton>

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
    outputWidget{new QTextEdit{this}}
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

    this->noButton->setDefault(true);

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

void PathActionDialog::setText(const QString &text)
{
    this->textLabel->setText(text);
}

void PathActionDialog::setPaths(const QStringList &paths)
{
    this->pathList = paths;
    this->pathsWidget->setHtml(toHtmlList(paths));
}
