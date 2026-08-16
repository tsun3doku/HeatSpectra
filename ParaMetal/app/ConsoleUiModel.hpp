#pragma once

#include "UiRuntimeTypes.hpp"

#include <QObject>
#include <QString>

class ConsoleUiModel final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString output READ output NOTIFY outputChanged)

public:
    explicit ConsoleUiModel(QObject* parent = nullptr);

    QString output() const { return terminalOutput; }

    Q_INVOKABLE void execute(const QString& source);
    Q_INVOKABLE void clear();
    void setPythonVersion(const QString& version);

public slots:
    void applyResult(const PythonResult& result);

signals:
    void outputChanged();
    void executeRequested(const QString& source);

private:
    QString terminalOutput;
    QString pythonBuffer;
    bool waitingForMoreInput = false;
    bool requestPending = false;
};
