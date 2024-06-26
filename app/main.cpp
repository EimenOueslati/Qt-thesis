// Copyright (c) 2024 Cecilia Norevik Bratlie, Nils Petter Skålerud, Eimen Oueslati
// SPDX-License-Identifier: MIT

// Qt header files.
#include <QApplication>
#include <QMessageBox>

// Other header files.
#include "MainWindow.h"
#include "TileLoader.h"
#include "Utilities.h"

// Helper function to let the program shut down easily if there are errors
// during startup and initialisation.
[[noreturn]] void earlyShutdown(const QString &msg = "")
{
    if (msg != "")
        qCritical() << msg;

    QMessageBox::critical(
        nullptr,
        "Unexpected error.",
        "Application will now quit.");
    std::exit(EXIT_FAILURE);
}

// The main program.
int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    QCoreApplication::setApplicationName("qt_thesis_app");

    // Print the cache folder to the terminal.
    qDebug() << "Current file cache can be found in: " << Bach::TileLoader::getGeneralCacheFolder();

    // Read key from file.
    std::optional<QString> mapTilerKeyOpt = Bach::readMapTilerKey("key.txt");
    bool hasMapTilerKey = mapTilerKeyOpt.has_value();
    if (!hasMapTilerKey) {
        qWarning() << "Reading of the MapTiler key failed. " <<
            "App will attempt to only use local cache.";
    }

    // The style sheet type to load (can be many different types).
    MapType mapType = MapType::BasicV2;

    std::optional<QJsonDocument> styleSheetJsonResult = Bach::loadStyleSheetJson(
        mapType,
        mapTilerKeyOpt);
    if (!styleSheetJsonResult.has_value()) {
        earlyShutdown("Unable to load stylesheet from disk/web.");
    }
    const QJsonDocument &styleSheetJson = styleSheetJsonResult.value();

    // Parse the stylesheet into data that can be rendered.
    std::optional<StyleSheet> parsedStyleSheetResult = StyleSheet::fromJson(styleSheetJson);
    // If the stylesheet can't be parsed, there is nothing to render. Shut down.
    if (!parsedStyleSheetResult.has_value()) {
        earlyShutdown("Unable to parse stylesheet JSON into a parsed StyleSheet object.");
    }
    StyleSheet &styleSheet = parsedStyleSheetResult.value();

    // Load useful links from the stylesheet.
    // This only matters if one is online and has a MapTiler key.

    // Tracks whether or not to download data from web.
    bool useWeb = hasMapTilerKey;
    QString pbfUrlTemplate;
    QString pngUrlTemplate;
    if (useWeb) {
        ParsedLink pbfUrlTemplateResult = Bach::getPbfUrlTemplate(styleSheetJson, "maptiler_planet");
        ParsedLink rasterUrlTemplateResult = Bach::getRasterUrlTemplate(mapType, mapTilerKeyOpt);
        if (pbfUrlTemplateResult.resultType != ResultType::Success)
            useWeb = false;
        else if (rasterUrlTemplateResult.resultType != ResultType::Success)
            useWeb = false;
        else {
            pbfUrlTemplate = pbfUrlTemplateResult.link;
            pngUrlTemplate = rasterUrlTemplateResult.link;
        }
    }

    // Create TileLoader based on whether one can access online data or not.
    std::unique_ptr<Bach::TileLoader> tileLoaderPtr;
    if (useWeb) {
        tileLoaderPtr = Bach::TileLoader::fromTileUrlTemplate(
            pbfUrlTemplate,
            pngUrlTemplate,
            std::move(styleSheet));
    } else {
        tileLoaderPtr = Bach::TileLoader::newLocalOnly(std::move(styleSheet));
    }
    Bach::TileLoader &tileLoader = *tileLoaderPtr;

    // Creates the Widget that displays the map.
    auto *mapWidget = new MapWidget;
    // Set up the function that forwards requests from the
    // MapWidget into the TileLoader. This lambda does the
    // two components together.
    mapWidget->requestTilesFn = [&](auto tileList, auto tileLoadedCallback) {
        return tileLoader.requestTiles(tileList, tileLoadedCallback, true);
    };

    // Main window setup
    auto app = Bach::MainWindow(mapWidget);
    app.show();

    return a.exec();
}
