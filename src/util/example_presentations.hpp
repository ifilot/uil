#pragma once

#include <QString>
#include <QVector>

/** @brief Describes one presentation shipped beside the application. */
struct ExamplePresentation {
    /** @brief Title shown in the File > Examples menu. */
    QString title;
    /** @brief PDF file name inside the `examples` directory. */
    QString file_name;
    /** @brief Absolute path of the installed PDF. */
    QString absolute_path;
};

/**
 * @brief Returns the examples installed below @p application_directory.
 *
 * Looks for the six bundled example decks in `<application_directory>/examples` and returns,
 * in menu order, those that exist as readable files; missing decks are skipped silently.
 */
QVector<ExamplePresentation> installed_example_presentations(const QString& application_directory);
