/*
 * Copyright (c) 2014 Damien Grauser (Damien.Grauser@pev-geneve.ch)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */



#include "AllPlotInterval.h"

#include "Colors.h"
#include "Units.h"
#include "Athlete.h"
#include "AllPlot.h"
#include "IntervalItem.h"

#include <qwt_plot.h>
#include <qwt_plot_canvas.h>
#include <qwt_plot_intervalcurve.h>
#include <qwt_scale_div.h>
#include <qwt_scale_widget.h>

class AllPlotIntervalData : public QwtArraySeriesData<QwtIntervalSample>
{
    public:
    AllPlotIntervalData(AllPlotInterval *plot, Context *context, int level, int max, RideItem *rideItem, const RideFileInterval interval) :
        plot(plot), context(context), level(level), max(max), rideItem(rideItem), interval(interval) {}

    double x(size_t i) const ;
    double ymin(size_t) const ;
    double ymax(size_t i) const ;

    size_t size() const ;

    void init() ;
    IntervalItem *intervalNum(int n) const;
    int intervalCount() const;

    RideItem *rideItem;
    AllPlotInterval *plot;
    Context *context;
    int level;
    int max;
    const RideFileInterval interval;

    virtual QwtIntervalSample sample(size_t i) const;
    virtual QRectF boundingRect() const;
};

class TimeScaleDraw: public ScaleScaleDraw
{

    public:

    TimeScaleDraw(bool *bydist) : ScaleScaleDraw(), bydist(bydist) {}

    virtual QwtText label(double v) const
    {
        if (*bydist) {
            return QString("%1").arg(v);
        } else {
            QTime t = QTime(0,0,0,0).addSecs(v*60.00);
            if (scaleMap().sDist() > 5)
                return t.toString("hh:mm");
            return t.toString("hh:mm:ss");
        }
    }
    private:
    bool *bydist;

};

AllPlotInterval::AllPlotInterval(QWidget *parent, Context *context):
    QwtPlot(parent),
    bydist(false),
    context(context),
    groupMatch(false)
{
    setCanvasBackground(GColor(CRIDEPLOTBACKGROUND));
    static_cast<QwtPlotCanvas*>(canvas())->setFrameStyle(QFrame::NoFrame);

    QPalette pal = palette();
    pal.setBrush(QPalette::Background, QBrush(GColor(CRIDEPLOTBACKGROUND)));
    setPalette(pal);

    // tick draw
    //TimeScaleDraw *tsd = new TimeScaleDraw(&this->bydist) ;
    //tsd->setTickLength(QwtScaleDiv::MajorTick, 3);
    //setAxisScaleDraw(QwtPlot::xBottom, tsd);
    //pal.setColor(QPalette::WindowText, GColor(CPLOTMARKER));
    //pal.setColor(QPalette::Text, GColor(CPLOTMARKER));
    //axisWidget(QwtPlot::xBottom)->setPalette(pal);

    enableAxis(xBottom, false);
    setAxisVisible(xBottom, false);

    setAxisScale(QwtPlot::xBottom, 0, context->ride->ride(true)->maximumFor(RideFile::secs));

    setAxisVisible(yLeft, false);

    tooltip = new LTMToolTip(QwtPlot::xBottom, QwtAxis::yLeft,
                                   QwtPicker::NoRubberBand,
                                   QwtPicker::AlwaysOn,
                                   canvas(),
                                   "");

    canvasPicker = new AllPlotIntervalCanvasPicker(this);

    connect(context, SIGNAL(intervalHover(RideFileInterval)), this, SLOT(intervalHover(RideFileInterval)));
    connect(canvasPicker, SIGNAL(pointHover(QwtPlotIntervalCurve*, int)), this, SLOT(intervalCurveHover(QwtPlotIntervalCurve*)));
    connect(canvasPicker, SIGNAL(pointClicked(QwtPlotIntervalCurve*,int)), this, SLOT(intervalCurveClick(QwtPlotIntervalCurve*)));
    connect(canvasPicker, SIGNAL(pointDblClicked(QwtPlotIntervalCurve*,int)), this, SLOT(intervalCurveDblClick(QwtPlotIntervalCurve*)));


}

