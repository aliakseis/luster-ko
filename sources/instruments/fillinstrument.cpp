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

#include "fillinstrument.h"
#include "../imagearea.h"
#include "../datasingleton.h"

#include <QPen>
#include <QPainter>

#include <QImage>
#include <QRgb>
#include <vector>
#include <algorithm>
#include <cmath>

namespace {

inline QRgb localMedianRGB(const QImage &img, int x, int y)
{
    int r[9], g[9], b[9];
    int count = 0;

    for(int dy = -1; dy <= 1; dy++)
        for(int dx = -1; dx <= 1; dx++)
        {
            int xx = std::clamp(x + dx, 0, img.width()  - 1);
            int yy = std::clamp(y + dy, 0, img.height() - 1);

            QRgb p = img.pixel(xx, yy);

            if (qAlpha(p) == 0)
                return img.pixel(x, y);

            r[count] = qRed(p);
            g[count] = qGreen(p);
            b[count] = qBlue(p);
            count++;
        }

    std::nth_element(r, r + 4, r + 9);
    std::nth_element(g, g + 4, g + 9);
    std::nth_element(b, b + 4, b + 9);

    return qRgb(r[4], g[4], b[4]);
}

const int SQ_TOLERANCE = 200;

bool isSimilar(QRgb a, QRgb b)
{
    int dr = qRed(a) - qRed(b);
    int dg = qGreen(a) - qGreen(b);
    int db = qBlue(a) - qBlue(b);
    return (dr * dr + dg * dg + db * db) <= SQ_TOLERANCE;
}


void fill(int x, int y, QRgb switchColor, QRgb oldColor, QImage& tempImage)
{
    const int width = tempImage.width();
    const int height = tempImage.height();

    if (x < 0 || x >= width || y < 0 || y >= height)
        return;
    if (!isSimilar(tempImage.pixel(x, y), oldColor))
        return;

    std::vector<std::pair<int,int>> stack;
    stack.reserve(256);
    stack.emplace_back(x, y);

    while (!stack.empty()) {
        const auto [cx, cy] = stack.back();
        stack.pop_back();

        // scan left from cx
        int left = cx - 1;
        while (left >= 0 && isSimilar(tempImage.pixel(left, cy), oldColor)) {
            tempImage.setPixel(left, cy, switchColor);
            --left;
        }

        // scan right from cx+1
        int right = cx + 1;
        while (right < width && isSimilar(tempImage.pixel(right, cy), oldColor)) {
            tempImage.setPixel(right, cy, switchColor);
            ++right;
        }

        // for each pixel in the filled span, check above and below
        for (int xx = left + 1; xx < right; ++xx) {
            if (cy > 0) {
                QRgb currentColor = tempImage.pixel(xx, cy - 1);
                if (isSimilar(currentColor, oldColor) && currentColor != switchColor) {
                    tempImage.setPixel(xx, cy - 1, switchColor);
                    stack.emplace_back(xx, cy - 1);
                }
            }
            if (cy < height - 1) {
                QRgb currentColor = tempImage.pixel(xx, cy + 1);
                if (isSimilar(currentColor, oldColor) && currentColor != switchColor) {
                    tempImage.setPixel(xx, cy + 1, switchColor);
                    stack.emplace_back(xx, cy + 1);
                }
            }
        }
    }
}

} // namespace


FillInstrument::FillInstrument(QObject *parent) :
    AbstractInstrument(parent)
{
}

void FillInstrument::mousePressEvent(QMouseEvent *event, ImageArea &imageArea)
{
    if(event->button() == Qt::LeftButton || event->button() == Qt::RightButton)
    {
        mStartPoint = mEndPoint = event->pos() / imageArea.getZoomFactor();
        imageArea.setIsPaint(true);
        makeUndoCommand(imageArea);
    }
}

void FillInstrument::mouseMoveEvent(QMouseEvent *, ImageArea &)
{

}

void FillInstrument::mouseReleaseEvent(QMouseEvent *event, ImageArea &imageArea)
{
    if(imageArea.isPaint())
    {
        if(event->button() == Qt::LeftButton)
        {
            paint(imageArea, false);
        }
        else if(event->button() == Qt::RightButton)
        {
            paint(imageArea, true);
        }
        imageArea.setIsPaint(false);
    }
}

void FillInstrument::paint(ImageArea& imageArea, bool isSecondaryColor, bool)
{
    QColor switchColor;
    if (!isSecondaryColor)
        switchColor = DataSingleton::Instance()->getPrimaryColor();
    else
        switchColor = DataSingleton::Instance()->getSecondaryColor();

    //QRgb pixel(imageArea.getImage()->pixel(mStartPoint));
    //QColor oldColor(pixel);
    auto oldColor = localMedianRGB(*imageArea.getImage(), mStartPoint.x(), mStartPoint.y());

    if (switchColor != oldColor)
    {
        fill(mStartPoint.x(), mStartPoint.y(),
            switchColor.rgba(), oldColor,
            *imageArea.getImage());
    }

    imageArea.setEdited(true);
    imageArea.update();
}
