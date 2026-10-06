#pragma once

/*
 * This source file is part of luster-ko.
 *
 * Copyright (c) 2026 luster-ko <https://github.com/aliakseis/luster-ko>
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <QColor>
#include <QtCore/QSize>
#include <QtCore/QString>
#include <QtCore/QMap>
#include <QKeySequence>
#include <QFont>
#include <QObject>

#include "app_enums.h"

class AbstractEffect;
class AbstractInstrument;
struct FunctionInfo;
class ScriptModel;

// -----------------------------------------------------------------------------
// Persistent settings
//
// Keep each persistent setting in one place: type, public name, member,
// QSettings key and default value. The list is deliberately kept in this
// header so declarations and serialization cannot silently get out of sync.
// -----------------------------------------------------------------------------
#define DATA_SINGLETON_DEFAULT_BASE_SIZE QSize(400, 300)

// -----------------------------------------------------------------------------
// Persistent settings.
//
// type, public name, member, QSettings key, default value and assignment rule
// are all kept in one place. The same list generates the members, accessors
// and serialization code.
// -----------------------------------------------------------------------------
#define DATA_SINGLETON_SETTINGS(X) \
    X(QSize,   BaseSize,            DATA_SINGLETON_DEFAULT_BASE_SIZE) \
    X(bool,    IsAutoSave,          false) \
    X(int,     AutoSaveInterval,    300) \
    X(int,     HistoryDepth,        40) \
    X(QString, AppLanguage,         QStringLiteral("system")) \
    X(bool,    IsRestoreWindowSize, true) \
    X(bool,    IsAskCanvasSize,     true) \
    X(bool,    IsDarkMode,          false) \
    X(bool,    IsLoadScript,        false) \
    X(QString, ScriptPath,          QString()) \
    X(QString, VirtualEnvPath,      QString())

// -----------------------------------------------------------------------------
// Shortcut definitions. The map member, logical key, QSettings key and
// default shortcut are kept together, so read/write code is generated once.
// -----------------------------------------------------------------------------
#define DATA_SINGLETON_SHORTCUTS(X) \
    X(mFileShortcuts,        "New",    "/Shortcuts/File/New",              QKeySequence(QKeySequence::New)) \
    X(mFileShortcuts,        "Open",   "/Shortcuts/File/Open",             QKeySequence(QKeySequence::Open)) \
    X(mFileShortcuts,        "Save",   "/Shortcuts/File/Save",             QKeySequence(QKeySequence::Save)) \
    X(mFileShortcuts,        "SaveAs", "/Shortcuts/File/SaveAs",           QKeySequence(QKeySequence::SaveAs)) \
    X(mFileShortcuts,        "Close",  "/Shortcuts/File/Close",            QKeySequence(QKeySequence::Close)) \
    X(mFileShortcuts,        "Print",  "/Shortcuts/File/Print",            QKeySequence(QKeySequence::Print)) \
    X(mFileShortcuts,        "Exit",   "/Shortcuts/File/Exit",             QKeySequence(QKeySequence::Quit)) \
    X(mEditShortcuts,        "Undo",   "/Shortcuts/Edit/Undo",              QKeySequence(QKeySequence::Undo)) \
    X(mEditShortcuts,        "Redo",   "/Shortcuts/Edit/Redo",              QKeySequence(QKeySequence::Redo)) \
    X(mEditShortcuts,        "Copy",   "/Shortcuts/Edit/Copy",              QKeySequence(QKeySequence::Copy)) \
    X(mEditShortcuts,        "Paste",  "/Shortcuts/Edit/Paste",             QKeySequence(QKeySequence::Paste)) \
    X(mEditShortcuts,        "Cut",    "/Shortcuts/Edit/Cut",               QKeySequence(QKeySequence::Cut)) \
    X(mInstrumentsShortcuts, "Cursor",  "/Shortcuts/Instruments/Cursor",   "Ctrl+1") \
    X(mInstrumentsShortcuts, "Lastic",  "/Shortcuts/Instruments/Lastic",   "Ctrl+2") \
    X(mInstrumentsShortcuts, "Pipette", "/Shortcuts/Instruments/Pipette",  "Ctrl+3") \
    X(mInstrumentsShortcuts, "Loupe",   "/Shortcuts/Instruments/Loupe",    "Ctrl+4") \
    X(mInstrumentsShortcuts, "Pen",     "/Shortcuts/Instruments/Pen",      "Ctrl+5") \
    X(mInstrumentsShortcuts, "Line",    "/Shortcuts/Instruments/Line",     "Ctrl+6") \
    X(mInstrumentsShortcuts, "Spray",   "/Shortcuts/Instruments/Spray",    "Ctrl+7") \
    X(mInstrumentsShortcuts, "Fill",    "/Shortcuts/Instruments/Fill",     "Ctrl+8") \
    X(mInstrumentsShortcuts, "Rect",    "/Shortcuts/Instruments/Rect",     "Ctrl+9") \
    X(mInstrumentsShortcuts, "Ellipse", "/Shortcuts/Instruments/Ellipse",  "Ctrl+0") \
    X(mInstrumentsShortcuts, "Curve",   "/Shortcuts/Instruments/Curve",    "") \
    X(mInstrumentsShortcuts, "Text",    "/Shortcuts/Instruments/Text",     "") \
    X(mToolsShortcuts,       "ZoomIn",  "/Shortcuts/Tools/Zoom/ZoomIn",     QKeySequence(QKeySequence::ZoomIn)) \
    X(mToolsShortcuts,       "ZoomOut", "/Shortcuts/Tools/Zoom/ZoomOut",    QKeySequence(QKeySequence::ZoomOut))

/**
 * @brief Singleton for variables needed for the program.
 */