void
AllPlotInterval::setByDistance(int id)
{
    bydist = (id == 1);
    if (rideItem == NULL) return;
    if (intervalLigns.count() == 0) return;

    refreshIntervals();
}

void
AllPlotInterval::setDataFromRide(RideItem *_rideItem)
{
    rideItem = _rideItem;
    if (rideItem == NULL) return;

    refreshIntervals();
}

void
AllPlotInterval::refreshIntervals()
{
    placeIntervals();
    refreshIntervalCurve();
    //refreshIntervalMarkers();
}

// Compare two RideFileInterval on duration.
bool intervalBiggerThan(const RideFileInterval &i1, const RideFileInterval &i2)
{
    return (i1.stop-i1.start) > (i2.stop-i2.start);
}

void
AllPlotInterval::sortIntervals(QList<RideFileInterval> &intervals, QList< QList<RideFileInterval> > &intervalsGroups)
{
    // Sort by duration
    qSort(intervals.begin(), intervals.end(), intervalBiggerThan);

    QList<RideFileInterval> matchesGroup;

    for (int i=0; i<intervals.count(); i++) {
        RideFileInterval interval = intervals.at(i);

        if (groupMatch && interval.isMatch()) {
            matchesGroup.append(interval);
            intervals.removeOne(interval);
            //intervals.move(i, place++);
        }
    }

    if (matchesGroup.count() > 0)
        intervalsGroups.append(matchesGroup);

}

void
AllPlotInterval::placeIntervals()
{
    QList<RideFileInterval> intervals = rideItem->ride()->intervals();
    QList< QList<RideFileInterval> > intervalsGroups;

    sortIntervals(intervals, intervalsGroups);

    intervalLigns.clear();

    if (intervalsGroups.count()>0)
        intervalLigns.append(intervalsGroups.at(0));
    else {
        QList<RideFileInterval> intervalsLign1;
        intervalLigns.append(intervalsLign1);
    }

    while (intervals.count()>0) {
        const RideFileInterval &interval = intervals.first();

        int lign = 0;
        bool placed = false;

        while (!placed) {
            bool place = true;

            /*if (interval.isPeak()) {
                intervals.removeFirst();
                placed = true;
            }*/

            foreach(const RideFileInterval &placedinterval, intervalLigns.at(lign)) {
                if (interval.stop>placedinterval.start && interval.start<placedinterval.stop)
                    place = false;
            }
            if (place) {
                intervalLigns[lign].append(interval);
                intervals.removeFirst();
                placed = true;
            } else {
                lign++;
                if (intervalLigns.count()<=lign) {
                    QList<RideFileInterval> intervalsLign;
                    intervalLigns.append(intervalsLign);
                }
            }
         }
    }

    setFixedHeight((1+intervalLigns.count())*10);
    setAxisScale(yLeft, 0, 3000*intervalLigns.count());
}

void
AllPlotInterval::setColorForIntervalCurve(QwtPlotIntervalCurve *intervalCurve, const RideFileInterval &interval, bool selected)
{
    QColor color;
    if (interval.isPeak()) {
        color = Qt::lightGray;
    } else if (interval.isMatch()) {
        color = Qt::red;
    }
    else {
        color = GColor(CINTERVALHIGHLIGHTER);
    }

    QPen ihlPen = QPen(color);
    intervalCurve->setPen(ihlPen);
    QColor ihlbrush = QColor(color);
    if (!selected)
        ihlbrush.setAlpha(128);
    intervalCurve->setPen(ihlbrush);   // fill below the line
    intervalCurve->setBrush(ihlbrush);   // fill below the line
}

