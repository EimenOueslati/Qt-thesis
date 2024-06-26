// Copyright (c) 2024 Cecilia Norevik Bratlie, Nils Petter Skålerud, Eimen Oueslati
// SPDX-License-Identifier: MIT

#ifndef BACH_TILELOADER_H
#define BACH_TILELOADER_H

// Qt header files
#include <QMutex>
#include <QMutexLocker>
#include <QNetworkAccessManager>
#include <QObject>
#include <QThreadPool>
#include <QUrl>

// STL header files
#include <functional>
#include <map>
#include <memory>
#include <set>

// Other header files
#include "RequestTilesResult.h"
#include "TileCoord.h"
#include "Utilities.h"
#include "VectorTiles.h"

class UnitTesting;

namespace Bach {
    enum class LoadedTileState {
        Ok,
        Pending,
        ParsingFailed,
        Cancelled,
        UnknownError
    };

    /*!
     * \class System for loading, storing and caching map-tiles.
     *
     * Can be used to load tiles for the MapWidget.
     */
    class TileLoader : public QObject
    {
        Q_OBJECT

    private:
        // Use the static creator functions.
        // This constructor alone will not guarantee you a functional
        // TileLoader object.
        TileLoader();
    public:
        // We disallow implicit copying.
        TileLoader(const TileLoader&) = delete;
        // Inheriting from QObject makes our class non-movable.
        TileLoader(TileLoader&&) = delete;
        ~TileLoader(){};

        // Disallow copying.
        TileLoader& operator=(const TileLoader&) = delete;
        // Our class is non-movable.
        TileLoader& operator=(TileLoader&&) = delete;

        // Returns the path to the general cache storage for the application.
        static QString getGeneralCacheFolder();
        // Returns the path to the tile cache storage for the application.
        // This guaranteed to be a subfolder of the general cache.
        static QString getTileCacheFolder();


        // We can't return by value below, because TileLoader is a QObject and therefore
        // doesn't support move-semantics.
        static std::unique_ptr<TileLoader> fromTileUrlTemplate(
            const QString &pbfUrlTemplate,
            const QString &pngUrlTemplate,
            StyleSheet&& styleSheet);

        static std::unique_ptr<TileLoader> newLocalOnly(StyleSheet&& styleSheet);

        using LoadTileOverrideFnT = QByteArray const*(TileCoord, TileType);
        static std::unique_ptr<TileLoader> newDummy(
            const QString &diskCachePath,
            std::function<LoadTileOverrideFnT> loadTileOverride = nullptr,
            bool loadRaster = true,
            std::optional<int> workerThreadCount = std::nullopt);

        QString getTileDiskPath(TileCoord coord, TileType tileType);

        std::optional<Bach::LoadedTileState> getTileState_Vector(TileCoord) const;

    signals:
        /*!
         * @brief Gets signalled whenever a tile is finished loading,
         * the coordinates of said tile. Will not be signalled if the
         * tile loading process is cancelled.
         */
        void tileFinished(TileCoord);

    private:
        StyleSheet styleSheet;

        QString pbfLinkTemplate;
        QString pngUrlTemplate;

        QNetworkAccessManager networkManager;

        // Controls whether the TileLoader should try to access
        // web when loading.
        bool useWeb = true;

        // Controls whether we should load raster-tiles.
        bool loadRaster = true;

        std::function<LoadTileOverrideFnT> loadTileOverride = nullptr;

        // Directory path to tile cache storage.
        QString tileCacheDiskPath;

        struct StoredVectorTile {
            // Current loading-state of this tile.
            Bach::LoadedTileState state = {};

            // Stores the final vectorTile data.
            //
            // We use std::unique_ptr over QScopedPointer
            // because QScopedPointer doesn't support move semantics.
            std::unique_ptr<VectorTile> tileData;

            // Tells us whether this tile is safe to return to
            // rendering.
            bool isReadyToRender() const {
                return state == Bach::LoadedTileState::Ok;
            }

            // Creates a new tile-item with a pending state.
            static StoredVectorTile newPending() {
                StoredVectorTile temp;
                temp.state = Bach::LoadedTileState::Pending;
                return temp;
            }
        };

        struct StoredRasterTile {
            // Current loading-state of this tile.
            Bach::LoadedTileState state = {};

            QImage image;

            // Tells us whether this tile is safe to return to
            // rendering.
            bool isReadyToRender() const {
                return state == Bach::LoadedTileState::Ok;
            }

