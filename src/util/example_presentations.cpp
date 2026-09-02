#include "example_presentations.hpp"

#include <QDir>
#include <QFileInfo>

#include <array>

namespace {
struct ExampleDefinition {
    const char* title;
    const char* file_name;
};

constexpr std::array<ExampleDefinition, 3> kExamples{{
    {"Getting Started with uil", "getting-started.pdf"},
    {"Pointer and Annotation Tools", "pointer-and-annotations.pdf"},
    {"Interactive Molecule Visualizer", "molecule-visualizer.uil"},
}};
}  // namespace

QVector<ExamplePresentation> installed_example_presentations(
    const QString& application_directory) {
    const QDir examples_directory(
        QDir(application_directory).filePath(QStringLiteral("examples")));
    QVector<ExamplePresentation> examples;
    examples.reserve(kExamples.size());

    for (const ExampleDefinition& definition : kExamples) {
        const QFileInfo file_info(
            examples_directory.filePath(QString::fromLatin1(definition.file_name)));
        if (!file_info.isFile() || !file_info.isReadable()) {
            continue;
        }

        examples.push_back({
            QString::fromLatin1(definition.title),
            QString::fromLatin1(definition.file_name),
            file_info.absoluteFilePath(),
        });
    }

    return examples;
}
