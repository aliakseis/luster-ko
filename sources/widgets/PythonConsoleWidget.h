#pragma once
#include <QPlainTextEdit>

class PythonConsoleWidget : public QPlainTextEdit {
    Q_OBJECT
public:
    explicit PythonConsoleWidget(QWidget* parent = nullptr);
    ~PythonConsoleWidget();

public slots:
    void appendPythonOutput(const QString& text);

protected:
    void resizeEvent(QResizeEvent* e) override;
};

extern PythonConsoleWidget* g_consoleWidget;
