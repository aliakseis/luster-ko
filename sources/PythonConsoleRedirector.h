#pragma once

#include "widgets/PythonConsoleWidget.h"

#include <QObject>
#include <QSocketNotifier>
#include <QString>
#include <functional>
#include <algorithm>

#if defined(_WIN32)
#include <io.h>     // _pipe, _dup2, _read
#include <fcntl.h>  // O_BINARY
#define pipe(fds) _pipe(fds, 4096, O_BINARY)
#define dup2 _dup2
#define read _read
#else
#include <unistd.h> // pipe, dup2, read
#endif

//class PythonConsoleWidget;
//extern PythonConsoleWidget* g_consoleWidget;



// 1) Python-level stream
struct PythonQtStream {
    static std::function<void(const std::string&)> sink;
    void write(const std::string& s) { if (sink) sink(s); }
    void flush() {}
};
inline std::function<void(const std::string&)> PythonQtStream::sink;

// 2) FD-level redirector
class PythonFdRedirector : public QObject {
    Q_OBJECT
public:
    explicit PythonFdRedirector(QObject* parent = nullptr)
        : QObject(parent)
    {
        pipe(m_stdoutPipe);
        pipe(m_stderrPipe);

        dup2(m_stdoutPipe[1], 1); // stdout
        dup2(m_stderrPipe[1], 2); // stderr

        m_stdoutNotifier = new QSocketNotifier(m_stdoutPipe[0], QSocketNotifier::Read, this);
        m_stderrNotifier = new QSocketNotifier(m_stderrPipe[0], QSocketNotifier::Read, this);

        connect(m_stdoutNotifier, &QSocketNotifier::activated,
            this, &PythonFdRedirector::onStdout);
        connect(m_stderrNotifier, &QSocketNotifier::activated,
            this, &PythonFdRedirector::onStderr);
    }

private slots:
    void onStdout() { readFromFd(m_stdoutPipe[0]); }
    void onStderr() { readFromFd(m_stderrPipe[0]); }

private:
    void readFromFd(int fd) {
        char buf[4096];
#if defined(_WIN32)
        int n = read(fd, buf, sizeof(buf));
#else
        ssize_t n = read(fd, buf, sizeof(buf));
#endif
        if (n > 0 && PythonQtStream::sink)
            PythonQtStream::sink(std::string(buf, n));
    }

    int m_stdoutPipe[2]{ -1, -1 };
    int m_stderrPipe[2]{ -1, -1 };
    QSocketNotifier* m_stdoutNotifier{ nullptr };
    QSocketNotifier* m_stderrNotifier{ nullptr };
};

// 3) tqdm width helper
inline int get_console_width_chars()
{
    if (!g_consoleWidget)
        return 80;

    int px = g_consoleWidget->viewport()->width();
    int charWidth = g_consoleWidget->fontMetrics().horizontalAdvance('M');
    if (charWidth <= 0) charWidth = 8;

    return std::max(20, px / charWidth);
}