void
AllPlotInterval::refreshIntervalCurve()
{
    foreach(QwtPlotIntervalCurve *curve, curves.values()) {
        curve->detach();
        delete curve;
    }
    curves.clear();

    int level=0;
    foreach(const QList<RideFileInterval> &intervalsLign, intervalLigns) {

        foreach(const RideFileInterval &interval, intervalsLign) {
            QwtPlotIntervalCurve *intervalCurve = new QwtPlotIntervalCurve();
            intervalCurve->setYAxis(QwtAxis::yLeft);

            setColorForIntervalCurve(intervalCurve, interval, false);

            int max = 3000*intervalLigns.count();
            intervalCurve->setSamples(new AllPlotIntervalData(this, context, level, max, rideItem, interval));

            intervalCurve->attach(this);
            curves.insert(interval, intervalCurve);
        }
        level++;
    }
}

void
AllPlotInterval::intervalHover(RideFileInterval chosen)
{
    foreach(RideFileInterval interval, curves.keys()) {
        if (chosen == interval ||
                (rideItem->ride()->intervals().indexOf(interval) > -1 &&
                 context->athlete->allIntervalItems()->child(rideItem->ride()->intervals().indexOf(interval)) != NULL &&
                 context->athlete->allIntervalItems()->child(rideItem->ride()->intervals().indexOf(interval))->isSelected() )) {
            setColorForIntervalCurve(curves.value(interval), interval, true);
        } else  {
            setColorForIntervalCurve(curves.value(interval), interval, false);
        }
    }
    replot();
}

void
AllPlotInterval::intervalCurveHover(QwtPlotIntervalCurve *curve)
{
    if (curve != NULL) {
        RideFileInterval interval = curves.key(curve);
        //intervalHover(interval);

        // tell the charts -- and block signals whilst they occur
        blockSignals(true);
        context->notifyIntervalHover(interval);
        blockSignals(false);

    } else {
        context->notifyIntervalHover(RideFileInterval()); // clear

    }
}

void
AllPlotInterval::intervalCurveClick(QwtPlotIntervalCurve *curve) {
    RideFileInterval interval = curves.key(curve);
    int  idx = rideItem->ride()->intervals().indexOf(interval);


    if (idx != -1) {
        context->athlete->allIntervalItems()->child(idx)->setSelected(!context->athlete->allIntervalItems()->child(idx)->isSelected());

        if (QApplication::keyboardModifiers() != Qt::ControlModifier) {
            const QTreeWidgetItem *allIntervals = context->athlete->allIntervalItems();
            for (int i=0; i<allIntervals->childCount(); i++) {
                if (i!=idx)
                    context->athlete->allIntervalItems()->child(i)->setSelected(false);
            }
        }
    }
}

void
AllPlotInterval::intervalCurveDblClick(QwtPlotIntervalCurve *curve) {
    RideFileInterval interval = curves.key(curve);
    int  idx = rideItem->ride()->intervals().indexOf(interval);


    if (idx != -1) {
        context->athlete->allIntervalItems()->child(idx)->setSelected(!context->athlete->allIntervalItems()->child(idx)->isSelected());
        context->notifyIntervalZoom((IntervalItem *)context->athlete->allIntervalItems()->child(idx));
    }
}




/*
 *
 */

double
AllPlotIntervalData::x(size_t i) const
{
    //if (interval == NULL) return 0; // out of bounds !?
    double multiplier = context->athlete->useMetricUnits ? 1 : MILES_PER_KM;

    // which point are we returning?
    switch (i%4) {
        case 0 : return plot->bydist ? multiplier * rideItem->ride()->timeToDistance(interval.start) : interval.start/60; // bottom left
        case 1 : return plot->bydist ? multiplier * rideItem->ride()->timeToDistance(interval.start) : interval.start/60; // top left
        case 2 : return plot->bydist ? multiplier * rideItem->ride()->timeToDistance(interval.stop) : interval.stop/60; // top right
        case 3 : return plot->bydist ? multiplier * rideItem->ride()->timeToDistance(interval.stop) : interval.stop/60; // bottom right
    }
    return 0; // shouldn't get here, but keeps compiler happy
}


