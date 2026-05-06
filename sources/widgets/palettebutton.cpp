#include "palettebutton.h"

#include <QMouseEvent>
#include <QPainter>

PaletteButton::PaletteButton(const QColor& color)
{
    mColor = color;
    setMinimumSize(QSize(30, 30));
    setMaximumSize(QSize(30, 30));

    QPixmap pixmap(20, 20);

    if (color == Qt::transparent)
    {
        pixmap.fill(Qt::white);
        QPainter p(&pixmap);

        const int s = 4;
        for (int y = 0; y < pixmap.height(); y += s)
            for (int x = 0; x < pixmap.width(); x += s)
                if (((x / s) + (y / s)) % 2)
                    p.fillRect(x, y, s, s, QColor(200, 200, 200));

        setToolTip(tr("Transparent"));
    }
    else
    {
        pixmap.fill(color);
        setToolTip(color.name());
    }

    setIcon(pixmap);
}

void PaletteButton::mousePressEvent(QMouseEvent *event)
{
    if(event->button() == Qt::LeftButton)
        DataSingleton::Instance()->setPrimaryColor(mColor);
    else if(event->button() == Qt::RightButton)
        DataSingleton::Instance()->setSecondaryColor(mColor);

    emit colorPicked();
}
