#include "PythonConsoleWidget.h"
#include <QFontDatabase>
#include <QTextCursor>
#include "../PythonConsoleRedirector.h"

PythonConsoleWidget* g_consoleWidget = nullptr;

PythonConsoleWidget::PythonConsoleWidget(QWidget* parent)
    : QPlainTextEdit(parent)
{
    setReadOnly(true);
    setLineWrapMode(QPlainTextEdit::NoWrap);

    QFont f = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    f.setStyleHint(QFont::Monospace);
    f.setFixedPitch(true);
    setFont(f);

    g_consoleWidget = this;
}

void PythonConsoleWidget::appendPythonOutput(const QString& text)
{
    QTextCursor cursor = this->textCursor();
    cursor.movePosition(QTextCursor::End);

    const auto parts = text.split('\r');
    for (int i = 0; i < parts.size(); ++i) {
        if (i > 0) {
            cursor.movePosition(QTextCursor::StartOfLine, QTextCursor::KeepAnchor);
            cursor.removeSelectedText();
            cursor.deletePreviousChar();
        }
        cursor.insertText(parts[i]);
    }

    this->setTextCursor(cursor);
}

void PythonConsoleWidget::resizeEvent(QResizeEvent* e)
{
    QPlainTextEdit::resizeEvent(e);
    if (PythonQtStream::sink)
        PythonQtStream::sink("\r");
}
