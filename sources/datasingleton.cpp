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

#include "datasingleton.h"

#include "effects/negativeeffect.h"
#include "effects/grayeffect.h"
#include "effects/binarizationeffect.h"
#include "effects/gaussianblureffect.h"
#include "effects/gammaeffect.h"
#include "effects/sharpeneffect.h"
#include "effects/customeffect.h"
#include "effects/scripteffect.h"
#include "effects/scripteffectwithsettings.h"

#include "ScriptInfo.h"

#include <QtCore/QSettings>

DataSingleton::DataSingleton()
{
    mPrimaryColor = Qt::black;
    mSecondaryColor = Qt::white;
    mPenSize = 1;
    mTextFont = QFont("Times", 12);
    mCurrentInstrument = NONE_INSTRUMENT;
    mPreviousInstrument = NONE_INSTRUMENT;
    readSetting();
    setMarkupTransparency(mMarkupTransparency);
    readState();

    // Effects handlers
    mEffectsHandlers.fill(0, (int)EFFECTS_COUNT);
    mEffectsHandlers[NEGATIVE] = new NegativeEffect(this);
    mEffectsHandlers[GRAY] = new GrayEffect(this);
    mEffectsHandlers[BINARIZATION] = new BinarizationEffect(this);
    mEffectsHandlers[GAUSSIANBLUR] = new GaussianBlurEffect(this);
    mEffectsHandlers[GAMMA] = new GammaEffect(this);
    mEffectsHandlers[SHARPEN] = new SharpenEffect(this);
    mEffectsHandlers[CUSTOM] = new CustomEffect(this);
}

DataSingleton* DataSingleton::Instance()
{
    static DataSingleton instance;
    return &instance;
}

void DataSingleton::readSetting()
{
    QSettings settings;

#define DATA_SINGLETON_READ_SETTING(type, name, defaultValue) \
    m##name = settings.value("/Settings/" #name, defaultValue).value<type>();

    DATA_SINGLETON_SETTINGS(DATA_SINGLETON_READ_SETTING)

#undef DATA_SINGLETON_READ_SETTING

#define DATA_SINGLETON_READ_SHORTCUT(member, name, key, defaultValue) \
    member.insert(QStringLiteral(name), settings.value(QStringLiteral(key), defaultValue).value<QKeySequence>());

    DATA_SINGLETON_SHORTCUTS(DATA_SINGLETON_READ_SHORTCUT)

#undef DATA_SINGLETON_READ_SHORTCUT
}

void DataSingleton::writeSettings()
{
    QSettings settings;

#define DATA_SINGLETON_WRITE_SETTING(type, name, defaultValue) \
    settings.setValue("/Settings/" #name, m##name);

    DATA_SINGLETON_SETTINGS(DATA_SINGLETON_WRITE_SETTING)

#undef DATA_SINGLETON_WRITE_SETTING

#define DATA_SINGLETON_WRITE_SHORTCUT(member, name, key, defaultValue) \
    settings.setValue(QStringLiteral(key), member.value(QStringLiteral(name)));

    DATA_SINGLETON_SHORTCUTS(DATA_SINGLETON_WRITE_SHORTCUT)

#undef DATA_SINGLETON_WRITE_SHORTCUT
}

void DataSingleton::readState()
{
    QSettings settings;
    mWindowSize = settings.value("/State/WindowSize", QSize()).toSize();
}

void DataSingleton::writeState()
{
    QSettings settings;
    if (mWindowSize.isValid()) {
        settings.setValue("/State/WindowSize", mWindowSize);
    }
}

int DataSingleton::addScriptActionHandler(ScriptModel* scriptModel, const FunctionInfo& functionInfo)
{
    int result = mEffectsHandlers.size();

    mEffectsHandlers.push_back(functionInfo.parameters.size() > 1
        ? static_cast<AbstractEffect*>(new ScriptEffectWithSettings(scriptModel, functionInfo))
        : new ScriptEffect(scriptModel, functionInfo));

    return result;
}
