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

PythonConsoleWidget::~PythonConsoleWidget()
{
    g_consoleWidget = nullptr;
}

void PythonConsoleWidget::appendPythonOutput(const QString& text)
{
    QTextCursor cursor = this->textCursor();
    cursor.movePosition(QTextCursor::End);

    QString buffer = text;

    while (!buffer.isEmpty()) {

        int rPos = buffer.indexOf('\r');
        int nPos = buffer.indexOf('\n');

        // Determine which control char comes first
        int pos = -1;
        QChar ctrl;

        if (rPos == -1 && nPos == -1) {
            ctrl = QChar();
        }
        else if (rPos == -1 || (nPos != -1 && nPos < rPos)) {
            pos = nPos;
            ctrl = '\n';
        }
        else {
            pos = rPos;
            ctrl = '\r';
        }

        // No control characters left
        if (pos == -1) {
            cursor.insertText(buffer);
            break;
        }

        // Extract chunk before control char
        QString chunk = buffer.left(pos);

        if (!chunk.isEmpty()) {
            cursor.insertText(chunk);
        }

        // Handle control character
        if (ctrl == '\n') {
            cursor.insertBlock();
            buffer.remove(0, pos + 1);
            continue;
        }

        if (ctrl == '\r') {
            // Handle Windows \r\n as newline
            if (pos + 1 < buffer.size() && buffer[pos + 1] == '\n') {
                cursor.insertBlock();
                buffer.remove(0, pos + 2);
                continue;
            }

            // Proper carriage return behavior (overwrite line)
            cursor.movePosition(QTextCursor::StartOfLine);
            cursor.select(QTextCursor::LineUnderCursor);
            cursor.removeSelectedText();

            buffer.remove(0, pos + 1);
            continue;
        }
    }

    this->setTextCursor(cursor);
}

void PythonConsoleWidget::resizeEvent(QResizeEvent* e)
{
    QPlainTextEdit::resizeEvent(e);
    if (PythonQtStream::sink)
        PythonQtStream::sink("\r");
}
