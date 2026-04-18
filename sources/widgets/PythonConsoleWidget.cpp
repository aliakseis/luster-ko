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

        //cursor.movePosition(QTextCursor::End);

        if (m_insertBlock) {
            cursor.insertBlock();
            m_insertBlock = false;
        }

        // Case 1: newline first
        if (nPos != -1 && (rPos == -1 || nPos < rPos)) {
            QString chunk = buffer.left(nPos);
            cursor.insertText(chunk);
            m_insertBlock = true;
            buffer.remove(0, nPos + 1);
            continue;
        }

        // Case 2: carriage return first
        if (rPos != -1) {
            // Check for \r\n (Windows newline)
            if (rPos + 1 < buffer.size() && buffer[rPos + 1] == '\n') {
                QString chunk = buffer.left(rPos);
                cursor.insertText(chunk);
                m_insertBlock = true;
                buffer.remove(0, rPos + 2);
                continue;
            }

            // Bare \r -> overwrite current line (tqdm)
            QString chunk = buffer.left(rPos);

            cursor.movePosition(QTextCursor::StartOfLine, QTextCursor::KeepAnchor);
            cursor.removeSelectedText();
            cursor.deletePreviousChar();

            cursor.insertText(chunk);
            buffer.remove(0, rPos + 1);
            continue;
        }

        // Case 3: no control characters left
        cursor.insertText(buffer);
        break;
    }

    this->setTextCursor(cursor);
}

void PythonConsoleWidget::resizeEvent(QResizeEvent* e)
{
    QPlainTextEdit::resizeEvent(e);
    if (PythonQtStream::sink)
        PythonQtStream::sink("\r");
}
