// Copyright (c) 2024 Cecilia Norevik Bratlie, Nils Petter Skålerud, Eimen Oueslati
// SPDX-License-Identifier: MIT

#ifndef EVALUATOR_H
#define EVALUATOR_H

// Qt header files.
#include <QChar>
#include <QJsonArray>
#include <QMap>
#include <QString>
#include <QVariant>

// Other header files.
#include "VectorTiles.h"

class Evaluator
{
private:
    static void setupExpressionMap();
    static QMap<QString, QVariant(*)(const QJsonArray&, const AbstractLayerFeature*, int mapZoomLevel, float vpZoomeLevel)> m_expressionMap;
    static QVariant all(const QJsonArray& array, const AbstractLayerFeature* feature, int mapZoomLevel, float vpZoomeLevel);
    static QVariant case_(const QJsonArray& array, const AbstractLayerFeature* feature, int mapZoomLevel, float vpZoomeLevel);
    static QVariant coalesce(const QJsonArray& array, const AbstractLayerFeature* feature, int mapZoomLevel, float vpZoomeLevel);
    static QVariant compare(const QJsonArray& array, const AbstractLayerFeature* feature, int mapZoomLevel, float vpZoomeLevel);
    static QVariant get(const QJsonArray& array, const AbstractLayerFeature* feature, int mapZoomLevel, float vpZoomeLevel);
    static QVariant greater(const QJsonArray& array, const AbstractLayerFeature* feature, int mapZoomLevel, float vpZoomeLevel);
    static QVariant has(const QJsonArray& array, const AbstractLayerFeature* feature, int mapZoomLevel, float vpZoomeLevel);
    static QVariant in(const QJsonArray& array, const AbstractLayerFeature* feature, int mapZoomLevel, float vpZoomeLevel);
    static QVariant interpolate(const QJsonArray& array, const AbstractLayerFeature* feature, int mapZoomLevel, float vpZoomeLevel);
    static QVariant match(const QJsonArray& array, const AbstractLayerFeature* feature, int mapZoomLevel, float vpZoomeLevel);

public:
    Evaluator(){};
    static QVariant resolveExpression(const QJsonArray& expression, const AbstractLayerFeature* feature, int mapZoomLevel, float vpZoomeLevel);
};

#endif // EVALUATOR_H
