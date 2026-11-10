/**
 * @file glass_parameters.cpp
 * @brief The live glass numbers for glass_parameters.h.
 */

#include "glass_parameters.h"

namespace lens::app {

GlassParameters::GlassParameters(QObject* parent)
    : QObject(parent)
{
}

bool GlassParameters::holds(std::string_view const key)
{
    return key == "frostedBlur" or key == "frostedSaturation" or key == "frostedRim" or key == "liquidBlur" or key == "liquidRim" or key == "liquidRefraction";
}

bool GlassParameters::set(std::string_view const key, int const value)
{
    if (key == "frostedBlur") {
        if (frostedBlur_ == value) return false;
        frostedBlur_ = value;
        emit frostedBlurChanged();
        return true;
    }
    if (key == "frostedSaturation") {
        if (frostedSaturation_ == value) return false;
        frostedSaturation_ = value;
        emit frostedSaturationChanged();
        return true;
    }
    if (key == "frostedRim") {
        if (frostedRim_ == value) return false;
        frostedRim_ = value;
        emit frostedRimChanged();
        return true;
    }
    if (key == "liquidBlur") {
        if (liquidBlur_ == value) return false;
        liquidBlur_ = value;
        emit liquidBlurChanged();
        return true;
    }
    if (key == "liquidRim") {
        if (liquidRim_ == value) return false;
        liquidRim_ = value;
        emit liquidRimChanged();
        return true;
    }
    if (key == "liquidRefraction") {
        if (liquidRefraction_ == value) return false;
        liquidRefraction_ = value;
        emit liquidRefractionChanged();
        return true;
    }
    return false;
}

} // namespace lens::app
