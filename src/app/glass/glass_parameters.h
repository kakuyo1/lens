#pragma once

/**
 * @file glass_parameters.h
 * @brief The six glass numbers as the surfaces read them, live while a slider moves (docs/adr/0173).
 *
 * The settings map is one binding dependency for the whole application: every surface that reads a key rebuilds the
 * projection when any key moves, and the application keeps about twenty glass cards alive -- each card's GlassSurface
 * and GlassHost read six of them. A slider's stops land a dozen times a second, so those six are projected on their
 * own -- one Q_PROPERTY each, announced one at a time -- and the map waits until the gesture settles. A reader
 * changing a glass number by a slider therefore repaints the glass, and nothing else.
 *
 * This is a mirror, not a second store: AppController writes the document first and hands the value here, and reads
 * the document back into it whenever something else moves the keys (a restore, a sync). Nothing clamps here; the
 * settings table's row is the clamp.
 */

#include <QObject>
#include <QtQml/qqmlregistration.h>

#include <string_view>

namespace lens::app {

class GlassParameters final : public QObject {
    Q_OBJECT
    QML_ANONYMOUS
    QML_UNCREATABLE("Owned by Controller; reached as Controller.glassParameters")
    Q_PROPERTY(int frostedBlur READ frostedBlur NOTIFY frostedBlurChanged)
    Q_PROPERTY(int frostedSaturation READ frostedSaturation NOTIFY frostedSaturationChanged)
    Q_PROPERTY(int frostedRim READ frostedRim NOTIFY frostedRimChanged)
    Q_PROPERTY(int liquidBlur READ liquidBlur NOTIFY liquidBlurChanged)
    Q_PROPERTY(int liquidRim READ liquidRim NOTIFY liquidRimChanged)
    Q_PROPERTY(int liquidRefraction READ liquidRefraction NOTIFY liquidRefractionChanged)

public:
    /// @note Not default-constructible: QML reaches the one instance through Controller.glassParameters, and a
    ///       zero-argument constructor would put the type on QML's creation side instead.
    explicit GlassParameters(QObject* parent);

    int frostedBlur() const
    {
        return frostedBlur_;
    }
    int frostedSaturation() const
    {
        return frostedSaturation_;
    }
    int frostedRim() const
    {
        return frostedRim_;
    }
    int liquidBlur() const
    {
        return liquidBlur_;
    }
    int liquidRim() const
    {
        return liquidRim_;
    }
    int liquidRefraction() const
    {
        return liquidRefraction_;
    }

    /// @return Whether @p key is one of the six the glass surfaces read.
    static bool holds(std::string_view key);

    /**
     * @brief Take @p key's value, announcing that one property when it moved.
     * @return Whether the value moved. A key that is not one of the six is left alone.
     */
    bool set(std::string_view key, int value);

signals:
    void frostedBlurChanged();
    void frostedSaturationChanged();
    void frostedRimChanged();
    void liquidBlurChanged();
    void liquidRimChanged();
    void liquidRefractionChanged();

private:
    int frostedBlur_       = 0;
    int frostedSaturation_ = 0;
    int frostedRim_        = 0;
    int liquidBlur_        = 0;
    int liquidRim_         = 0;
    int liquidRefraction_  = 0;
};

} // namespace lens::app
