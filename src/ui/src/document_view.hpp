#pragma once

#include <QObject>

namespace mirrorfly
{
    // View magnification is independent of document formatting and edit history.
    class DocumentView : public QObject
    {
        Q_OBJECT
        Q_PROPERTY(qreal zoom READ zoom NOTIFY zoomChanged)

    public:
        explicit DocumentView(QObject* parent = nullptr);
        qreal zoom() const;
        Q_INVOKABLE bool setZoom(qreal value);

    signals:
        void zoomChanged();

    private:
        qreal zoom_ = 1;
    };
}