double
AllPlotIntervalData::ymin(size_t) const
{
    return max-2000-3000*(level);
}

double
AllPlotIntervalData::ymax(size_t i) const
{
    switch (i%4) {
        case 0 : return ymin(i); // bottom left
        case 1 : return max-3000*(level); // top left
        case 2 : return max-3000*(level); // top right
        case 3 : return ymin(i); // bottom right
    }
    return 0; // shouldn't get here, but keeps compiler happy
}


size_t
AllPlotIntervalData::size() const { return 4; }

QwtIntervalSample AllPlotIntervalData::sample(size_t i) const {
    return QwtIntervalSample( x(i), ymin(i), ymax(i) );
}

QRectF
AllPlotIntervalData::boundingRect() const
{
    return QRectF(0, 5000, 5100, 5100);
}

AllPlotIntervalCanvasPicker::AllPlotIntervalCanvasPicker(QwtPlot *plot):
    QObject(plot),
    d_selectedCurve(NULL),
    d_selectedPoint(-1)
{
    canvas = static_cast<QwtPlotCanvas*>(plot->canvas());
    canvas->installEventFilter(this);


    // We want the focus, but no focus rect. The
    canvas->setFocusPolicy(Qt::StrongFocus);
    canvas->setFocusIndicator(QwtPlotCanvas::ItemFocusIndicator);
}

bool
AllPlotIntervalCanvasPicker::event(QEvent *e)
{
    if ( e->type() == QEvent::User )
    {
        //showCursor(true);
        return true;
    }
    return QObject::event(e);
}

bool
AllPlotIntervalCanvasPicker::eventFilter(QObject *object, QEvent *e)
{
    // for our canvas ?
    if (object != canvas) return false;

    switch(e->type())
    {
        default:
            QApplication::postEvent(this, new QEvent(QEvent::User));
            break;
        case QEvent::MouseButtonDblClick:
            select(((QMouseEvent *)e)->pos(), true, true);
            break;
        case QEvent::MouseButtonPress:
            select(((QMouseEvent *)e)->pos(), true, false);
            break;
        case QEvent::MouseMove:
            select(((QMouseEvent *)e)->pos(), false, false);
            break;
    }
    return QObject::eventFilter(object, e);
}

// Select the point at a position. If there is no point
// deselect the selected point

void
AllPlotIntervalCanvasPicker::select(const QPoint &pos, bool clicked, bool dblClicked)
{
    QwtPlotIntervalCurve *curve = NULL;
    int index = -1;

    const QwtPlotItemList& itmList = plot()->itemList();
    for ( QwtPlotItemIterator it = itmList.begin();
        it != itmList.end(); ++it )
    {
        if ( (*it)->rtti() == QwtPlotItem::Rtti_PlotIntervalCurve )
        {
            QwtPlotIntervalCurve *c = (QwtPlotIntervalCurve*)(*it);

            double xmin = c->plot()->transform(c->xAxis(),c->sample(0).value);
            double xmax = c->plot()->transform(c->xAxis(),c->sample(2).value);
            double ymax = c->plot()->transform(c->yAxis(),c->sample(2).interval.minValue());
            double ymin = c->plot()->transform(c->yAxis(),c->sample(2).interval.maxValue());

            if ( pos.x()>=xmin &&
                 pos.x()<=xmax &&
                 pos.y()>=ymin &&
                 pos.y()<=ymax)
            {
                curve = c;
            }
        }
    }

    d_selectedCurve = NULL;
    d_selectedPoint = -1;

    if ( curve )
    {
        // picked one
        d_selectedCurve = curve;
        d_selectedPoint = index;

        if (dblClicked)
            pointDblClicked(curve, index); // emit
        else if (clicked)
            pointClicked(curve, index); // emit
        else
            pointHover(curve, index);  // emit
    } else {
        // didn't
        if (dblClicked)
            pointDblClicked(NULL, index); // emit
        else if (clicked)
            pointClicked(NULL, -1); // emit
        else
            pointHover(NULL, -1);  // emit

    }
}