class DataSingleton : public QObject
{
public:
    static DataSingleton* Instance();

    // Simple runtime properties.
#define DATA_SINGLETON_RUNTIME_PROPERTY(type, name, member) \
private: \
    type member; \
public: \
    type get##name() const { return member; } \
    void set##name(const type& value) { member = value; }

    DATA_SINGLETON_RUNTIME_PROPERTY(QColor, PrimaryColor, mPrimaryColor)
    DATA_SINGLETON_RUNTIME_PROPERTY(QColor, SecondaryColor, mSecondaryColor)
    DATA_SINGLETON_RUNTIME_PROPERTY(int, PenSize, mPenSize)
    DATA_SINGLETON_RUNTIME_PROPERTY(InstrumentsEnum, PreviousInstrument, mPreviousInstrument)
    DATA_SINGLETON_RUNTIME_PROPERTY(QSize, WindowSize, mWindowSize)
    DATA_SINGLETON_RUNTIME_PROPERTY(QString, LastFilePath, mLastFilePath)
    DATA_SINGLETON_RUNTIME_PROPERTY(QFont, TextFont, mTextFont)

#undef DATA_SINGLETON_RUNTIME_PROPERTY

    // Persistent settings. Accessors are generated from the same list used
    // by readSetting() and writeSettings().
#define DATA_SINGLETON_SETTING_ACCESSOR(type, name, defaultValue) \
    type get##name() const { return m##name; } \
    void set##name(const type& value) { m##name = value; }

    DATA_SINGLETON_SETTINGS(DATA_SINGLETON_SETTING_ACCESSOR)

#undef DATA_SINGLETON_SETTING_ACCESSOR

    InstrumentsEnum getInstrument() const { return mCurrentInstrument; }
    void setInstrument(const InstrumentsEnum& instrument)
    {
        mCurrentInstrument = instrument;
        mIsResetCurve = true;
    }

    QMap<QString, QKeySequence> getFileShortcuts() const { return mFileShortcuts; }
    QKeySequence getFileShortcutByKey(const QString& key) const { return mFileShortcuts.value(key); }
    void setFileShortcutByKey(const QString& key, const QKeySequence& value) { mFileShortcuts[key] = value; }

    QMap<QString, QKeySequence> getEditShortcuts() const { return mEditShortcuts; }
    QKeySequence getEditShortcutByKey(const QString& key) const { return mEditShortcuts.value(key); }
    void setEditShortcutByKey(const QString& key, const QKeySequence& value) { mEditShortcuts[key] = value; }

    QMap<QString, QKeySequence> getInstrumentsShortcuts() const { return mInstrumentsShortcuts; }
    QKeySequence getInstrumentShortcutByKey(const QString& key) const { return mInstrumentsShortcuts.value(key); }
    void setInstrumentShortcutByKey(const QString& key, const QKeySequence& value) { mInstrumentsShortcuts[key] = value; }

    QMap<QString, QKeySequence> getToolsShortcuts() const { return mToolsShortcuts; }
    QKeySequence getToolShortcutByKey(const QString& key) const { return mToolsShortcuts.value(key); }
    void setToolShortcutByKey(const QString& key, const QKeySequence& value) { mToolsShortcuts[key] = value; }

    // Needs for correct work of Bezier curve instrument.
    void setResetCurve(bool value) { mIsResetCurve = value; }
    bool isResetCurve() const { return mIsResetCurve; }

    void setMarkupMode(bool value) { mMarkupMode = value; }
    bool isMarkupMode() const { return mMarkupMode; }

    void setMarkupTransparency(int value) { mMarkupTransparency = qBound(0, value, 100); }
    int getMarkupTransparency() const { return mMarkupTransparency; }

    void readSetting();
    void writeSettings();
    void readState();
    void writeState();

    QVector<AbstractEffect*> mEffectsHandlers;
    int addScriptActionHandler(ScriptModel* scriptModel, const FunctionInfo& functionInfo);

private:
    DataSingleton();
    DataSingleton(DataSingleton const&) = delete;
    DataSingleton& operator=(DataSingleton const&) = delete;

#define DATA_SINGLETON_SETTING_MEMBER(type, name, defaultValue) \
    type m##name{};

    DATA_SINGLETON_SETTINGS(DATA_SINGLETON_SETTING_MEMBER)

#undef DATA_SINGLETON_SETTING_MEMBER

    InstrumentsEnum mCurrentInstrument = NONE_INSTRUMENT;

    bool mIsResetCurve = false; /**< Needs to correct work of Bezier curve instrument. */
    bool mMarkupMode = false;
    int mMarkupTransparency = 0;

    QMap<QString, QKeySequence> mFileShortcuts;
    QMap<QString, QKeySequence> mEditShortcuts;
    QMap<QString, QKeySequence> mInstrumentsShortcuts;
    QMap<QString, QKeySequence> mToolsShortcuts;
};
