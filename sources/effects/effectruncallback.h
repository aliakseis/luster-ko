#pragma once

#include <QtCore/QObject>

class EffectRunCallback : public QObject
{
    Q_OBJECT

public:
    bool isInterrupted() noexcept { return mIsInterrupted.load(); }
    void interrupt() noexcept { mIsInterrupted.store(true); }

signals:
    void sendImage(const QImage& img);

private:
    std::atomic_bool mIsInterrupted = false;
};
