#pragma once

#include "ScriptInfo.h"
#include "effects/effectruncallback.h"

#include <QObject>
#include <QVariant>

#include <vector>
#include <memory>
#include <mutex>

class QMenu;
class QAction;
class QThread;
class ScriptModelImpl;

const char CHECK_PYTHON_OPTION[] = "--checkPython";

class ScriptModel : public QObject
{
    Q_OBJECT

public:
    ScriptModel(QWidget* parent, const QString& venvPath);
    ~ScriptModel();

    void LoadScript(const QString& path);

    void setupActions(QMenu* fileMenu, QMenu* effectsMenu, QMap<int, QAction*>& effectsActMap);

    QVariant call(const QString& callable, const QVariantList& args = QVariantList(), std::weak_ptr<EffectRunCallback> callback = {}, const QVariantMap& kwargs = QVariantMap());

    static int ValidatePythonSystem();

private:
    // PImpl running in its own thread
    std::unique_ptr<ScriptModelImpl> mImpl;
    QThread* mImplThread = nullptr;
};
