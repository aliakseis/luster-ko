#pragma once
#include <QPlainTextEdit>

class PythonConsoleWidget : public QPlainTextEdit {
    Q_OBJECT
public:
    explicit PythonConsoleWidget(QWidget* parent = nullptr);

public slots:
    void appendPythonOutput(const QString& text);

protected:
    void resizeEvent(QResizeEvent* e) override;
};
