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

#include "palettebar.h"
#include "../datasingleton.h"

PaletteBar::PaletteBar() :
    QToolBar(tr("Colors"))
{
    setMovable(false);
    initializeItems();
}

void PaletteBar::initializeItems()
{
    const QColor colorList[] = {
        Qt::black, Qt::white, Qt::red, Qt::darkRed, Qt::green, Qt::darkGreen,
        Qt::blue, Qt::darkBlue, Qt::cyan, Qt::darkCyan, Qt::magenta,
        Qt::darkMagenta, Qt::yellow, Qt::darkYellow, Qt::gray
    };
    for (const auto& color : colorList)
    {
        mColorButton = new PaletteButton(color);
        connect(mColorButton, SIGNAL(colorPicked()), this, SIGNAL(colorClicked()));
        addWidget(mColorButton);
    }
}

void PaletteBar::contextMenuEvent(QContextMenuEvent *) { }