            // Creates a new tile-item with a pending state.
            static StoredRasterTile newPending() {
                StoredRasterTile temp;
                temp.state = Bach::LoadedTileState::Pending;
                return temp;
            }
        };
        /* This contains our memory tile-cache.
         *
         * IMPORTANT! Only use when 'tileMemoryLock' is locked!
         *
         * We had to use std::map because QMap doesn't support move semantics,
         * which interferes with our automated resource cleanup.
         */
        std::map<TileCoord, StoredVectorTile> vectorTileMemory;
        /* This contains our memory tile-cache.
         *
         * IMPORTANT! Only use when 'tileMemoryLock' is locked!
         *
         * We had to use std::map because QMap doesn't support move semantics,
         * which interferes with our automated resource cleanup.
         */
        std::map<TileCoord, StoredRasterTile> rasterTileMemory;

        // We use unique-ptr here to let use the lock in const methods.
        std::unique_ptr<QMutex> _tileMemoryLock = std::make_unique<QMutex>();

        // Generates the scoped lock for our tile-memory.
        // Will block if mutex is already held.
        QMutexLocker<QMutex> createTileMemoryLocker() const { return QMutexLocker(_tileMemoryLock.get()); }

    public:
        // Function signature of the tile-loaded
        // callback passed into 'requestTiles'.
        using TileLoadedCallbackFn = std::function<void(TileCoord)>;

        QScopedPointer<Bach::RequestTilesResult> requestTiles(
            const std::set<TileCoord> &requestInput,
            const TileLoadedCallbackFn &tileLoadedSignalFn,
            bool loadMissingTiles);
        // Overload where we don't need to pass any callback function.
        auto requestTiles(
            const std::set<TileCoord> &requestInput,
            bool loadMissingTiles)
        {
            return requestTiles(requestInput, nullptr, loadMissingTiles);
        }

        // Overload where callback can get passed as nullptr and
        // the TileLoader will not load missing tiles if the
        // callback is nullptr.
        auto requestTiles(
            const std::set<TileCoord> &requestInput,
            const TileLoadedCallbackFn &tileLoadedSignalFn = nullptr)
        {
            return requestTiles(
                requestInput,
                tileLoadedSignalFn,
                static_cast<bool>(tileLoadedSignalFn));
        }

    private:
        struct LoadJob {
            TileCoord tileCoord;
            TileType type;
        };
        void queueTileLoadingJobs(
            const QVector<LoadJob> &input,
            const TileLoadedCallbackFn &signalFn);

        // Thread-pool for the tile-loader worker threads.
        QThreadPool threadPool;
        // Thread-pool for the tile-loader worker threads.
        QThreadPool &getThreadPool() { return threadPool; }

        bool loadFromDisk_Vector(TileCoord coord, TileLoadedCallbackFn signalFn);
        bool loadFromDisk_Raster(TileCoord coord, TileLoadedCallbackFn signalFn);
        void networkReplyHandler_Raster(
            QNetworkReply *rasterReply,
            TileCoord coord,
            TileLoadedCallbackFn signalFn);
        void networkReplyHandler_Vector(
            QNetworkReply *vectorReply,
            TileCoord coord,
            TileLoadedCallbackFn signalFn);
        void loadFromWeb_Raster(TileCoord coord, TileLoadedCallbackFn signalFn);
        void loadFromWeb_Vector(TileCoord coord, TileLoadedCallbackFn signalFn);
        void writeTileToDisk_Raster(
            TileCoord coord,
            const QByteArray &rasterBytes);
        void writeTileToDisk_Vector(
            TileCoord coord,
            const QByteArray &vectorBytes);
        void insertIntoTileMemory_Vector(
            TileCoord coord,
            const QByteArray &vectorBytes,
            TileLoadedCallbackFn signalFn);
        void insertIntoTileMemory_Raster(
            TileCoord coord,
            const QByteArray &rasterBytes,
            TileLoadedCallbackFn signalFn);
    };

    QString setPbfLink(TileCoord tileCoord, const QString &pbfLinkTemplate);

    bool writeTileToDiskCache(
        const QString& basePath,
        TileCoord coord,
        const QByteArray &vectorBytes,
        const QByteArray &rasterBytes);
    bool writeTileToDiskCache_Vector(
        const QString& basePath,
        TileCoord coord,
        const QByteArray &vectorBytes);
    bool writeTileToDiskCache_Raster(
        const QString& basePath,
        TileCoord coord,
        const QByteArray &rasterBytes);

    QString tileDiskCacheSubPath(TileCoord coord, TileType tileType);
}


#endif // BACH_TILELOADER_H
