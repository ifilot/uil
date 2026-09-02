#pragma once

#include <QString>
#include <QVector>

/** @brief Describes one presentation shipped beside the application. */
struct ExamplePresentation {
    QString title;
    QString file_name;
    QString absolute_path;
};

/** @brief Returns the examples installed below @p application_directory. */
QVector<ExamplePresentation> installed_example_presentations(
    const QString& application_directory);
