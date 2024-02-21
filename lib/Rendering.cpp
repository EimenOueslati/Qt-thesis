#include "Rendering.h"

int Bach::calcMapZoomLevelForTileSizePixels(
    int vpWidth,
    int vpHeight,
    double vpZoom,
    int desiredTileWidth)
{
    // Calculate current tile size based on the largest dimension and current scale
    int currentTileSize = qMax(vpWidth, vpHeight);

    // Calculate desired scale factor
    double desiredScale = (double)desiredTileWidth / currentTileSize;

    // Figure out how the difference between the zoom levels of viewport and map
    // needed to satisfy the pixel-size requirement.
    double newMapZoomLevel = vpZoom - log2(desiredScale);

    // Round to int, and clamp output to zoom level range.
    return std::clamp((int)round(newMapZoomLevel), 0, maxZoomLevel);
}

QPair<double, double> calcViewportSizeNorm(double vpZoomLevel, double viewportAspect) {
    // Math formula can be seen in the figure in the report, with the caption
    // "Calculating viewport size as a factor of the world map"
    auto temp = 1 / pow(2, vpZoomLevel);
    return {
        temp * qMin(1.0, 1.0 / viewportAspect),
        temp * qMax(1.0, viewportAspect)
    };
}

QVector<TileCoord> Bach::calcVisibleTiles(
    double vpX,
    double vpY,
    double vpAspect,
    double vpZoomLevel,
    int mapZoomLevel)
{
    mapZoomLevel = qMax(0, mapZoomLevel);

    auto [vpWidthNorm, vpHeightNorm] = calcViewportSizeNorm(vpZoomLevel, vpAspect);

    auto vpMinNormX = vpX - (vpWidthNorm / 2.0);
    auto vpMaxNormX = vpX + (vpWidthNorm / 2.0);
    auto vpMinNormY = vpY - (vpHeightNorm / 2.0);
    auto vpMaxNormY = vpY + (vpHeightNorm / 2.0);

    auto tileCount = 1 << mapZoomLevel;

    auto clampToGrid = [&](int i) {
        return std::clamp(i, 0, tileCount-1);
    };
    auto leftmostTileX = clampToGrid(floor(vpMinNormX * tileCount));
    auto rightmostTileX = clampToGrid(ceil(vpMaxNormX * tileCount));
    auto topmostTileY = clampToGrid(floor(vpMinNormY * tileCount));
    auto bottommostTileY = clampToGrid(ceil(vpMaxNormY * tileCount));

    QVector<TileCoord> visibleTiles;
    for (int y = topmostTileY; y <= bottommostTileY; y++) {
        for (int x = leftmostTileX; x <= rightmostTileX; x++) {
            visibleTiles += { mapZoomLevel, x, y };
        }
    }
    return visibleTiles;
}

void paintSingleTileDebug(
    QPainter& painter,
    TileCoord const& tileCoord,
    QPoint pixelPos,
    QTransform const& transform)
{
    painter.setPen(Qt::green);
    painter.drawLine(transform.map(QLineF{ QPointF(0.45, 0.45), QPointF(0.55, 0.55) }));
    painter.drawLine(transform.map(QLineF{ QPointF(0.55, 0.45), QPointF(0.45, 0.55) }));
    painter.drawRect(transform.mapRect(QRectF(0, 0, 1, 1)));

    {
        // Text rendering has issues if our coordinate system is [0, 1].
        // So we get it back to unscaled and just offset where we need it.
        painter.save();
        QTransform transform;
        transform.translate(pixelPos.x(), pixelPos.y());
        painter.setTransform(transform);
        painter.drawText(10, 30, tileCoord.toString());

        painter.restore();
    }
}

