#include "ConsoleUiModel.hpp"

ConsoleUiModel::ConsoleUiModel(QObject* parent) : QObject(parent) {
    terminalOutput = QStringLiteral("ParaMetal Console\nInitializing Python...\n\n");
}

void ConsoleUiModel::setPythonVersion(const QString& version) {
    terminalOutput = QStringLiteral("ParaMetal Console\nPython %1\nparametal imported as pm\n\nHelp: api(), registry()\n\n").arg(version);
    emit outputChanged();
}

void ConsoleUiModel::execute(const QString& source) {
    if (requestPending) return;
    if (waitingForMoreInput) {
        pythonBuffer += QStringLiteral("\n") + source;
        terminalOutput += QStringLiteral("... ") + source + QStringLiteral("\n");
    } else {
        pythonBuffer = source;
        terminalOutput += QStringLiteral(">>> ") + source + QStringLiteral("\n");
    }
    requestPending = true;
    emit outputChanged();
    emit executeRequested(pythonBuffer);
}

void ConsoleUiModel::clear() {
    terminalOutput.clear();
    emit outputChanged();
}

void ConsoleUiModel::applyResult(const PythonResult& result) {
    requestPending = false;
    waitingForMoreInput = result.incomplete;
    terminalOutput += result.output;
    terminalOutput += result.error;
    if (!waitingForMoreInput) pythonBuffer.clear();
    emit outputChanged();
}