static void paintSingleTile(
    VectorTile const& tileData,
    QPainter& painter,
    int mapZoomLevel,
    float viewportZoomLevel,
    StyleSheet const& styleSheet,
    QTransform const& transformIn)
{
    for (auto const& abstractLayerStyle : styleSheet.m_layerStyles) {

        // Background is a special case and has no associated layer.
        if (abstractLayerStyle->type() == AbstractLayereStyle::LayerType::background) {
            // Fill the entire tile with a single color
            auto const& layerStyle = *static_cast<BackgroundStyle const*>(abstractLayerStyle);
            auto backgroundColor = layerStyle.getColorAtZoom(mapZoomLevel);
            painter.fillRect(transformIn.mapRect(QRect(0, 0, 1, 1)), backgroundColor);
            continue;
        }

        auto layerExists = tileData.m_layers.contains(abstractLayerStyle->m_sourceLayer);
        if (!layerExists) {
            continue;
        }
        auto const& layer = *tileData.m_layers[abstractLayerStyle->m_sourceLayer];

        if (abstractLayerStyle->type() == AbstractLayereStyle::LayerType::fill) {
            auto const& layerStyle = *static_cast<FillLayerStyle const*>(abstractLayerStyle);

            painter.setBrush(layerStyle.getFillColorAtZoom(mapZoomLevel));

            for (auto const& abstractFeature : layer.m_features) {
                if (abstractFeature->type() == AbstractLayerFeature::featureType::polygon) {
                    auto const& feature = *static_cast<PolygonFeature const*>(abstractFeature);
                    auto const& path = feature.polygon();

                    QTransform transform = transformIn;
                    transform.scale(1 / 4096.0, 1 / 4096.0);
                    auto newPath = transform.map(path);

                    painter.save();
                    painter.setPen(Qt::NoPen);
                    painter.drawPath(newPath);
                    painter.restore();
                }
            }
        }
        else if (abstractLayerStyle->type() == AbstractLayereStyle::LayerType::line) {
            auto const& layerStyle = *static_cast<LineLayerStyle const*>(abstractLayerStyle);

            painter.save();
            auto pen = painter.pen();
            auto lineColor = layerStyle.getLineColorAtZoom(mapZoomLevel);
            //lineColor.setAlphaF(layerStyle.getLineOpacityAtZoom(mapZoomLevel));
            pen.setColor(lineColor);
            pen.setWidth(layerStyle.getLineWidthAtZoom(mapZoomLevel));
            //pen.setMiterLimit(layerStyle.getLineMitterLimitAtZoom(mapZoomLevel));
            painter.setPen(pen);

            painter.setBrush(Qt::NoBrush);

            for (auto const& abstractFeature : layer.m_features) {
                if (abstractFeature->type() == AbstractLayerFeature::featureType::line) {
                    auto& feature = *static_cast<LineFeature const*>(abstractFeature);
                    auto const& path = feature.line();
                    QTransform transform = transformIn;
                    transform.scale(1 / 4096.0, 1 / 4096.0);
                    auto newPath = transform.map(path);

                    painter.save();
                    painter.drawPath(newPath);
                    painter.restore();
                }
            }

            painter.restore();
        }
    }
}

void Bach::paintTiles(
    QPainter& painter,
    double vpX,
    double vpY,
    double viewportZoomLevel,
    int mapZoomLevel,
    QMap<TileCoord, VectorTile const*> const& tileContainer,
    StyleSheet const& styleSheet)
{
    auto viewportWidth = painter.window().width();
    auto viewportHeight = painter.window().height();
    double vpAspectRatio = (double)viewportWidth / (double)viewportHeight;
    auto visibleTiles = calcVisibleTiles(
        vpX,
        vpY,
        vpAspectRatio,
        viewportZoomLevel,
        mapZoomLevel);

    auto largestDimension = qMax(viewportWidth, viewportHeight);

    double scale = pow(2, viewportZoomLevel - mapZoomLevel);
    double tileWidthNorm = scale;
    double tileHeightNorm = scale;

    auto font = painter.font();
    font.setPointSize(18);
    painter.setFont(font);

    // Calculate total number of tiles at the current zoom level
    int totalTilesAtZoom = 1 << mapZoomLevel;

    // Calculate the offset of the viewport center in pixel coordinates
    double centerNormX = vpX * totalTilesAtZoom * tileWidthNorm - 1.0 / 2;
    double centerNormY = vpY * totalTilesAtZoom * tileHeightNorm - 1.0 / 2;

    if (viewportHeight >= viewportWidth) {
        centerNormX += -0.5 * vpAspectRatio + 0.5;
    } else if (viewportWidth >= viewportHeight) {
        centerNormY += -0.5 * (1 / vpAspectRatio) + 0.5;
    }

    for (const auto& tileCoord : visibleTiles) {
        double posNormX = (tileCoord.x * tileWidthNorm) - centerNormX;
        double posNormY = (tileCoord.y * tileHeightNorm) - centerNormY;

        auto tilePixelPos = QPoint(
            round(posNormX * largestDimension),
            round(posNormY * largestDimension));
        int tileWidthPixels = round(tileWidthNorm * largestDimension);
        int tileHeightPixels = round(tileHeightNorm * largestDimension);

        painter.save();

        QTransform transform;
        transform.translate(tilePixelPos.x(), tilePixelPos.y());
        //transform.scale(largestDimension, largestDimension);
        painter.setTransform(transform);

        auto pen = painter.pen();
        pen.setColor(Qt::white);
        pen.setWidth(1);
        painter.setPen(pen);

        QTransform test;
        test.scale(largestDimension * scale, largestDimension * scale);

        auto tileIt = tileContainer.find(tileCoord);
        if (tileIt != tileContainer.end()) {
            auto const& tileData = **tileIt;

            painter.save();
            painter.setClipRect(
                0,
                0,
                tileWidthPixels,
                tileHeightPixels);

            paintSingleTile(
                tileData,
                painter,
                mapZoomLevel,
                viewportZoomLevel,
                styleSheet,
                test);

            painter.restore();
        }

        paintSingleTileDebug(painter, tileCoord, tilePixelPos, test);

        painter.restore();
    }
}
